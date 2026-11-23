/* 文件功能：由燃烧室出口状态重建喷管所需的候选产物表（元素守恒模型）。
 * 输入：燃烧室出口状态、NASA 数据库路径和温度窗口。
 * 输出：可直接用于平衡求解的 MassBalance，以及与燃烧室候选表的比对结果。 */
#include "nozzle_balance.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 已加载数据库的路径，用来避免重复解析同一个文件。 */
static char loaded_path[512];
/* 最近一次规范化的统计信息。 */
static NozzleDatabase database_info;

/* 功能：把状态错误写入缓冲区。
 * 输入：error、size、message。
 * 输出：返回失败值 0。 */
static int fail(char *error, size_t size, const char *message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

/* 功能：确保 NASA 数据库已加载，重复调用同一路径时跳过解析。
 * 输入：path。
 * 输出：成功返回 1；失败返回 0 并写入 error。 */
int nozzle_thermo_ensure(const char *path, char *error, size_t error_size)
{
    const char *source = path ? path : thermo_default_file();
    NozzleDatabase database;
    if (!source || source[0] == '\0')
        return fail(error, error_size, "NASA 数据库路径为空。");
    /* 先规范化：把同名多条记录合并成一条多温度段记录。 */
    if (!nozzle_database_prepare(source, &database, error, error_size)) return 0;
    database_info = database;
    if (loaded_path[0] != '\0' && strcmp(loaded_path, database.path) == 0) return 1;
    if (thermo_load(database.path) != 0) {
        loaded_path[0] = '\0';
        if (error && error_size)
            snprintf(error, error_size, "NASA 数据库加载失败：%s", thermo_last_error());
        return 0;
    }
    snprintf(loaded_path, sizeof(loaded_path), "%s", database.path);
    return 1;
}

/* 功能：取得实际加载的数据库路径。
 * 输入：无。
 * 输出：返回常量字符串。 */
const char *nozzle_effective_path(void) { return loaded_path; }

/* 功能：取得最近一次数据库规范化的统计信息。
 * 输入：无。
 * 输出：返回指针，未加载时为 NULL。 */
const NozzleDatabase *nozzle_database_info(void)
{
    return database_info.valid ? &database_info : NULL;
}

/* 功能：由燃烧室出口的元素总量反推推进剂假定化学式。
 * 输入：chamber、out、error、size。
 * 输出：成功返回 1 并写入 formula。 */
int nozzle_balance_formula(const ChamberOutlet *chamber,
                           PropellantFormula *out, char *error, size_t error_size)
{
    PropellantFormula formula = {0};
    size_t k;
    double mass_sum = 0.0;
    if (!chamber || !out || !chamber->valid)
        return fail(error, error_size, "燃烧室出口状态不可用。");
    if (chamber->M == 0 || chamber->M > CHEM_MAX_ELEMENTS)
        return fail(error, error_size, "元素数量不合法。");
    formula.M = chamber->M;
    for (k = 0; k < chamber->M; ++k) {
        double mass;
        formula.elements[k] = chamber->elements[k];
        if (!isfinite(chamber->N_k[k]) || chamber->N_k[k] <= 0)
            return fail(error, error_size, "元素摩尔数必须为正。");
        mass = chamber->N_k[k] * chamber->elements[k].A_r * 1e-3;
        if (!isfinite(mass) || mass <= 0)
            return fail(error, error_size, "元素质量分数不合法。");
        mass_sum += mass;
    }
    /* 归一化到严格的 1 kg，使 propellant_validate 的 1e-10 判据可通过。 */
    if (!isfinite(mass_sum) || mass_sum <= 0)
        return fail(error, error_size, "元素质量分数之和必须为正。");
    for (k = 0; k < chamber->M; ++k) {
        double fraction = chamber->N_k[k] * chamber->elements[k].A_r * 1e-3 / mass_sum;
        formula.g_p[k] = fraction;
        formula.N_k[k] = fraction / (chamber->elements[k].A_r * 1e-3);
    }
    if (!propellant_validate(&formula, error, error_size)) return 0;
    *out = formula;
    if (error && error_size) error[0] = '\0';
    return 1;
}

/* 功能：在候选产物表中查找指定名称。
 * 输入：balance、name。
 * 输出：找到返回 1，否则返回 0。 */
static int has_species(const MassBalance *balance, const char *name)
{
    size_t j;
    for (j = 0; j < balance->N; ++j)
        if (strcmp(balance->names[j], name) == 0) return 1;
    return 0;
}

/* 功能：在指定温度窗口内重建候选产物表。
 * 输入：chamber、thermo_path、T_min、T_max、report、error、size。
 * 输出：成功返回 MassBalance 指针；失败返回 NULL。 */
MassBalance *nozzle_balance_build(const ChamberOutlet *chamber,
                                  const char *thermo_path,
                                  double T_min, double T_max,
                                  NozzleBalanceReport *report,
                                  char *error, size_t error_size)
{
    MassBalance *balance;
    PropellantFormula formula;
    NozzleBalanceReport local = {0};
    const char *path = thermo_path ? thermo_path : thermo_default_file();
    size_t j;
    if (!chamber || !chamber->valid) {
        fail(error, error_size, "燃烧室出口状态不可用。");
        return NULL;
    }
    if (!isfinite(T_min) || !isfinite(T_max) || T_min <= 0 || T_max <= T_min) {
        fail(error, error_size, "候选产物温度窗口不合法。");
        return NULL;
    }
    if (!nozzle_balance_formula(chamber, &formula, error, error_size)) return NULL;
    if (!nozzle_thermo_ensure(path, error, error_size)) return NULL;
    /* 用规范化后的副本建表：重名记录已合并，温度窗可以放宽。 */
    path = nozzle_effective_path();

    balance = (MassBalance *)malloc(sizeof(*balance));
    if (!balance) {
        fail(error, error_size, "候选产物表内存不足。");
        return NULL;
    }
    if (!mass_balance_from_nasa_phases(path, &formula, T_min, T_max, 1,
                                       balance, error, error_size)) {
        free(balance);
        return NULL;
    }

    /* 与燃烧室候选表逐名比对：燃烧室的候选产物应当全部出现在重建表中。 */
    local.N = balance->N;
    local.L = balance->L;
    for (j = 0; j < chamber->N; ++j) {
        if (has_species(balance, chamber->species[j].name)) {
            ++local.matched;
        } else {
            if (local.missing < 4)
                snprintf(local.missing_names[local.missing],
                         sizeof(local.missing_names[0]), "%s", chamber->species[j].name);
            ++local.missing;
        }
    }
    for (j = 0; j < balance->N; ++j) {
        size_t i;
        int found = 0;
        for (i = 0; i < chamber->N; ++i)
            if (strcmp(balance->names[j], chamber->species[i].name) == 0) {
                found = 1;
                break;
            }
        if (!found) ++local.extra;
    }
    if (report) *report = local;
    if (error && error_size) error[0] = '\0';
    return balance;
}

/* 功能：释放重建的候选产物表。
 * 输入：balance。输出：无。 */
void nozzle_balance_free(MassBalance *balance) { free(balance); }
