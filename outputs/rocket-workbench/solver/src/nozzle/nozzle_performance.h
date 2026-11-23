/* 文件功能：由燃烧室、喉部和出口截面计算推力室理想性能参数。
 * 输入：参考状态、喉部与出口截面、环境压强、总质量流量。
 * 输出：特征速度、推力系数、比冲、推力及两种方法的对照值。 */
#ifndef NOZZLE_PERFORMANCE_H
#define NOZZLE_PERFORMANCE_H

#include "nozzle_section.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* 基于燃烧室与喷管热力计算的方法（讲义 8.1） */
    double c_star;       /* 特征速度 p_c*A_t/m_dot，m/s */
    double u_e;          /* 出口排气速度 sqrt(2(h_c-h_e))，m/s */
    double I_sp;         /* 比冲（含环境压强），m/s */
    double I_sp_vac;     /* 真空比冲，m/s */
    double C_F;          /* 推力系数 */
    double C_F_vac;      /* 真空推力系数 */
    double F;            /* 推力，N（m_dot 未知时为 0） */
    double F_vac;        /* 真空推力，N */
    /* 基于平均等熵指数的气动计算方法（讲义 8.2），仅作对照 */
    double gamma_avg;    /* 全程平均等熵指数 */
    double gamma_func;   /* 气动函数 Γ */
    double c_star_gamma; /* 气动法特征速度，m/s */
    double u_e_gamma;    /* 气动法出口速度，m/s */
    double I_sp_vac_gamma; /* 气动法真空比冲，m/s */
    double C_F_vac_gamma;  /* 气动法真空推力系数 */
    double eps_gamma;      /* 气动法面积比 */
    /* 几何尺度（需要 m_dot；未知时为 0） */
    double A_t;  /* 喉部面积，m^2 */
    double A_e;  /* 出口面积，m^2 */
    double d_t;  /* 喉部直径，m */
    double d_e;  /* 出口直径，m */
    double eps;  /* 实际面积比 A_e/A_t */
    double m_dot;/* 总质量流量，kg/s */
    double p_a;  /* 环境压强，Pa */
} NozzlePerformance;

/* 功能：计算理想性能参数。
 * 输入：ref、throat、exit、p_a（Pa）、m_dot（kg/s，0 表示未知）。
 * 输出：成功返回 1 并写入 out；失败返回 0。 */
int nozzle_performance_solve(const NozzleReference *ref, const NozzleSection *throat,
                             const NozzleSection *exit, double p_a, double m_dot,
                             NozzlePerformance *out, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
