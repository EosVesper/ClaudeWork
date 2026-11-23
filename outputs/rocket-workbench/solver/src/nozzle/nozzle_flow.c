/* 文件功能：沿喷管轴线推进，给出压强与面积比的沿程分布。
 * 输入：参考状态、膨胀模型、出口压强、截面数。
 * 输出：包含燃烧室截面在内的 NozzleSection 数组。 */
#include "nozzle_flow.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define NOZZLE_PROFILE_MAX 2000

/* 功能：按等比压强步长从燃烧室推进到给定出口压强。
 * 输入：sys、ref、model、options、p_exit、n_sections、profile、error、size。
 * 输出：成功返回 1 并写入 profile。 */
int nozzle_flow_profile(const NozzleSystem *sys, NozzleReference *ref,
                        NozzleModel model, const NozzleSectionOptions *options,
                        double p_exit, int n_sections, NozzleSection *profile,
                        char *error, size_t error_size)
{
    double n_guess[BALANCE_MAX_PRODUCTS];
    double ratio;
    int i;
    if (!sys || !ref || !profile)
        return 0;
    if (n_sections < 2 || n_sections > NOZZLE_PROFILE_MAX) {
        if (error && error_size) snprintf(error, error_size, "沿程截面数不合法。");
        return 0;
    }
    if (!isfinite(p_exit) || p_exit <= 0 || p_exit >= ref->p_c) {
        if (error && error_size) snprintf(error, error_size, "出口压强必须小于燃烧室压强。");
        return 0;
    }
    memcpy(n_guess, ref->n_c, sys->N * sizeof(*n_guess));
    ratio = p_exit / ref->p_c;
    for (i = 0; i < n_sections; ++i) {
        double fraction = (double)i / (double)(n_sections - 1);
        double p = ref->p_c * pow(ratio, fraction);
        if (i == n_sections - 1) p = p_exit; /* 末端严格取给定值 */
        if (!nozzle_section_at_pressure(sys, ref, model, p, options, n_guess,
                                        &profile[i], NULL, error, error_size))
            return 0;
        if (i == 0) profile[0].eps = 0.0; /* 燃烧室截面没有面积比可言 */
    }
    return 1;
}
