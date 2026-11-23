/* 文件功能：由燃烧室、喉部和出口截面计算推力室理想性能参数。
 * 输入：参考状态、喉部与出口截面、环境压强、总质量流量。
 * 输出：特征速度、推力系数、比冲、推力及两种方法的对照值。 */
#include "nozzle_performance.h"

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

/* 功能：讲义 8.2 的气动函数 Γ = sqrt(gamma)*(2/(gamma+1))^((gamma+1)/(2(gamma-1)))。
 * 输入：gamma。
 * 输出：返回 Γ。 */
static double gamma_function(double gamma)
{
    return sqrt(gamma) * pow(2.0 / (gamma + 1.0), (gamma + 1.0) / (2.0 * (gamma - 1.0)));
}

/* 功能：气动法由压强比求面积比。
 * 输入：gamma、pr = p_e/p_c。
 * 输出：返回 A_e/A_t。 */
static double area_ratio_from_ratio(double gamma, double pr)
{
    /* 先由压强比解出马赫数，再套用面积比公式，避免两套公式不一致。 */
    double factor = pow(pr, -(gamma - 1.0) / gamma); /* 1 + (γ-1)/2*Ma^2 */
    double Ma = sqrt(2.0 * (factor - 1.0) / (gamma - 1.0));
    if (!(Ma > 0.0)) return 0.0;
    return (1.0 / Ma) * pow((2.0 / (gamma + 1.0)) * factor,
                            (gamma + 1.0) / (2.0 * (gamma - 1.0)));
}

/* 功能：计算理想性能参数。
 * 输入：ref、throat、exit、p_a、m_dot。
 * 输出：成功返回 1 并写入 out。 */
int nozzle_performance_solve(const NozzleReference *ref, const NozzleSection *throat,
                             const NozzleSection *exit, double p_a, double m_dot,
                             NozzlePerformance *out, char *error, size_t error_size)
{
    NozzlePerformance result = {0};
    double gamma, c_star, u_e, v_e, I_sp, I_sp_vac, A_t = 0.0, A_e = 0.0;
    double gamma_avg, gamma_func, c_star_gamma, u_e_gamma, I_sp_vac_gamma;
    double pr, eps_gamma, C_F_vac_gamma;
    if (!ref || !throat || !exit || !out)
        return fail(error, error_size, "性能计算输入为空。");
    if (!isfinite(p_a) || p_a < 0.0)
        return fail(error, error_size, "环境压强不能为负。");
    if (!isfinite(m_dot) || m_dot < 0.0)
        return fail(error, error_size, "总质量流量不能为负。");
    if (!(throat->G > 0.0))
        return fail(error, error_size, "喉部质量流率密度必须为正。");
    if (!(exit->u >= 0.0))
        return fail(error, error_size, "出口流速为负，结果不可用。");

    gamma = ref->gamma_c;
    /* 8.1：特征速度直接由喉部质量流率密度给出，c* = p_c/(rho_t*u_t)。 */
    c_star = ref->p_c / throat->G;
    u_e = exit->u;
    v_e = exit->v;
    /* 出口与燃烧室重合（u_e=0）时没有膨胀，速度类参数按 0 处理。 */
    if (u_e > 0.0) {
        I_sp = u_e + (exit->p - p_a) * v_e / u_e;
        I_sp_vac = u_e + exit->p * v_e / u_e;
    } else {
        I_sp = 0.0;
        I_sp_vac = 0.0;
    }
    if (!isfinite(c_star) || c_star <= 0.0)
        return fail(error, error_size, "特征速度不合法。");

    result.c_star = c_star;
    result.u_e = u_e;
    result.I_sp = I_sp;
    result.I_sp_vac = I_sp_vac;
    result.C_F = (c_star > 0.0) ? I_sp / c_star : 0.0;
    result.C_F_vac = (c_star > 0.0) ? I_sp_vac / c_star : 0.0;
    result.p_a = p_a;
    result.m_dot = m_dot;

    /* 8.2：c* 用燃烧室比热比（特征速度只取决于燃烧室状态）；
     * 膨胀段用讲义 7.2 的平均等熵指数，即由两个端态反算出的那个 γ。 */
    gamma_avg = nozzle_gamma_average(ref->p_c, ref->RmT_c, exit->p,
                                     exit->R_m * exit->T, ref->gamma_c);
    gamma_func = gamma_function(gamma);
    c_star_gamma = sqrt(ref->RmT_c) / gamma_func;
    pr = exit->p / ref->p_c;
    u_e_gamma = sqrt(2.0 * gamma_avg / (gamma_avg - 1.0) * ref->RmT_c *
                     (1.0 - pow(pr, (gamma_avg - 1.0) / gamma_avg)));
    eps_gamma = area_ratio_from_ratio(gamma_avg, pr);
    I_sp_vac_gamma = u_e_gamma + exit->p * eps_gamma * c_star_gamma / ref->p_c;
    C_F_vac_gamma = I_sp_vac_gamma / c_star_gamma;

    result.gamma_avg = gamma_avg;
    result.gamma_func = gamma_func;
    result.c_star_gamma = c_star_gamma;
    result.u_e_gamma = u_e_gamma;
    result.I_sp_vac_gamma = I_sp_vac_gamma;
    result.C_F_vac_gamma = C_F_vac_gamma;
    result.eps_gamma = eps_gamma;

    /* 几何尺度：m_dot = p_c*A_t/c*。流量未知时只给比参数。 */
    result.eps = exit->eps;
    if (m_dot > 0.0) {
        A_t = m_dot * c_star / ref->p_c;
        A_e = result.eps * A_t;
        result.A_t = A_t;
        result.A_e = A_e;
        result.d_t = sqrt(4.0 * A_t / 3.14159265358979323846);
        result.d_e = sqrt(4.0 * A_e / 3.14159265358979323846);
        result.F = m_dot * I_sp;
        result.F_vac = m_dot * I_sp_vac;
    }
    if (!isfinite(result.C_F) || !isfinite(result.I_sp_vac))
        return fail(error, error_size, "性能参数出现非有限值。");
    *out = result;
    if (error && error_size) error[0] = '\0';
    return 1;
}
