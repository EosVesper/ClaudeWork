/* 文件功能：准备标准态物性并评价混合物的吉布斯目标函数。 
 * 输入：候选物种、温度、标准压强、实际压强和组成。
 * 输出：标准态数据、目标函数值和错误信息。 */
#ifndef GIBBS_OBJECTIVE_H
#define GIBBS_OBJECTIVE_H

#include "mass_balance.h"
#include "thermo_properties.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t N, L;
    int available[BALANCE_MAX_PRODUCTS];
    double T, p_0;
    char names[BALANCE_MAX_PRODUCTS][NASA_NAME_LENGTH+1];
    double H_m0[BALANCE_MAX_PRODUCTS];
    double S_m0[BALANCE_MAX_PRODUCTS];
    double mu_0[BALANCE_MAX_PRODUCTS];
    double y[BALANCE_MAX_PRODUCTS];
} GibbsStandard;

typedef struct {
    double n_g;
    double Phi_j[BALANCE_MAX_PRODUCTS];
    double Phi;
    double g;
} GibbsResult;

/* 功能：从 NASA 物性准备指定温度的标准态化学势和焓熵。
 * 输入：balance、T、p_0、out、error、error_size。
 * 输出：成功返回 1 并写入 GibbsStandard。 */
int gibbs_prepare(const MassBalance *balance, double T, double p_0,
                   GibbsStandard *out, char *error, size_t error_size);

/* 功能：按给定组分和压强计算吉布斯目标函数。
 * 输入：standard、p、n、out、error、error_size。
 * 输出：成功返回 1 并写入 GibbsEvaluation。 */
int gibbs_evaluate(const GibbsStandard *standard, double p, const double *n,
                    GibbsResult *out, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
