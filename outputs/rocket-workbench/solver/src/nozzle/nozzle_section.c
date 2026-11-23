/* 文件功能：在给定截面位置上求解等熵方程，得到温度、组分和全部热力参数。
 * 输入：候选产物表、燃烧室参考状态、膨胀模型和截面位置。
 * 输出：截面参数 NozzleSection 与该截面的组成。 */
#include "nozzle_section.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 一次截面求解的工作区：把反复求值需要的东西集中在一起。 */
typedef struct {
    const NozzleSystem *sys;
    const NozzleReference *ref;
    NozzleModel model;
    const NozzleSectionOptions *opts;
    GibbsStandard *standard;              /* 堆上分配，跨求值复用 */
    double n_guess[BALANCE_MAX_PRODUCTS]; /* 平衡求解的热启动初值 */
    double s_target;                      /* 等熵方程的右端 s_c */
    int frozen;                           /* 本截面是否冻结组成 */
    const double *n_fixed;                /* 冻结组成的来源 */
    int evaluations;                      /* 累计求值次数 */
} SectionWork;

/* 功能：把状态错误写入缓冲区。
 * 输入：error、size、message。
 * 输出：返回失败值 0。 */
static int fail(char *error, size_t size, const char *message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

/* 功能：生成默认的等熵求解选项。
 * 输入：T_min、T_max（K）。
 * 输出：返回选项。 */
NozzleSectionOptions nozzle_section_default_options(double T_min, double T_max)
{
    NozzleSectionOptions options;
    options.T_min = T_min;
    options.T_max = T_max;
    options.s_tolerance = 1e-6;
    options.T_tolerance = 1e-7;
    options.max_iterations = 60;
    options.equilibrium = equilibrium_default_options();
    return options;
}

/* 功能：选择冻结模型采用的组成。
 * 输入：ref、model。
 * 输出：返回冻结组成指针，NULL 表示平衡模型。 */
static const double *frozen_composition(const NozzleReference *ref, NozzleModel model)
{
    if (model == NOZZLE_MODEL_FROZEN_COMPOSITION || model == NOZZLE_MODEL_FROZEN_ALL)
        return ref->n_c;      /* 讲义：组分冻结即保持燃烧室组分不变 */
    if (model == NOZZLE_MODEL_SUDDEN_FREEZE && ref->has_frozen)
        return ref->n_frozen; /* 突然冻结：保持喉部组分不变 */
    return NULL;
}

/* 功能：判断某截面是否采用冻结组成。
 * 输入：ref、model、p。
 * 输出：返回 1 表示冻结。 */
static int is_frozen(const NozzleReference *ref, NozzleModel model, double p)
{
    if (model == NOZZLE_MODEL_FROZEN_COMPOSITION || model == NOZZLE_MODEL_FROZEN_ALL)
        return 1;
    if (model == NOZZLE_MODEL_SUDDEN_FREEZE && ref->has_throat && p < ref->p_t)
        return ref->has_frozen;
    return 0;
}

/* 功能：在给定压强与温度下求组成和混合物性。
 * 输入：w、p、T。
 * 输出：成功返回 1 并写入 n、mix；失败返回 0 并写入 error。 */
static int evaluate(SectionWork *w, double p, double T, double *n,
                    NozzleMixture *mix, char *error, size_t error_size)
{
    const MassBalance *b = w->sys->balance;
    size_t j;
    char inner[256];
    if (!isfinite(T) || T <= 0)
        return fail(error, error_size, "温度不合法。");
    if (!gibbs_prepare(b, T, w->sys->p_0, w->standard, inner, sizeof(inner))) {
        if (error && error_size)
            snprintf(error, error_size, "标准态物性准备失败（T=%.3f K）：%s", T, inner);
        return 0;
    }
    if (w->frozen) {
        if (!w->n_fixed) return fail(error, error_size, "冻结组成尚未设定。");
        memcpy(n, w->n_fixed, w->sys->N * sizeof(*n));
    } else {
        EquilibriumResult eq;
        EquilibriumStatus status;
        /* 热启动：上一个解作为初值；把该温度下不可用的组分清零。 */
        for (j = 0; j < w->sys->N; ++j)
            if (!mass_balance_species_available(b, j, T)) w->n_guess[j] = 0.0;
        status = equilibrium_solve(b, w->standard, p, w->n_guess,
                                   &w->opts->equilibrium, &eq, inner, sizeof(inner));
        if (status != EQ_OK) {
            if (error && error_size)
                snprintf(error, error_size,
                         "平衡组分求解失败（p=%.6g Pa, T=%.3f K）：%s", p, T,
                         equilibrium_status_name(status));
            return 0;
        }
        memcpy(n, eq.n, w->sys->N * sizeof(*n));
        memcpy(w->n_guess, eq.n, w->sys->N * sizeof(*n));
    }
    ++w->evaluations;
    return nozzle_mixture_evaluate(w->sys, w->standard, p, n, mix, error, error_size);
}

/* 功能：按内插法（割线法）求等熵方程 s(T,p) = s_c 的温度根。
 * 输入：w、p。
 * 输出：成功返回 1，并写入温度、组成、混合物性、迭代次数与熵残差。 */
static int solve_temperature(SectionWork *w, double p, double *T_out, double *n_out,
                             NozzleMixture *mix_out, int *iterations,
                             double *s_residual, char *error, size_t error_size)
{
    const NozzleReference *ref = w->ref;
    const NozzleSectionOptions *o = w->opts;
    double T0, T_lo, T_hi, f_lo, f_hi, dT, T = 0.0, f = 0.0;
    double n[BALANCE_MAX_PRODUCTS];
    NozzleMixture mix;
    int k, used = 0, bracketed;
    char inner[256];

    /* 完全冻结：温度由等熵过程公式直接给出，组分与热力性质都不变。 */
    if (w->model == NOZZLE_MODEL_FROZEN_ALL) {
        double gamma = ref->gamma_c;
        T = ref->T_c * pow(p / ref->p_c, (gamma - 1.0) / gamma);
        if (!evaluate(w, p, T, n, &mix, error, error_size)) return 0;
        *T_out = T;
        if (n_out) memcpy(n_out, n, w->sys->N * sizeof(*n));
        if (mix_out) *mix_out = mix;
        if (iterations) *iterations = 0;
        if (s_residual) *s_residual = mix.s - ref->s_c;
        return 1;
    }

    /* 初值：按等熵过程公式估计，再限制在搜索窗内。 */
    T0 = ref->T_c * pow(p / ref->p_c, (ref->gamma_c - 1.0) / ref->gamma_c);
    if (!isfinite(T0) || T0 <= 0) T0 = 0.5 * (o->T_min + o->T_max);
    if (T0 < o->T_min) T0 = o->T_min;
    if (T0 > o->T_max) T0 = o->T_max;
    if (!evaluate(w, p, T0, n, &mix, error, error_size)) {
        T0 = 0.5 * (o->T_min + o->T_max);
        if (!evaluate(w, p, T0, n, &mix, error, error_size)) return 0;
    }
    f = mix.s - ref->s_c;
    if (fabs(f) <= o->s_tolerance) {
        *T_out = T0;
        if (n_out) memcpy(n_out, n, w->sys->N * sizeof(*n));
        if (mix_out) *mix_out = mix;
        if (iterations) *iterations = 0;
        if (s_residual) *s_residual = f;
        return 1;
    }

    /* 布点：熵随温度单调增，故按残差符号单向扩张，直到跨过零点。 */
    T_lo = T_hi = T0;
    f_lo = f_hi = f;
    dT = 0.06 * T0;
    bracketed = 0;
    for (k = 0; k < 40; ++k) {
        double trial = (f > 0) ? T0 - dT : T0 + dT;
        if (trial < o->T_min) trial = o->T_min;
        if (trial > o->T_max) trial = o->T_max;
        if (trial == T_lo || trial == T_hi) break;
        if (!evaluate(w, p, trial, n, &mix, inner, sizeof(inner))) {
            /* 该温度超出可用范围，结束布点并由下面的判据报错。 */
            break;
        }
        f = mix.s - ref->s_c;
        if (f > 0) { T_hi = trial; f_hi = f; } else { T_lo = trial; f_lo = f; }
        if (f_lo <= 0.0 && f_hi >= 0.0) { bracketed = 1; break; }
        dT *= 1.7;
    }
    if (!bracketed) {
        if (error && error_size)
            snprintf(error, error_size,
                     "等熵方程无根：p=%.6g Pa 时 s 在 [%.1f, %.1f] K 内未跨越 s_c=%.6f J/(kg·K)。%s",
                     p, T_lo, T_hi, ref->s_c, inner);
        return 0;
    }

    /* 内插法迭代：线性内插给出新温度，残差异号后收缩区间。 */
    for (k = 0; k < o->max_iterations; ++k) {
        double denom = f_hi - f_lo;
        T = (fabs(denom) > DBL_MIN) ? T_lo - f_lo * (T_hi - T_lo) / denom
                                    : 0.5 * (T_lo + T_hi);
        if (!(T > T_lo && T < T_hi)) T = 0.5 * (T_lo + T_hi);
        if (!evaluate(w, p, T, n, &mix, error, error_size)) return 0;
        f = mix.s - ref->s_c;
        ++used;
        if (fabs(f) <= o->s_tolerance) break;
        if (fabs(T_hi - T_lo) <= o->T_tolerance) break;
        if (f > 0) { T_hi = T; f_hi = f; } else { T_lo = T; f_lo = f; }
    }
    if (iterations) *iterations = used;
    if (s_residual) *s_residual = f;
    if (fabs(f) > 10.0 * o->s_tolerance) {
        if (error && error_size)
            snprintf(error, error_size,
                     "等熵方程未收敛：p=%.6g Pa, T=%.4f K, 残差=%.3e J/(kg·K)。",
                     p, T, f);
        return 0;
    }
    *T_out = T;
    if (n_out) memcpy(n_out, n, w->sys->N * sizeof(*n));
    if (mix_out) *mix_out = mix;
    return 1;
}

/* 功能：由已知 (p,T,n,mix) 组装截面参数。
 * 输入：w、p、mix、迭代次数与熵残差。
 * 输出：写入 section。 */
static void fill_section(SectionWork *w, double p, const NozzleMixture *mix,
                         int iterations, double s_residual, NozzleSection *section)
{
    const NozzleReference *ref = w->ref;
    double dh = ref->h_c - mix->h;
    double gamma_avg = nozzle_gamma_average(ref->p_c, ref->RmT_c, p,
                                            mix->R_m * mix->T, ref->gamma_c);
    memset(section, 0, sizeof(*section));
    if (dh < 0.0) dh = 0.0;
    section->p = p;
    section->T = mix->T;
    section->v = mix->v;
    section->rho = mix->rho;
    section->u = sqrt(2.0 * dh);
    section->h = mix->h;
    section->s = mix->s;
    section->cp = mix->cp;
    section->cv = mix->cv;
    section->gamma = mix->gamma;
    section->gamma_avg = gamma_avg;
    section->R_m = mix->R_m;
    section->Mw_gas = mix->Mw_gas;
    section->Mw_equiv = mix->Mw_equiv;
    section->n_g = mix->n_g;
    section->Y_c = mix->Y_c;
    section->a_frozen = mix->a_frozen;
    /* 平衡模型用平均等熵指数定义声速，冻结模型用冻结声速。 */
    section->a = w->frozen ? mix->a_frozen : sqrt(gamma_avg * p * mix->v);
    section->Ma = (section->a > 0.0) ? section->u / section->a : 0.0;
    section->G = mix->rho * section->u;
    section->s_residual = s_residual;
    section->iterations = iterations;
    section->converged = 1;
    section->eps = 0.0;
    if (ref->has_throat && ref->G_t > 0.0 && section->G > 0.0 && p <= ref->p_t)
        section->eps = ref->G_t / section->G;
}

/* 功能：为一次截面求解准备共享工作区。
 * 输入：sys、ref、model、options、p。
 * 输出：成功返回 1 并填好 w；失败返回 0。 */
static int setup_work(SectionWork *w, const NozzleSystem *sys,
                      const NozzleReference *ref, NozzleModel model,
                      const NozzleSectionOptions *options, double p,
                      char *error, size_t error_size)
{
    memset(w, 0, sizeof(*w));
    w->sys = sys;
    w->ref = ref;
    w->model = model;
    w->opts = options;
    w->s_target = ref->s_c;
    w->frozen = is_frozen(ref, model, p);
    if (w->frozen) {
        w->n_fixed = frozen_composition(ref, model);
        if (!w->n_fixed)
            return fail(error, error_size, "突然冻结模型需要先确定喉部组成。");
    }
    memcpy(w->n_guess, ref->n_c, sys->N * sizeof(*w->n_guess));
    w->standard = (GibbsStandard *)malloc(sizeof(*w->standard));
    if (!w->standard)
        return fail(error, error_size, "标准态物性结构体内存不足。");
    return 1;
}

/* 功能：在给定压强下求截面（内部实现）。
 * 输入：w、p、n_guess、n_out。
 * 输出：成功返回 1 并写入 out，同时把热启动初值回写给 n_guess。 */
static int at_pressure(SectionWork *w, double p, double *n_guess, NozzleSection *out,
                       double *n_out, char *error, size_t error_size)
{
    double T = 0.0, s_residual = 0.0;
    int iterations = 0;
    double n[BALANCE_MAX_PRODUCTS];
    NozzleMixture mix;
    if (!isfinite(p) || p <= 0)
        return fail(error, error_size, "截面压强必须为正。");
    if (n_guess) memcpy(w->n_guess, n_guess, w->sys->N * sizeof(*w->n_guess));
    if (!solve_temperature(w, p, &T, n, &mix, &iterations, &s_residual,
                           error, error_size))
        return 0;
    fill_section(w, p, &mix, iterations, s_residual, out);
    if (n_guess) memcpy(n_guess, w->n_guess, w->sys->N * sizeof(*n_guess));
    if (n_out) memcpy(n_out, n, w->sys->N * sizeof(*n_out));
    return 1;
}

/* 功能：在给定压强下求截面，供沿程推进反复调用。
 * 输入：sys、ref、model、p、options、n_guess、n_out。
 * 输出：成功返回 1 并写入 out。 */
int nozzle_section_at_pressure(const NozzleSystem *sys, const NozzleReference *ref,
                               NozzleModel model, double p,
                               const NozzleSectionOptions *options,
                               double *n_guess, NozzleSection *out, double *n_out,
                               char *error, size_t error_size)
{
    NozzleSectionOptions defaults;
    SectionWork w;
    int ok;
    if (!sys || !ref || !out)
        return fail(error, error_size, "截面求解输入为空。");
    if (!options) {
        defaults = nozzle_section_default_options(200.0, ref->T_c);
        options = &defaults;
    }
    if (!setup_work(&w, sys, ref, model, options, p, error, error_size)) return 0;
    ok = at_pressure(&w, p, n_guess, out, n_out, error, error_size);
    free(w.standard);
    return ok;
}

/* 功能：计算用于定压求根的探针值（面积比或马赫数）。
 * 输入：w、p、mode（0 面积比，1 马赫数）。
 * 输出：成功返回 1 并写入 value。 */
static int probe(SectionWork *w, double p, int mode, double *value,
                 char *error, size_t error_size)
{
    NozzleSection section;
    memset(&section, 0, sizeof(section));
    if (!at_pressure(w, p, NULL, &section, NULL, error, error_size)) return 0;
    *value = (mode == 0) ? section.eps : section.Ma;
    return 1;
}

/* 功能：理想气体等熵关系给出的压强比 p/p_c。
 * 输入：Ma、比热比 gamma。
 * 输出：返回压强比。 */
static double ideal_pressure_ratio(double Ma, double gamma)
{
    double factor = 1.0 + 0.5 * (gamma - 1.0) * Ma * Ma;
    return pow(factor, -gamma / (gamma - 1.0));
}

/* 功能：理想气体等熵关系给出的面积比 A/A_t。
 * 输入：Ma、比热比 gamma。
 * 输出：返回面积比。 */
static double ideal_area_ratio(double Ma, double gamma)
{
    double factor = 1.0 + 0.5 * (gamma - 1.0) * Ma * Ma;
    return (1.0 / Ma) * pow((2.0 / (gamma + 1.0)) * factor,
                            (gamma + 1.0) / (2.0 * (gamma - 1.0)));
}

/* 功能：由面积比反解马赫数（牛顿法，取超音速根）。
 * 输入：eps 面积比（须大于 1）、gamma。
 * 输出：返回马赫数。 */
static double mach_from_area_ratio(double eps, double gamma)
{
    double Ma = 1.0 + 0.7 * log(eps) + 0.1 * eps; /* 经验初值，超音速一侧 */
    int k;
    if (!(Ma > 1.01)) Ma = 1.01;
    for (k = 0; k < 80; ++k) {
        double f = ideal_area_ratio(Ma, gamma) - eps;
        double h = 1e-6 * Ma, d;
        double fp = (ideal_area_ratio(Ma + h, gamma) - ideal_area_ratio(Ma - h, gamma)) / (2.0 * h);
        if (!isfinite(fp) || fabs(fp) < 1e-14) break;
        d = f / fp;
        if (d > 0.5 * Ma) d = 0.5 * Ma;
        Ma -= d;
        if (Ma < 1.001) Ma = 1.001;
        if (fabs(d) < 1e-12 * Ma) break;
    }
    return Ma;
}

/* 功能：用二分法在压强上求解 value(p) = target，value 随压强单调递减。
 * 输入：w、mode（0 面积比，1 马赫数）、target。
 * 输出：成功返回 1 并写入 p_out；失败返回 0。 */
static int find_pressure(SectionWork *w, int mode, double target, double *p_out,
                         char *error, size_t error_size)
{
    const NozzleReference *ref = w->ref;
    double gamma = ref->gamma_c;
    double Ma_est, p_est, p_low, p_high, v, mid;
    int k, bracketed = 0;

    if (!isfinite(target) || target <= 0.0)
        return fail(error, error_size, "目标值必须为正。");
    if (!ref->has_throat)
        return fail(error, error_size, "按面积比或马赫数求解需要先确定喉部。");
    if (mode == 0 && target < 1.0)
        return fail(error, error_size, "面积比不得小于 1。");

    /* 用平均等熵指数的解析关系给出压强的初估，作为区间起点。 */
    if (mode == 0) {
        Ma_est = mach_from_area_ratio(target, gamma);
    } else {
        Ma_est = target;
    }
    p_est = (Ma_est > 0.0) ? ref->p_c * ideal_pressure_ratio(Ma_est, gamma) : ref->p_t;
    if (!isfinite(p_est) || p_est <= 0.0) p_est = ref->p_t;
    if (p_est > ref->p_c) p_est = ref->p_c;
    if (p_est < 1e-3) p_est = 1e-3;

    /* 以初估为中心成倍扩张，直到探针值跨过目标值（value 随 p 递减）。 */
    p_low = p_high = p_est;
    v = 0.0;
    for (k = 0; k < 60; ++k) {
        double factor = 1.0 + 0.35 * (double)(k / 2 + 1);
        p_low = p_est / factor;
        p_high = p_est * factor;
        if (p_high > ref->p_c) p_high = ref->p_c;
        if (p_low < 1e-3) p_low = 1e-3;
        if (!probe(w, p_high, mode, &v, error, error_size)) return 0;
        if (v > target) continue; /* 上界还不够低 */
        if (!probe(w, p_low, mode, &v, error, error_size)) {
            /* 下界温度过低：向喉部收缩，改用更小的扩张倍数重试。 */
            p_low = 0.5 * (p_low + p_high);
            if (!probe(w, p_low, mode, &v, error, error_size)) continue;
        }
        if (v >= target) { bracketed = 1; break; }
    }
    if (!bracketed) {
        if (error && error_size)
            snprintf(error, error_size,
                     "定压求根无区间：目标值 %.6g 在 p∈[%.4g, %.4g] Pa 内不可达。",
                     target, p_low, p_high);
        return 0;
    }

    for (k = 0; k < 120; ++k) {
        double f_mid;
        mid = sqrt(p_low * p_high); /* 压强跨数量级，取几何中点 */
        if (!probe(w, mid, mode, &v, error, error_size)) return 0;
        f_mid = v - target;
        if (fabs(f_mid) <= 1e-9 * fmax(1.0, target)) {
            *p_out = mid;
            return 1;
        }
        if (f_mid > 0.0) p_low = mid; else p_high = mid;
        if (fabs(p_high - p_low) <= 1e-11 * p_high) break;
    }
    *p_out = sqrt(p_low * p_high);
    return 1;
}

/* 功能：在指定截面求解等熵方程与截面方程。
 * 输入：sys、ref、model、request、options、n_out。
 * 输出：成功返回 1 并写入 out。 */
int nozzle_section_solve(const NozzleSystem *sys, const NozzleReference *ref,
                         NozzleModel model, const NozzleSectionRequest *request,
                         const NozzleSectionOptions *options,
                         NozzleSection *out, double *n_out,
                         char *error, size_t error_size)
{
    NozzleSectionOptions defaults;
    SectionWork w;
    double p = 0.0;
    int ok;
    if (!sys || !ref || !request || !out)
        return fail(error, error_size, "截面求解输入为空。");
    if (!options) {
        defaults = nozzle_section_default_options(200.0, ref->T_c);
        options = &defaults;
    }
    switch (request->spec) {
    case NOZZLE_BY_PRESSURE:
        p = request->value;
        break;
    case NOZZLE_BY_PRESSURE_RATIO:
        p = request->value * ref->p_c;
        break;
    case NOZZLE_BY_AREA_RATIO:
    case NOZZLE_BY_MACH:
        if (!setup_work(&w, sys, ref, model, options,
                        ref->has_throat ? ref->p_t * 0.5 : ref->p_c,
                        error, error_size))
            return 0;
        ok = find_pressure(&w, request->spec == NOZZLE_BY_AREA_RATIO ? 0 : 1,
                           request->value, &p, error, error_size);
        if (ok) {
            /* 沿用同一工作区的热启动初值与已加载物性。 */
            ok = at_pressure(&w, p, NULL, out, n_out, error, error_size);
        }
        free(w.standard);
        return ok;
    default:
        return fail(error, error_size, "未知的截面给定方式。");
    }
    if (!isfinite(p) || p <= 0.0)
        return fail(error, error_size, "求得的截面压强不合法。");
    return nozzle_section_at_pressure(sys, ref, model, p, options, NULL, out, n_out,
                                      error, error_size);
}
