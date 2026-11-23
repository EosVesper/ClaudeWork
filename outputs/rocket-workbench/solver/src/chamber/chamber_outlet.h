/* 文件功能：将燃烧室求解结果整理为可供后续模块调用的完整出口状态。
 * 输入：守恒模型、能量求解结果或目标焓及可选总质量流量。
 * 输出：出口状态、全部候选物种及其编号、相态和摩尔质量。 */
#ifndef CHAMBER_OUTLET_H
#define CHAMBER_OUTLET_H

#include "chamber_energy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CHAMBER_PHASE_GAS = 0,
    CHAMBER_PHASE_CONDENSED = 1
} ChamberPhase;

typedef struct {
    size_t j;
    char name[NASA_NAME_LENGTH + 1];
    ChamberPhase phase;
    double molar_mass;
    double n;
    double A_k[CHEM_MAX_ELEMENTS];
} ChamberOutletSpecies;

typedef struct {
    int valid;
    EnergyStatus energy_status;
    EquilibriumStatus equilibrium_status;
    double p_c;
    double T_c;
    double h_c;
    double s_c;
    double p_0;
    double n_g;
    double m_dot_total;
    double energy_residual;
    double mass_closure;
    int temperature_evaluations; /* 全部温度试算次数，含搜区间、Brent 和最终复算 */
    int brent_iterations;        /* 温度根求解的 Brent 迭代次数 */
    int equilibrium_iterations;  /* 最终温度处平衡组成的迭代次数 */
    int phase_iterations;        /* 最终温度处活动凝相集合调整轮数 */
    size_t M, N, L;
    ChemicalElement elements[CHEM_MAX_ELEMENTS];
    double N_k[CHEM_MAX_ELEMENTS];
    ChamberOutletSpecies species[BALANCE_MAX_PRODUCTS];
} ChamberOutlet;

/* 功能：把已收敛的能量结果与完整物种元数据复制为独立的出口状态；m_dot_total 单位 kg/s，0 表示未知。
 * 输入：balance、energy、m_dot_total、out、error、error_size。
 * 输出：成功返回 1，out 包含全部候选物种且 valid=1；失败返回 0、valid=0。 */
int chamber_outlet_from_energy(const MassBalance *balance,
                               const EnergyResult *energy,
                               double m_dot_total,
                               ChamberOutlet *out,
                               char *error, size_t error_size);

/* 功能：由守恒模型、绝对压强 p(Pa) 和目标焓 H_in(J/kg) 求完整出口；m_dot_total 为 kg/s，options 为空时搜温 1500～5000 K。
 * 输入：balance、p、H_in、m_dot_total、options、out、error、error_size。
 * 输出：成功返回 1，out 包含 p_c、T_c、h_c、s_c 及全部 n_j；失败返回 0、valid=0。 */
int chamber_outlet_solve_hp(const MassBalance *balance, double p, double H_in,
                            double m_dot_total, const EnergyOptions *options,
                            ChamberOutlet *out, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
