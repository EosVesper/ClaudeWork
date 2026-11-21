/* 文件功能：由一个组成求解燃烧产物的热力性质：比焓、比熵、比热和声速。
 * 输入：候选产物表、标准态物性、绝对压强和摩尔数。
 * 输出：混合物性结构体 NozzleMixture。 */
#include "nozzle_state.h"

#include <math.h>
#include <stdio.h>

/* 功能：把状态错误写入缓冲区。
 * 输入：error、size、message。
 * 输出：返回失败值 0。 */
static int fail(char *error, size_t size, const char *message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

/* 功能：建立求解上下文。
 * 输入：balance、p_0。
 * 输出：成功返回 1；失败返回 0 并写入 error。 */
int nozzle_system_init(NozzleSystem *out, const MassBalance *balance, double p_0,
                       char *error, size_t error_size)
{
    if (!out || !balance || balance->N == 0 ||
        balance->N > BALANCE_MAX_PRODUCTS || balance->L >= balance->N ||
        !isfinite(p_0) || p_0 <= 0)
        return fail(error, error_size, "候选产物表或熵参考压强不合法。");
    out->balance = balance;
    out->N = balance->N;
    out->L = balance->L;
    out->p_0 = p_0;
    if (error && error_size) error[0] = '\0';
    return 1;
}

/* 功能：按给定组成、温度和压强求混合物性。
 * 输入：sys、std、p、n。
 * 输出：成功返回 1 并写入 out；失败返回 0 并写入 error。 */
int nozzle_mixture_evaluate(const NozzleSystem *sys, const GibbsStandard *std,
                            double p, const double *n, NozzleMixture *out,
                            char *error, size_t error_size)
{
    NozzleMixture result = {0};
    size_t j;
    double cp = 0.0, n_g = 0.0, mass_c = 0.0, mass_gas, R_m, denom;
    if (!sys || !std || !n || !out || !isfinite(p) || p <= 0 ||
        sys->N == 0 || std->N != sys->N || std->L != sys->L ||
        !isfinite(std->T) || std->T <= 0)
        return fail(error, error_size, "混合物性输入不合法。");

    for (j = 0; j < sys->N; ++j) {
        if (!isfinite(n[j]) || n[j] < 0)
            return fail(error, error_size, "产物摩尔数不合法。");
        if (j >= sys->L) {
            n_g += n[j];
        } else if (n[j] > 0) {
            mass_c += n[j] * sys->balance->molar_mass[j];
        }
    }
    if (!isfinite(n_g) || n_g <= 0)
        return fail(error, error_size, "气相摩尔数必须为正。");
    if (!isfinite(mass_c) || mass_c < 0 || mass_c >= 1.0)
        return fail(error, error_size, "凝相质量分数越界。");

    /* 比焓与比熵复用燃烧室函数，保证同一定义、同一参考态。 */
    if (!mixture_enthalpy(std, n, &result.h, error, error_size)) return 0;
    if (!mixture_entropy(std, p, n, &result.s, error, error_size)) return 0;

    /* 定压比热按讲义：各组分摩尔定压比热乘摩尔数求和（冻结组成）。 */
    for (j = 0; j < sys->N; ++j) {
        ThermoResult property;
        if (n[j] <= 0) continue;
        property = thermo_h(sys->balance->names[j], std->T);
        if (!isfinite(property.Cp))
            return fail(error, error_size, "物性查询失败：组分温度超出数据库区间。");
        cp += n[j] * property.Cp;
    }

    /* 含凝相时 p*v = R_m*T 仍严格成立，R_m = R0*n_g 即"等价气体常数"。 */
    R_m = THERMO_R0 * n_g;
    denom = cp - R_m;
    if (!isfinite(cp) || !isfinite(R_m) || !(denom > 0.0))
        return fail(error, error_size, "比热或气体常数不合法。");

    result.p = p;
    result.T = std->T;
    result.n_g = n_g;
    result.Y_c = mass_c;
    result.R_m = R_m;
    result.cp = cp;
    result.cv = denom;
    result.gamma = cp / denom;
    result.Mw_equiv = 1.0 / n_g;
    mass_gas = 1.0 - mass_c;
    result.Mw_gas = mass_gas / n_g;
    result.v = R_m * std->T / p;
    result.rho = 1.0 / result.v;
    result.a_frozen = sqrt(result.gamma * p * result.v);
    if (!isfinite(result.v) || result.v <= 0 || !isfinite(result.a_frozen) ||
        result.gamma < 1.0 || result.gamma > 2.0)
        return fail(error, error_size, "混合物性自检失败（比容或比热比越界）。");
    *out = result;
    if (error && error_size) error[0] = '\0';
    return 1;
}

/* 功能：按两个状态的平均等熵指数求平均比热比。
 * 输入：p_c、RmT_c、p、RmT、gamma_c。
 * 输出：返回平均等熵指数。 */
double nozzle_gamma_average(double p_c, double RmT_c, double p, double RmT,
                            double gamma_c)
{
    double a, b, gamma;
    if (!isfinite(p_c) || !isfinite(p) || !isfinite(RmT_c) || !isfinite(RmT) ||
        p_c <= 0 || p <= 0 || RmT <= 0)
        return gamma_c;
    a = log(p_c / p);
    b = log(RmT_c / RmT);
    /* 靠近燃烧室时两个对数同时趋于零，直接返回燃烧室值以免 0/0。 */
    if (a < 1e-6 || !isfinite(a) || !isfinite(b)) return gamma_c;
    if (b >= a) return gamma_c;
    gamma = a / (a - b);
    if (!isfinite(gamma)) return gamma_c;
    if (gamma < 1.02) gamma = 1.02;
    if (gamma > 2.0) gamma = 2.0;
    return gamma;
}
