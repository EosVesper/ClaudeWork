/* 文件功能：把液体或预设固体推进剂的原始输入直接转换为燃烧室出口状态。
 * 输入：推进剂、入口温度、绝对压强、配比或配方及可选求解参数。
 * 输出：完整的 ChamberOutlet；失败时给出错误信息。 */
#ifndef CHAMBER_HIGH_LEVEL_H
#define CHAMBER_HIGH_LEVEL_H

#include "chamber_outlet.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CHAMBER_LIQUID_ADIABATIC = 1,
    CHAMBER_LIQUID_HEAT_IN_PER_KG_FUEL = 2,
    CHAMBER_LIQUID_HEAT_LOSS_PER_KG_PRODUCT = 3
} ChamberLiquidHeatCase;

typedef struct {
    const char *oxidizer;
    const char *fuel;
    double T_ox;
    double T_fuel;
    double m_dot_total;
    double mixture_ratio;
    double p;
    ChamberLiquidHeatCase heat_case;
    double heat;
    const char *thermo_path;
} LiquidChamberInput;

typedef enum {
    CHAMBER_SOLID_CP_DEFAULT = 0,
    CHAMBER_SOLID_CP_BINDER = 1,
    CHAMBER_SOLID_CP_EFFECTIVE = 2
} ChamberSolidCpMode;

typedef struct {
    const char *propellant;
    double T_in;
    double p;
    double m_dot_total;
    ChamberSolidCpMode cp_mode;
    double cp_value;
    const char *thermo_path;
} SolidChamberInput;

/* 功能：计算双组元液发的入口焓、候选产物、平衡组成和燃烧室出口状态。
 * 输入：input->oxidizer/fuel 为组元代号，T_ox/T_fuel 为入口温度 K，
 *       mixture_ratio 为氧燃质量比，p 为绝对压强 Pa，m_dot_total 为总流量 kg/s
 *       （0 表示未知）；heat_case 选传热情形，heat 为非负热量（情形 2 用
 *       J/kg 燃料，情形 3 用 J/kg 总产物）；thermo_path 为数据库路径，
 *       NULL 时读取当前工作目录的 thermo.inp。
 *       options 为搜温选项，NULL 时使用 1500～5000 K；
 *       out 指向可写的出口结构，error 指向错误文本数组，
 *       error_size 为该数组容量。
 * 输出：成功返回 1，out->valid=1；out 内含 p_c(Pa)、T_c(K)、
 *       h_c(J/kg)、s_c(J/(kg·K)) 和全部候选物种的 n_j(mol/kg) 等。
 *       失败返回 0、out->valid=0，并将原因写入 error。 */
int chamber_solve_liquid(const LiquidChamberInput *input,
                         const EnergyOptions *options, ChamberOutlet *out,
                         char *error, size_t error_size);

/* 功能：计算预设固体推进剂的入口焓、候选产物、平衡组成和燃烧室出口状态。
 * 输入：input->propellant 为 531、518 或 JPN；T_in 为入口温度 K，
 *       p 为绝对压强 Pa，m_dot_total 为总流量 kg/s（0 表示未知）；
 *       cp_mode 选默认、黏合剂或整剂等效比热模型，非默认时 cp_value
 *       为指定比热 J/(kg·K)；thermo_path 为数据库路径，NULL 时读取
 *       当前工作目录的 thermo.inp。options 为搜温选项，NULL 时使用
 *       1500～5000 K；out 指向可写的出口结构，error 指向错误文本数组，
 *       error_size 为该数组容量。
 * 输出：成功返回 1，out->valid=1；out 内含 p_c(Pa)、T_c(K)、
 *       h_c(J/kg)、s_c(J/(kg·K)) 和全部候选物种的 n_j(mol/kg) 等。
 *       失败返回 0、out->valid=0，并将原因写入 error。 */
int chamber_solve_solid(const SolidChamberInput *input,
                        const EnergyOptions *options, ChamberOutlet *out,
                        char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
