/* 文件功能：从 NASA 数据库建立候选物种目录及元素守恒矩阵。
 * 输入：推进剂假定化学式、数据库路径和温度范围。 
 * 输出：物种编号、相态、元素矩阵与守恒余量。 */
#ifndef MASS_BALANCE_H
#define MASS_BALANCE_H

#include "propellant_formula.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BALANCE_MAX_PRODUCTS 512
#define NASA_NAME_LENGTH 18
#define BALANCE_MAX_RANGES 8

typedef struct {
    size_t M;
    size_t N;
    size_t L;
    ChemicalElement elements[CHEM_MAX_ELEMENTS];
    char names[BALANCE_MAX_PRODUCTS][NASA_NAME_LENGTH + 1];

    double A[CHEM_MAX_ELEMENTS][BALANCE_MAX_PRODUCTS];
    double N_k[CHEM_MAX_ELEMENTS];
    int nasa_phase[BALANCE_MAX_PRODUCTS];
    int range_count[BALANCE_MAX_PRODUCTS];
    double T_low[BALANCE_MAX_PRODUCTS][BALANCE_MAX_RANGES];
    double T_high[BALANCE_MAX_PRODUCTS][BALANCE_MAX_RANGES];
    double molar_mass[BALANCE_MAX_PRODUCTS];
} MassBalance;

/* 功能：从数据库建立仅含气相的候选产物和元素守恒模型。
 * 输入：path、formula、T_min、T_max、out、error、error_size。
 * 输出：成功返回 1 并写入 MassBalance。 */
int mass_balance_from_nasa(const char *path, const PropellantFormula *formula,
                           double T_min, double T_max, MassBalance *out,
                           char *error, size_t error_size);

/* 功能：从数据库建立包含可选凝相的候选产物和元素守恒模型。
 * 输入：path、formula、T_min、T_max、include_condensed、out、error、error_size。
 * 输出：成功返回 1 并写入 MassBalance。 */
int mass_balance_from_nasa_phases(const char *path, const PropellantFormula *formula,
    double T_min, double T_max, int include_condensed, MassBalance *out,
    char *error, size_t error_size);

/* 功能：检查指定候选物种在当前温度是否可用。
 * 输入：balance、j、T。
 * 输出：返回可用性判断值。 */
int mass_balance_species_available(const MassBalance *balance, size_t j, double T);

/* 功能：根据给定 n_j 计算各元素守恒余量。
 * 输入：balance、n、r、error、error_size。
 * 输出：成功返回 1 并写入残差数组。 */
int mass_balance_residual(const MassBalance *balance, const double *n,
                          double *r, char *error, size_t error_size);

/* 功能：输出完整候选产物及元素守恒方程。
 * 输入：stream、balance。
 * 输出：写入指定输出流。 */
void mass_balance_print(FILE *stream, const MassBalance *balance);

#ifdef __cplusplus
}
#endif
#endif
