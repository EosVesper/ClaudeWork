/* 文件功能：确定喷管喉部（临界截面）的压强、温度、流速与质量流率密度。
 * 输入：候选产物表、燃烧室参考状态、膨胀模型和喉部判据。
 * 输出：喉部截面 NozzleSection，并把喉部量写回参考状态供沿程计算使用。 */
#include "nozzle_throat.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* 功能：把状态错误写入缓冲区。
 * 输入：error、size、message。
 * 输出：返回失败值 0。 */
static int fail(char *error, size_t size, const char *message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

/* 功能：在给定压强下取截面，只保留探针需要的量。
 * 输入：sys、ref、model、options、p。
 * 输出：成功返回 1 并写入 section。 */
static int section_at(const NozzleSystem *sys, NozzleReference *ref,
                      NozzleModel model, const NozzleSectionOptions *options,
                      double p, NozzleSection *section, double *n_out,
                      char *error, size_t error_size)
{
    return nozzle_section_at_pressure(sys, ref, model, p, options, NULL, section,
                                      n_out, error, error_size);
}

/* 功能：用二分法求 u = a 的临界截面（马赫数等于 1）。
 * 输入：sys、ref、model、options、初估压强 p_guess。
 * 输出：成功返回 1 并写入 p_t。 */
static int sonic_throat(const NozzleSystem *sys, NozzleReference *ref,
                        NozzleModel model, const NozzleSectionOptions *options,
                        double p_guess, double *p_t, char *error, size_t error_size)
{
    NozzleSection section;
    double p_lo, p_hi, mid;
    int k;
    p_hi = p_guess;
    p_lo = p_guess;
    /* 先确认上界为亚音速、下界为超音速。 */
    for (k = 0; k < 40; ++k) {
        if (!section_at(sys, ref, model, options, p_hi, &section, NULL,
                        error, error_size))
            return 0;
        if (section.Ma < 1.0) break;
        p_hi *= 1.05;
        if (p_hi > ref->p_c) { p_hi = ref->p_c; break; }
    }
    if (k == 40) return fail(error, error_size, "未能找到亚音速一侧的压强。");
    for (k = 0; k < 40; ++k) {
        if (!section_at(sys, ref, model, options, p_lo, &section, NULL,
                        error, error_size))
            return 0;
        if (section.Ma > 1.0) break;
        p_lo *= 0.92;
        if (p_lo < 1e-3) break;
    }
    if (section.Ma <= 1.0)
        return fail(error, error_size, "未能找到超音速一侧的压强。");

    for (k = 0; k < 80; ++k) {
        mid = sqrt(p_lo * p_hi);
        if (!section_at(sys, ref, model, options, mid, &section, NULL,
                        error, error_size))
            return 0;
        if (fabs(section.Ma - 1.0) < 1e-9) {
            *p_t = mid;
            return 1;
        }
        if (section.Ma > 1.0) p_lo = mid; else p_hi = mid;
        if (fabs(p_hi - p_lo) < 1e-10 * p_hi) break;
    }
    *p_t = sqrt(p_lo * p_hi);
    return 1;
}

/* 功能：用黄金分割法求质量流率密度 G(p) 的极大点。
 * 输入：sys、ref、model、options、区间 [a, b]。
 * 输出：成功返回 1 并写入 p_t。 */
static int massflux_throat(const NozzleSystem *sys, NozzleReference *ref,
                           NozzleModel model, const NozzleSectionOptions *options,
                           double a, double b, double *p_t,
                           char *error, size_t error_size)
{
    NozzleSection section;
    double x1, x2, f1, f2;
    const double ratio = 0.6180339887498949;
    int k;
    /* 在对数坐标上做黄金分割，压强跨数量级时更均匀。 */
    x1 = exp(log(a) + (1.0 - ratio) * (log(b) - log(a)));
    x2 = exp(log(a) + ratio * (log(b) - log(a)));
    if (!section_at(sys, ref, model, options, x1, &section, NULL, error, error_size)) return 0;
    f1 = section.G;
    if (!section_at(sys, ref, model, options, x2, &section, NULL, error, error_size)) return 0;
    f2 = section.G;
    for (k = 0; k < 80; ++k) {
        if (f1 < f2) {
            a = x1;
            x1 = x2;
            f1 = f2;
            x2 = exp(log(a) + ratio * (log(b) - log(a)));
            if (!section_at(sys, ref, model, options, x2, &section, NULL, error, error_size)) return 0;
            f2 = section.G;
        } else {
            b = x2;
            x2 = x1;
            f2 = f1;
            x1 = exp(log(a) + (1.0 - ratio) * (log(b) - log(a)));
            if (!section_at(sys, ref, model, options, x1, &section, NULL, error, error_size)) return 0;
            f1 = section.G;
        }
        if (fabs(log(b) - log(a)) < 1e-12) break;
    }
    *p_t = sqrt(x1 * x2);
    return 1;
}

/* 功能：求解喉部。
 * 输入：sys、ref、model、mode、options、error、error_size。
 * 输出：成功返回 1 并写入 out，同时更新 ref 的喉部量。 */
int nozzle_throat_solve(const NozzleSystem *sys, NozzleReference *ref,
                        NozzleModel model, NozzleThroatMode mode,
                        const NozzleSectionOptions *options,
                        NozzleSection *out, char *error, size_t error_size)
{
    NozzleSectionOptions defaults;
    double gamma, p_guess, p_t = 0.0;
    double n_t[BALANCE_MAX_PRODUCTS];
    if (!sys || !ref || !out)
        return fail(error, error_size, "喉部求解输入为空。");
    if (!options) {
        defaults = nozzle_section_default_options(200.0, ref->T_c);
        options = &defaults;
    }
    gamma = ref->gamma_c;
    if (!isfinite(gamma) || gamma <= 1.0)
        return fail(error, error_size, "燃烧室比热比不合法。");

    /* 讲义 8.2 的临界压强比：只用到燃烧室参数。 */
    p_guess = ref->p_c * pow(2.0 / (gamma + 1.0), gamma / (gamma - 1.0));
    if (!isfinite(p_guess) || p_guess <= 0 || p_guess >= ref->p_c)
        p_guess = 0.55 * ref->p_c;

    switch (mode) {
    case NOZZLE_THROAT_GAMMA:
        p_t = p_guess;
        break;
    case NOZZLE_THROAT_SONIC:
        if (!sonic_throat(sys, ref, model, options, p_guess, &p_t, error, error_size))
            return 0;
        break;
    case NOZZLE_THROAT_MASSFLUX:
        if (!massflux_throat(sys, ref, model, options, 0.35 * ref->p_c,
                             0.8 * ref->p_c, &p_t, error, error_size))
            return 0;
        break;
    default:
        return fail(error, error_size, "未知的喉部判据。");
    }

    /* 喉部截面本身仍按等熵方程求温度，保证与沿程计算同一套方程。 */
    if (!section_at(sys, ref, model, options, p_t, out, n_t, error, error_size))
        return 0;

    ref->p_t = p_t;
    ref->G_t = out->G;
    ref->u_t = out->u;
    ref->rho_t = out->rho;
    ref->has_throat = 1;
    /* 突然冻结模型需要喉部组成作为冻结起点。 */
    memcpy(ref->n_frozen, n_t, sys->N * sizeof(*n_t));
    ref->has_frozen = 1;
    return 1;
}
