/* 文件功能：在给定截面位置上求解等熵方程，得到温度、组分和全部热力参数。
 * 输入：候选产物表、燃烧室参考状态、膨胀模型和截面位置。
 * 输出：截面参数 NozzleSection 与该截面的组成。 */
#ifndef NOZZLE_SECTION_H
#define NOZZLE_SECTION_H

#include "nozzle_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 等熵方程求解选项。 */
typedef struct {
    double T_min;      /* 温度搜索下界，K */
    double T_max;      /* 温度搜索上界，K */
    double s_tolerance;/* 熵残差容差，J/(kg·K) */
    double T_tolerance;/* 温度收敛容差，K */
    int max_iterations;/* 内插/割线最大迭代次数 */
    EquilibriumOptions equilibrium; /* 平衡组分求解选项 */
} NozzleSectionOptions;

/* 功能：生成默认的等熵求解选项。
 * 输入：T_min、T_max（K）。
 * 输出：返回选项，熵容差 1e-6 J/(kg·K)，温度容差 1e-7 K。 */
NozzleSectionOptions nozzle_section_default_options(double T_min, double T_max);

/* 功能：在指定截面求解等熵方程 s_x(p,T,n) = s_c 及截面方程。
 * 输入：sys、ref、model、request、options（可为 NULL）、error、error_size。
 * 输出：成功返回 1 并写入 out 与 n_out（可为 NULL）；失败返回 0。
 *       状态码通过返回值区分，error 中给出可读原因。 */
int nozzle_section_solve(const NozzleSystem *sys, const NozzleReference *ref,
                         NozzleModel model, const NozzleSectionRequest *request,
                         const NozzleSectionOptions *options,
                         NozzleSection *out, double *n_out,
                         char *error, size_t error_size);

/* 功能：给定绝对压强直接求截面，供沿程推进反复调用。
 * 输入：sys、ref、model、p、options、n_guess（可为 NULL，作为初值并在返回时更新）、
 *       n_out（可为 NULL）。
 * 输出：成功返回 1 并写入 out。 */
int nozzle_section_at_pressure(const NozzleSystem *sys, const NozzleReference *ref,
                               NozzleModel model, double p,
                               const NozzleSectionOptions *options,
                               double *n_guess, NozzleSection *out, double *n_out,
                               char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
