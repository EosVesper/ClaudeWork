/* 文件功能：由燃烧室出口状态重建喷管所需的候选产物表（元素守恒模型）。
 * 输入：燃烧室出口状态、NASA 数据库路径和温度窗口。
 * 输出：可直接用于平衡求解的 MassBalance，以及与燃烧室候选表的比对结果。 */
#ifndef NOZZLE_BALANCE_H
#define NOZZLE_BALANCE_H

#include "nozzle_database.h"
#include "nozzle_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 候选产物表与燃烧室的比对结果，用于确认"以燃烧室为基础"没有走样。 */
typedef struct {
    size_t matched; /* 两边都出现的候选产物数 */
    size_t missing; /* 燃烧室有、重建表没有 */
    size_t extra;   /* 重建表有、燃烧室没有（通常为空） */
    size_t N;       /* 重建表的候选产物总数 */
    size_t L;       /* 重建表的凝相数 */
    char missing_names[4][NASA_NAME_LENGTH + 1];
} NozzleBalanceReport;

/* 功能：由燃烧室出口的元素总量反推推进剂假定化学式。
 * 输入：chamber。输出：成功返回 1 并写入 formula。 */
int nozzle_balance_formula(const ChamberOutlet *chamber,
                           PropellantFormula *out, char *error, size_t error_size);

/* 功能：在指定温度窗口内重建候选产物表。
 * 输入：chamber、thermo_path（NULL 表示当前目录 thermo.inp）、T_min、T_max。
 *       report 可为 NULL。
 * 输出：成功返回可释放的 MassBalance 指针；失败返回 NULL 并写入 error。
 *       该结构体约 0.3 MB，必须由本函数在堆上分配。 */
MassBalance *nozzle_balance_build(const ChamberOutlet *chamber,
                                  const char *thermo_path,
                                  double T_min, double T_max,
                                  NozzleBalanceReport *report,
                                  char *error, size_t error_size);

/* 功能：释放重建的候选产物表。
 * 输入：balance（可为 NULL）。
 * 输出：无。 */
void nozzle_balance_free(MassBalance *balance);

/* 功能：确保 NASA 数据库已加载，重复调用同一路径时跳过解析。
 * 加载前先把数据库规范化为"一个组分一条记录、多条温度段"，
 * 以避开燃烧室建表函数对重名记录的限制。
 * 输入：path 为数据库路径，NULL 表示默认文件。
 * 输出：成功返回 1；失败返回 0 并写入 error。 */
int nozzle_thermo_ensure(const char *path, char *error, size_t error_size);

/* 功能：取得实际加载的数据库路径（规范化后的副本）。
 * 输入：无。
 * 输出：返回常量字符串，未加载时为空串。 */
const char *nozzle_effective_path(void);

/* 功能：取得最近一次数据库规范化的统计信息。
 * 输入：无。
 * 输出：返回指向内部结构体的指针，可能为 NULL。 */
const NozzleDatabase *nozzle_database_info(void);

#ifdef __cplusplus
}
#endif

#endif
