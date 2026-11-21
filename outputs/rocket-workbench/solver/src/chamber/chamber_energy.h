/* 文件功能：在给定绝对压强及目标焓下求燃烧室平衡温度。
 * 输入：元素守恒模型、压强 Pa、入口目标焓 J/kg 和求解选项。
 * 输出：温度 K、产物焓熵、平衡组成及求解状态。 */ 
#ifndef CHAMBER_ENERGY_H
#define CHAMBER_ENERGY_H
#include "equilibrium_solver.h"
#include "brent.h"
#include "mixture_properties.h"
typedef enum { ENERGY_OK=0, ENERGY_INVALID, ENERGY_INNER_FAILURE,
               ENERGY_NO_BRACKET, ENERGY_ROOT_FAILURE, ENERGY_MEMORY } EnergyStatus;
typedef enum { ENERGY_TRACE_SCAN, ENERGY_TRACE_BRENT, ENERGY_TRACE_FINAL } EnergyTracePhase;
typedef void (*EnergyTrace)(void *context, EnergyTracePhase phase,
                            int evaluation, double temperature, double residual);
typedef struct {
    double T_min,T_max,T_tolerance,H_tolerance;
    int scan_steps,max_iterations;
    EquilibriumOptions equilibrium;
    EnergyTrace trace;
    void *trace_context;
} EnergyOptions;
typedef struct {
    EnergyStatus status;
    double T,p,H_in,H_product,residual;
    double S_product;
    int evaluations;
    RootStatus root_status;
    RootResult root;
    EquilibriumResult equilibrium;
} EnergyResult;

/* 功能：生成指定温度区间的能量求解默认选项。
 * 输入：T_min、T_max。
 * 输出：返回 EnergyOptions。 */
EnergyOptions energy_default_options(double T_min,double T_max);

/* 功能：联立化学平衡和能量守恒求燃烧室温度；p 用 Pa，H_in 用 J/kg。
 * 输入：b、p、H_in、options、out、error、error_size。
 * 输出：返回 EnergyStatus，并写入 EnergyResult 或错误信息。 */
EnergyStatus chamber_solve_hp(const MassBalance *b,double p,double H_in,
    const EnergyOptions *options,EnergyResult *out,char *error,size_t error_size);
#endif
