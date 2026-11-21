/* 文件功能：将双组元液发原始条件转换为完整的燃烧室出口状态。
 * 输入：氧化剂和燃料、入口温度、氧燃比、质量流量、压强及传热情况。
 * 输出：经入口焓、元素守恒和 HP 平衡计算得到的 ChamberOutlet。 */
#include "chamber_high_level.h"
#include "inlet_enthalpy.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    InletEnthalpyModel model;
    double atoms[4];
    int oxidizer;
    int fuel;
} LiquidDefinition;

static const LiquidDefinition liquids[] = {
    {"LOX", INLET_O2_LIQUID, {0,0,2,0},1,0},
    {"N2O4",INLET_N2O4_LIQUID,{0,0,4,2},1,0},
    {"H2O2",INLET_H2O2_LIQUID,{0,2,2,0},1,0},
    {"LH2", INLET_H2_LIQUID, {0,2,0,0},0,1},
    {"LCH4",INLET_CH4_LIQUID,{1,4,0,0},0,1},
    {"MMH", INLET_MMH_LIQUID,{1,6,0,2},0,1},
    {"UDMH",INLET_UDMH_LIQUID,{2,8,0,2},0,1},
    {"RP1", INLET_RP1_LIQUID,{1,1.95,0,0},0,1},
    {"N2H4",INLET_N2H4_LIQUID,{0,4,0,2},0,1},
    {"CH3OH",INLET_CH3OH_LIQUID,{1,4,1,0},0,1},
    {"C2H5OH",INLET_C2H5OH_LIQUID,{2,6,1,0},0,1}
};

/* 功能：按名称查找已支持的液体推进剂。
 * 输入：name 为推进剂名称。
 * 输出：返回定义指针，未找到时返回空指针。 */
static const LiquidDefinition *find_liquid(const char *name)
{
    size_t i;
    if (!name) return NULL;
    for (i=0;i<sizeof(liquids)/sizeof(liquids[0]);++i)
        if (!strcmp(name,liquids[i].name)) return &liquids[i];
    return NULL;
}

/* 功能：标记高层液发输入错误。
 * 输入：out、error 缓冲区和错误文本。
 * 输出：返回 0，并将 out 标记为无效。 */
static int fail(ChamberOutlet *out, char *error, size_t size, const char *message)
{
    if (out) out->valid=0;
    if (error && size) snprintf(error,size,"%s",message);
    return 0;
}

/* 功能：由原始液发条件计算入口目标焓、候选物种和平衡出口状态。
 * 输入：input、可选能量求解选项及输出缓冲区。
 * 输出：成功返回 1 和完整出口状态，失败返回 0 和错误文本。 */
int chamber_solve_liquid(const LiquidChamberInput *input,
                         const EnergyOptions *options, ChamberOutlet *out,
                         char *error, size_t error_size)
{
    static const ChemicalElement elements[] = {
        {"C",12.011},{"H",1.008},{"O",15.999},{"N",14.0067}
    };
    const LiquidDefinition *ox,*fuel;
    const char *path;
    InletComponent components[2];
    PropellantFormula formula;
    EnergyOptions defaults;
    const EnergyOptions *used_options;
    MassBalance *balance;
    double flow[2],g[8],mass,H_in,H_target,total;
    unsigned warnings=0;
    size_t i,k;
    int ok=0,loaded=0;

    if (!out) return fail(NULL,error,error_size,"Null chamber outlet.");
    memset(out,0,sizeof(*out));
    out->energy_status=ENERGY_INVALID;
    out->equilibrium_status=EQ_INVALID_INPUT;
    if (error && error_size) error[0]='\0';
    if (!input) return fail(out,error,error_size,"Null liquid chamber input.");
    ox=find_liquid(input->oxidizer);
    fuel=find_liquid(input->fuel);
    if (!ox || !ox->oxidizer || !fuel || !fuel->fuel)
        return fail(out,error,error_size,"Unsupported liquid oxidizer or fuel.");
    if (!isfinite(input->T_ox) || !isfinite(input->T_fuel) ||
        !isfinite(input->m_dot_total) || input->m_dot_total<0 ||
        !isfinite(input->mixture_ratio) || input->mixture_ratio<=0 ||
        !isfinite(input->p) || input->p<=0 ||
        !isfinite(input->heat) || input->heat<0 ||
        (input->heat_case!=CHAMBER_LIQUID_ADIABATIC &&
         input->heat_case!=CHAMBER_LIQUID_HEAT_IN_PER_KG_FUEL &&
         input->heat_case!=CHAMBER_LIQUID_HEAT_LOSS_PER_KG_PRODUCT))
        return fail(out,error,error_size,"Invalid liquid chamber condition.");

    path=input->thermo_path ? input->thermo_path : "thermo.inp";
    defaults=energy_default_options(1500,5000);
    used_options=options ? options : &defaults;
    balance=(MassBalance *)calloc(1,sizeof(*balance));
    if (!balance) return fail(out,error,error_size,"Cannot allocate mass balance.");
    if (thermo_load(path)) {
        fail(out,error,error_size,thermo_last_error());
        goto done;
    }
    loaded=1;

    total=input->m_dot_total>0 ? input->m_dot_total : 1.0;
    flow[0]=total*input->mixture_ratio/(1+input->mixture_ratio);
    flow[1]=total/(1+input->mixture_ratio);
    if (!inlet_liquid_component_at_pressure(ox->model,input->T_ox,input->p,
                                             input->mixture_ratio/(1+input->mixture_ratio),
                                             &components[0],error,error_size) ||
        !inlet_liquid_component_at_pressure(fuel->model,input->T_fuel,input->p,
                                             1/(1+input->mixture_ratio),
                                             &components[1],error,error_size) ||
        !inlet_mixture_enthalpy_ex(components,2,&H_in,&warnings,error,error_size))
        goto done;
    H_target=H_in;
    if (input->heat_case==CHAMBER_LIQUID_HEAT_IN_PER_KG_FUEL)
        H_target+=input->heat/(1+input->mixture_ratio);
    if (input->heat_case==CHAMBER_LIQUID_HEAT_LOSS_PER_KG_PRODUCT)
        H_target-=input->heat;
    if (!isfinite(H_target)) {
        fail(out,error,error_size,"Non-finite target enthalpy.");
        goto done;
    }
    for (i=0;i<2;++i) {
        const LiquidDefinition *li=i ? fuel : ox;
        mass=0;
        for (k=0;k<4;++k) mass+=li->atoms[k]*elements[k].A_r;
        for (k=0;k<4;++k) g[4*i+k]=li->atoms[k]*elements[k].A_r/mass;
    }
    if (!propellant_from_mass_flows(elements,4,g,flow,2,&formula,
                                    error,error_size) ||
        !mass_balance_from_nasa_phases(path,&formula,
                                       used_options->T_min,used_options->T_max,1,
                                       balance,error,error_size))
        goto done;
    ok=chamber_outlet_solve_hp(balance,input->p,H_target,input->m_dot_total,
                               used_options,out,error,error_size);
done:
    if (loaded) thermo_free();
    free(balance);
    return ok;
}
