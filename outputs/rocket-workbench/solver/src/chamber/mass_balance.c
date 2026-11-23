/* 文件功能：从 NASA 数据库建立候选物种目录及元素守恒矩阵。
 * 输入：推进剂假定化学式、数据库路径和温度范围。
 * 输出：物种编号、相态、元素矩阵与守恒余量。 */
#include "mass_balance.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LINE_SIZE 512

/* 功能：把守恒模型错误写入缓冲区。
 * 输入：error、size、message。
 * 输出：返回失败值 0。 */
static int fail(char *error, size_t size, const char *message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

/* 功能：从固定列宽文本提取数值。
 * 输入：line、start、length、value。
 * 输出：成功返回 1 并写入数值。 */
static int number(const char *line, size_t start, size_t length, double *value)
{
    char field[32], *end;
    size_t i;
    if (length >= sizeof(field) || strlen(line) < start + length) return 0;
    memcpy(field, line + start, length);
    field[length] = '\0';
    for (i = 0; i < length; ++i)
        if (field[i] == 'D' || field[i] == 'd') field[i] = 'E';
    *value = strtod(field, &end);
    if (end == field || !isfinite(*value)) return 0;
    while (isspace((unsigned char)*end)) ++end;
    return *end == '\0';
}

/* 功能：比较两个元素符号。
 * 输入：a、b。
 * 输出：返回布尔判断值。 */
static int same_symbol(const char *a, const char *b)
{
    return toupper((unsigned char)a[0]) == toupper((unsigned char)b[0]) &&
           toupper((unsigned char)a[1]) == toupper((unsigned char)b[1]);
}

/* 功能：将候选产物重排为凝相、非原子气相和原子气相。
 * 输入：b。
 * 输出：返回错误信息或空指针。 */
static const char *order_like_textbook(MassBalance *b)
{
    size_t order[BALANCE_MAX_PRODUCTS], atom[CHEM_MAX_ELEMENTS];
    MassBalance *copy;
    size_t j, k, non_atomic = 0;
    for (k = 0; k < b->M; ++k) atom[k] = b->N;
    for (j = 0; j < b->N; ++j)
        if (b->nasa_phase[j] != 0) order[non_atomic++] = j;
    b->L = non_atomic;
    for (j = 0; j < b->N; ++j) {
        double total_atoms = 0;
        size_t element = b->M;
        if (b->nasa_phase[j] != 0) continue;
        for (k = 0; k < b->M; ++k) {
            total_atoms += b->A[k][j];
            if (b->A[k][j] == 1) element = k;
        }
        if (total_atoms == 1 && element < b->M) {
            if (atom[element] != b->N)
                return "More than one neutral atomic species for an element.";
            atom[element] = j;
        } else {
            order[non_atomic++] = j;
        }
    }
    for (k = 0; k < b->M; ++k) {
        if (atom[k] == b->N)
            return "Textbook numbering requires a neutral atomic gas for every element.";
        order[non_atomic+k] = atom[k];
    }
    copy = (MassBalance *)malloc(sizeof(*copy));
    if (!copy) return "Cannot allocate species permutation.";
    *copy = *b;
    for (j = 0; j < b->N; ++j) {
        size_t old = order[j];
        strcpy(b->names[j], copy->names[old]);
        b->nasa_phase[j] = copy->nasa_phase[old];
        b->range_count[j] = copy->range_count[old];
        b->molar_mass[j] = copy->molar_mass[old];
        memcpy(b->T_low[j], copy->T_low[old], sizeof(b->T_low[j]));
        memcpy(b->T_high[j], copy->T_high[old], sizeof(b->T_high[j]));
        for (k = 0; k < b->M; ++k) b->A[k][j] = copy->A[k][old];
    }
    free(copy);
    return NULL;
}

/* 功能：从数据库建立仅含气相的候选产物和元素守恒模型。
 * 输入：path、formula、T_min、T_max、out、error、size。
 * 输出：成功返回 1 并写入 MassBalance。 */
int mass_balance_from_nasa(const char *path, const PropellantFormula *formula,
                           double T_min, double T_max, MassBalance *out,
                           char *error, size_t size)
{
    return mass_balance_from_nasa_phases(path,formula,T_min,T_max,0,out,error,size);
}

/* 功能：检查指定候选物种在当前温度是否可用。
 * 输入：b、j、T。
 * 输出：返回可用性判断值。 */
int mass_balance_species_available(const MassBalance *b, size_t j, double T)
{
    int i;
    if (!b || j>=b->N || !isfinite(T) || T<=0 ||
        b->range_count[j]<0 || b->range_count[j]>BALANCE_MAX_RANGES) return 0;
    if (b->range_count[j]==0) return 1;
    for (i=0;i<b->range_count[j];++i)
        if (T>=b->T_low[j][i] && T<=b->T_high[j][i]) return 1;
    return 0;
}

/* 功能：从数据库建立包含可选凝相的候选产物和元素守恒模型。
 * 输入：path、formula、T_min、T_max、include_condensed、out、error、size。
 * 输出：成功返回 1 并写入 MassBalance。 */
int mass_balance_from_nasa_phases(const char *path, const PropellantFormula *formula,
                           double T_min, double T_max, int include_condensed,
                           MassBalance *out, char *error, size_t size)
{
    FILE *file;
    MassBalance *result;
    char line[LINE_SIZE], meta[LINE_SIZE], name[NASA_NAME_LENGTH+1] = "header";
    size_t k, j;
    int started = 0, ended = 0, ok = 0;
    const char *problem = "Malformed NASA product data.";
    if (!path || !out || !isfinite(T_min) || !isfinite(T_max) ||
        T_min <= 0 || T_max < T_min)
        return fail(error, size, "Invalid path, output or temperature interval.");
    if (!propellant_validate(formula, error, size)) return 0;
    file = fopen(path, "r");
    if (!file) return fail(error, size, "Cannot open thermo.inp; check path/current directory.");
    result = (MassBalance *)calloc(1, sizeof(*result));
    if (!result) {
        fclose(file);
        return fail(error, size, "Cannot allocate mass balance.");
    }
    result->M = formula->M;
    for (k = 0; k < formula->M; ++k) {
        result->elements[k] = formula->elements[k];
        result->N_k[k] = formula->N_k[k];
    }
    while (fgets(line, sizeof(line), file)) {
        double nr_value, phase, mw, atoms[CHEM_MAX_ELEMENTS] = {0};
        double lows[BALANCE_MAX_RANGES]={0},highs[BALANCE_MAX_RANGES]={0};
        double covered = T_min;
        int nr, i, eligible = 1, has_atoms = 0, has_coverage = 0, overlaps = 0;
        if (!started) {
            if (strncmp(line, "thermo", 6) == 0) {
                started = 1;

                if (!fgets(line, sizeof(line), file)) goto done;
            }
            continue;
        }
        if (strncmp(line, "END PRODUCTS", 12) == 0) { ended = 1; break; }
        if (line[0] == '!' || line[0] == '\n' || line[0] == '\r') continue;

        if (!line[0] || isspace((unsigned char)line[0])) goto done;
        for (j = 0; j < NASA_NAME_LENGTH && line[j] &&
                    !isspace((unsigned char)line[j]); ++j) name[j] = line[j];
        name[j] = '\0';
        if (!fgets(meta, sizeof(meta), file) ||
            !number(meta, 0, 2, &nr_value) ||
            !number(meta, 51, 1, &phase) ||
            !number(meta, 52, 13, &mw) || mw<=0 || phase<0 || floor(phase)!=phase ||
            nr_value < 0 || nr_value > 99 || floor(nr_value) != nr_value)
            goto done;
        nr = (int)nr_value;
        if ((!include_condensed && phase != 0) || nr == 0) eligible = 0;

        for (i = 0; i < 5; ++i) {
            size_t pos = 10 + (size_t)i * 8, s = 0, t;
            char symbol[3] = {0};
            double count;
            for (t = 0; t < 2; ++t)
                if (!isspace((unsigned char)meta[pos+t])) symbol[s++] = meta[pos+t];
            if (!number(meta, pos+2, 6, &count)) goto done;

            if (same_symbol(symbol, "E")) { eligible = 0; continue; }
            if (count == 0) continue;
            if (count < 0 || symbol[0] == '\0') goto done;
            has_atoms = 1;
            for (k = 0; k < formula->M; ++k)
                if (same_symbol(symbol, formula->elements[k].symbol)) break;
            if (k == formula->M) eligible = 0;
            else atoms[k] += count;
        }
        if (!has_atoms) eligible = 0;
        if (nr == 0) {

            if (!fgets(line, sizeof(line), file)) goto done;
        }
        for (i = 0; i < nr; ++i) {
            double low, high;
            if (!fgets(line, sizeof(line), file) ||
                !number(line, 0, 11, &low) || !number(line, 11, 11, &high) ||
                low <= 0 || high <= low) goto done;
            if (low <= covered && high >= covered) {
                covered = high;
                has_coverage = 1;
            }
            if (low<=T_max && high>=T_min) overlaps=1;
            if (i<BALANCE_MAX_RANGES) { lows[i]=low;highs[i]=high; }

            if (!fgets(line, sizeof(line), file) || !fgets(line, sizeof(line), file))
                goto done;
        }
        if (!eligible || (phase==0 ? (!has_coverage || covered<T_max) : !overlaps)) continue;
        if (nr>BALANCE_MAX_RANGES) { problem="Too many temperature ranges.";goto done; }
        if (result->N == BALANCE_MAX_PRODUCTS) {
            problem = "Too many products; increase BALANCE_MAX_PRODUCTS.";
            goto done;
        }
        for (j = 0; j < result->N; ++j)
            if (strcmp(result->names[j], name) == 0) {
                problem = "Duplicate selected species name in NASA data.";
                goto done;
            }
        strcpy(result->names[result->N], name);
        result->nasa_phase[result->N]=(int)phase;
        result->range_count[result->N]=nr;
        result->molar_mass[result->N]=mw/1000.0;
        memcpy(result->T_low[result->N],lows,sizeof(lows));
        memcpy(result->T_high[result->N],highs,sizeof(highs));
        for (k = 0; k < result->M; ++k) result->A[k][result->N] = atoms[k];
        ++result->N;
    }
    if (!ended || ferror(file)) {
        problem = "NASA product section is incomplete (END PRODUCTS required).";
        goto done;
    }
    if (result->N == 0) { problem = "No eligible neutral gas products."; goto done; }
    for (k = 0; k < result->M; ++k) {
        int present = 0;
        for (j = 0; j < result->N; ++j)
            if (result->A[k][j] > 0) present = 1;
        if (!present) {
            problem = "An input element has no eligible gas product carrier.";
            goto done;
        }
    }
    problem = order_like_textbook(result);
    if (problem) goto done;
    *out = *result;
    if (error && size) error[0] = '\0';
    ok = 1;
done:
    if (!ok && error && size) snprintf(error, size, "%s Record: %s", problem, name);
    free(result);
    fclose(file);
    return ok;
}

/* 功能：根据给定 n_j 计算各元素守恒余量。
 * 输入：b、n、r、error、size。
 * 输出：成功返回 1 并写入残差数组。 */
int mass_balance_residual(const MassBalance *b, const double *n,
                          double *r, char *error, size_t size)
{
    size_t k, j;
    double result[CHEM_MAX_ELEMENTS];
    if (!b || !n || !r || b->M == 0 || b->M > CHEM_MAX_ELEMENTS ||
        b->N == 0 || b->N > BALANCE_MAX_PRODUCTS)
        return fail(error, size, "Invalid mass balance or composition.");
    for (j = 0; j < b->N; ++j)
        if (!isfinite(n[j]) || n[j] < 0)
            return fail(error, size, "Product amounts must be finite and nonnegative (mol/kg).");
    for (k = 0; k < b->M; ++k) {
        result[k] = -b->N_k[k];
        for (j = 0; j < b->N; ++j) result[k] += b->A[k][j] * n[j];
        if (!isfinite(result[k])) return fail(error, size, "Mass balance residual overflow.");
    }
    for (k = 0; k < b->M; ++k) r[k] = result[k];
    if (error && size) error[0] = '\0';
    return 1;
}

/* 功能：输出完整候选产物及元素守恒方程。
 * 输入：stream、b。
 * 输出：写入指定输出流。 */ 
void mass_balance_print(FILE *stream, const MassBalance *b)
{
    size_t k, j;
    if (!stream || !b || b->M > CHEM_MAX_ELEMENTS || b->N > BALANCE_MAX_PRODUCTS) return;
    for (k = 0; k < b->M; ++k) {
        int terms = 0;
        fprintf(stream, "\n%s: ", b->elements[k].symbol);
        for (j = 0; j < b->N; ++j) {
            if (b->A[k][j] == 0) continue;
            if (terms) fprintf(stream, terms % 4 == 0 ? "\n   + " : " + ");
            if (b->A[k][j] != 1) fprintf(stream, "%g*", b->A[k][j]);
            fprintf(stream, "n_%lu[%s]", (unsigned long)(j+1), b->names[j]);
            ++terms;
        }
        fprintf(stream, "\n   = %.10f mol/kg\n", b->N_k[k]);
    }
}
