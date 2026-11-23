/* 文件功能：依据 NASA、CoolProp、AFRPL 与 Aerojet 数据计算液体推进剂入口焓。
 * 输入：液体种类、温度 K、压强 Pa 和质量分数。
 * 输出：摩尔焓、混合比焓及模型警告标志。 */ 
#include "inlet_enthalpy.h"
#include "thermo_properties.h"
#include "inlet_cryogenic.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* 功能：把入口焓错误写入缓冲区。
 * 输入：error、size、message。
 * 输出：返回失败值 0。 */
static int fail(char *error, size_t size, const char *message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

/* 功能：判断入口模型是否属于已支持的液体模型。
 * 输入：model。
 * 输出：返回布尔判断值。 */
static int liquid_model(InletEnthalpyModel model)
{
    return model >= INLET_MMH_LIQUID && model <= INLET_UDMH_LIQUID;
}

/* 功能：取得入口液体模型的摩尔质量。
 * 输入：model。
 * 输出：返回 kg/mol。 */
static double liquid_mass(InletEnthalpyModel model)
{
    static const double values[] = {0.04607174,0.092011,0.0319988,0.00201588,0.01604246,0.013976183,
                                    0.03401468,0.03204516,0.03204186,0.04606844,0.06009832};
    return values[(int)model-(int)INLET_MMH_LIQUID];
}

/* 功能：取得入口液体模型的参考摩尔焓。
 * 输入：model。 
 * 输出：返回 J/mol。 */
static double liquid_reference(InletEnthalpyModel model)
{
    static const double values[] = {54200,-17549,-12979,-9012,-89233,-24717.7,
                                    -187780,50380,-238910,-277510,48900};
    return values[(int)model-(int)INLET_MMH_LIQUID];
}

/* 功能：对 RP-1 工程比热表插值并积分。
 * 输入：T、cp。
 * 输出：返回比焓差，同时写入比热。 */
static double rp1_integral(double T, double *cp)
{
    static const double F[] = {-50,0,100,200,300,400};
    static const double C[] = {0.415,0.440,0.490,0.540,0.600,0.650};
    double sum = 0;
    size_t i;
    for (i=0; i+1<sizeof(F)/sizeof(F[0]); ++i) {
        double low=(F[i]-32)*5/9+273.15;
        double high=(F[i+1]-32)*5/9+273.15;
        double width=fmin(T,high)-low;
        double slope=(C[i+1]-C[i])/(high-low);
        if (width<=0) break;
        sum += 4186.8*width*(C[i]+slope*width/2);
        if (T<=high) {
            if (cp) *cp=4186.8*(C[i]+slope*width);
            break;
        }
    }
    return sum;
}

/* 功能：计算三次比热多项式在两个温度间的积分。
 * 输入：x、x0、a。
 * 输出：返回积分值。 */
static double cp_integral(double x, double x0, const double a[4])
{
    return (x-x0)*(a[0] + a[1]*(x+x0)/2.0
        + a[2]*(x*x+x*x0+x0*x0)/3.0
        + a[3]*(x+x0)*(x*x+x0*x0)/4.0);
}

/* 功能：计算单个入口组元在所给状态下的焓和热容。
 * 输入：c、out、error、size。
 * 输出：成功返回 1 并写入物性和警告标志。 */
int inlet_component_properties(const InletComponent *c, InletEnthalpyProperties *out,
                               char *error, size_t size)
{
    InletEnthalpyProperties r = {0, NAN, 0};
    if (!c || !out || !isfinite(c->T) || c->T <= 0)
        return fail(error, size, "Invalid inlet component or temperature (K).");
    if (c->model == INLET_REFERENCE_ONLY) {
        if (!isfinite(c->T_ref) || c->T_ref <= 0 || !isfinite(c->H_m_ref))
            return fail(error, size, "Invalid reference temperature or molar enthalpy.");
        if (fabs(c->T-c->T_ref) > 1e-8) {
            if (error && size)
                snprintf(error, size, "%s: enthalpy is known only at %.8g K; "
                         "T=%.8g K requires heat-capacity/enthalpy data for this phase.",
                         c->name ? c->name : "Component", c->T_ref, c->T);
            return 0;
        }
        r.H_m = c->H_m_ref;
    } else if (c->model == INLET_NASA) {
        ThermoResult nasa;
        if (!c->nasa_name || !c->nasa_name[0])
            return fail(error, size, "Missing NASA species/phase name.");
        nasa = thermo_h(c->nasa_name, c->T);
        if (!isfinite(nasa.H)) return fail(error, size, thermo_last_error());
        r.H_m = nasa.H;
        r.Cp_m = nasa.Cp;
    } else if (c->model >= INLET_O2_LIQUID && c->model <= INLET_CH4_LIQUID) {
        return inlet_cryogenic_properties(c, out, error, size);
    } else if (c->model >= INLET_H2O2_LIQUID && c->model <= INLET_C2H5OH_LIQUID) {
        static const double lows[]={INLET_H2O2_T_MIN,INLET_N2H4_T_MIN,
                                    INLET_CH3OH_T_MIN,INLET_C2H5OH_T_MIN};
        static const double highs[]={INLET_H2O2_T_MAX,INLET_N2H4_T_MAX,
                                     INLET_CH3OH_T_MAX,INLET_C2H5OH_T_MAX};
        static const char *names[]={"H2O2(L)","N2H4(L)","CH3OH(L)","C2H5OH(L)"};
        size_t index=(size_t)(c->model-INLET_H2O2_LIQUID);
        ThermoResult nasa;
        double M=liquid_mass(c->model);
        if (c->T<lows[index] || c->T>highs[index]) {
            if (error && size) snprintf(error,size,"%s: liquid inlet T must be in [%.5g, %.5g] K.",
                c->name?c->name:"NASA liquid",lows[index],highs[index]);
            return 0;
        }
        if (!isfinite(c->molar_mass) || fabs(c->molar_mass/M-1)>1e-10)
            return fail(error,size,"NASA liquid molar mass differs from its database value (kg/mol).");
        if (!c->nasa_name || strcmp(c->nasa_name,names[index]))
            return fail(error,size,"NASA liquid species/phase does not match the selected model.");
        nasa=thermo_h(c->nasa_name,c->T);
        if (!isfinite(nasa.H) || !isfinite(nasa.Cp)) return fail(error,size,thermo_last_error());
        r.H_m=nasa.H;
        r.Cp_m=nasa.Cp;
        r.warnings=INLET_WARN_PRESSURE_IGNORED | INLET_WARN_PHASE_UNCHECKED;
    } else if (c->model == INLET_RP1_LIQUID) {
        double cp = NAN, M=liquid_mass(c->model);
        if (c->T < INLET_RP1_T_MIN || c->T > INLET_RP1_T_MAX)
            return fail(error, size, "RP-1: require 250 <= T <= 450 K; no temperature extrapolation.");
        if (!isfinite(c->molar_mass) || fabs(c->molar_mass/M-1)>1e-10)
            return fail(error, size, "RP-1 must use the NASA C1H1.95 mass basis: 0.013976183 kg/mol.");
        r.H_m=liquid_reference(c->model)+M*(rp1_integral(c->T,&cp)-rp1_integral(298.15,NULL));
        r.Cp_m=M*cp;
        r.warnings=INLET_WARN_PRESSURE_IGNORED | INLET_WARN_PHASE_UNCHECKED;
    } else if (c->model == INLET_UDMH_LIQUID) {

        const double a[]={0.4071,8.838e-4,0,0};
        const double M=liquid_mass(c->model);
        if (c->T<INLET_UDMH_T_MIN || c->T>INLET_UDMH_T_MAX)
            return fail(error,size,"UDMH(L): require 273.15 <= T <= 344.15 K; no temperature extrapolation.");
        if (!isfinite(c->molar_mass) || fabs(c->molar_mass/M-1)>1e-10)
            return fail(error,size,"UDMH molar mass differs from its NASA reference (kg/mol).");
        if (!c->nasa_name || strcmp(c->nasa_name,"C2H8N2(L),UDMH"))
            return fail(error,size,"UDMH NASA species/phase must be C2H8N2(L),UDMH.");
        r.H_m=liquid_reference(c->model)+M*4184.0*cp_integral(c->T,298.15,a);
        r.Cp_m=M*4184.0*(a[0]+a[1]*c->T);
        r.warnings=INLET_WARN_PRESSURE_IGNORED | INLET_WARN_PHASE_UNCHECKED;
    } else if (c->model == INLET_MMH_LIQUID || c->model == INLET_N2O4_LIQUID) {

        const double mmh[] = {0.6528, -1.7284e-5, 5.9142e-7, 0};
        const double nto[] = {0.360, 8.82e-4, -1.03e-5, 1.77e-7};
        const int is_mmh = c->model == INLET_MMH_LIQUID;
        const double *a = is_mmh ? mmh : nto;
        const double low = is_mmh ? INLET_MMH_T_MIN : INLET_N2O4_T_MIN;
        const double high = is_mmh ? INLET_MMH_T_MAX : INLET_N2O4_T_MAX;
        const double M = liquid_mass(c->model);
        const double x = is_mmh ? c->T : c->T-273.15;
        const double x0 = is_mmh ? 298.15 : 25.0;
        if (c->T > high || c->T < low || (is_mmh && c->T == low)) {
            if (error && size)
                snprintf(error, size, "%s: T=%.10g K outside %c%.10g, %.10g] K; no extrapolation beyond this range.",
                         is_mmh ? "MMH(L)" : "N2O4(L)", c->T, is_mmh ? '(' : '[', low, high);
            return 0;
        }
        if (!isfinite(c->molar_mass) || fabs(c->molar_mass/M-1.0) > 1e-10)
            return fail(error, size, "Liquid molar mass must match its built-in NASA reference (kg/mol); use inlet_liquid_component().");
        r.H_m = liquid_reference(c->model) + M*4184.0*cp_integral(x, x0, a);
        r.Cp_m = M*4184.0*(a[0]+x*(a[1]+x*(a[2]+x*a[3])));
        r.warnings = INLET_WARN_PRESSURE_IGNORED | INLET_WARN_PHASE_UNCHECKED;
        if (!is_mmh) {
            if (c->T > INLET_N2O4_EXPERIMENT_T_MAX)
                r.warnings |= INLET_WARN_CP_EXTRAPOLATED;

            if (c->T != 298.15)
                r.warnings |= INLET_WARN_ENTHALPY_EXTRAPOLATED;
        }
    } else return fail(error, size, "Unknown inlet enthalpy model.");
    if (!isfinite(r.H_m)) return fail(error, size, "Non-finite inlet molar enthalpy.");
    *out = r;
    if (error && size) error[0] = '\0';
    return 1;
}

/* 功能：按标准参考压强构造受支持的液体入口组元。
 * 输入：model、T、q、out、error、size。
 * 输出：成功返回 1 并写入 InletComponent。 */
int inlet_liquid_component(InletEnthalpyModel model, double T, double q,
                           InletComponent *out, char *error, size_t size)
{
    return inlet_liquid_component_at_pressure(model,T,INLET_P0,q,out,error,size);
}

/* 功能：按指定绝对压强构造受支持的液体入口组元。
 * 输入：model、T、p、q、out、error、size。
 * 输出：成功返回 1 并写入 InletComponent。 */
int inlet_liquid_component_at_pressure(InletEnthalpyModel model, double T, double p,
                                       double q, InletComponent *out, char *error, size_t size)
{
    static const char *names[]={"CH6N2(L)","N2O4(L)","O2(L)","H2(L)","CH4(L)","RP-1",
                                "H2O2(L)","N2H4(L)","CH3OH(L)","C2H5OH(L)","C2H8N2(L),UDMH"};
    static const double Trefs[]={298.15,298.15,90.170,20.270,111.643,298.15,
                                 298.15,298.15,298.15,298.15,298.15};
    InletComponent c = {0};
    InletEnthalpyProperties properties;
    if (!out || !liquid_model(model) || !isfinite(q) || q < 0 || q > 1 || !isfinite(p) || p<=0)
        return fail(error, size, "Invalid liquid model, output, mass fraction [0,1] or absolute p (Pa).");
    c.model = model;
    c.name = names[(int)model-(int)INLET_MMH_LIQUID];
    c.mass_fraction = q;
    c.molar_mass = liquid_mass(model);
    c.T = T;
    c.p = p;
    c.T_ref = Trefs[(int)model-(int)INLET_MMH_LIQUID];
    c.H_m_ref = liquid_reference(model);
    c.nasa_name = c.name;
    if (!inlet_component_properties(&c, &properties, error, size)) return 0;
    *out = c;
    return 1;
}

/* 功能：计算单个入口组元的摩尔焓。
 * 输入：c、H_m、error、size。
 * 输出：成功返回 1 并写入 J/mol。 */
int inlet_component_enthalpy(const InletComponent *c, double *H_m,
                            char *error, size_t size)
{
    InletEnthalpyProperties r;
    if (!H_m) return fail(error, size, "Null inlet enthalpy output.");
    if (!inlet_component_properties(c, &r, error, size)) return 0;
    *H_m = r.H_m;
    return 1;
}

/* 功能：按各组元质量分数求入口混合比焓并收集模型标志。
 * 输入：c、count、H_in、warnings、error、size。
 * 输出：成功返回 1 并写入 J/kg 和警告标志。 */
int inlet_mixture_enthalpy_ex(const InletComponent *c, size_t count,
                             double *H_in, unsigned *warnings, char *error, size_t size)
{
    size_t i;
    unsigned flags = 0;
    double q_sum = 0, H = 0, compensation = 0;
    if (!c || !count || !H_in)
        return fail(error, size, "Empty inlet mixture or null output.");

    for (i = 0; i < count; ++i) {
        if (!isfinite(c[i].mass_fraction) || c[i].mass_fraction < 0 ||
            c[i].mass_fraction > 1)
            return fail(error, size, "Inlet mass fractions must be in [0, 1].");
        q_sum += c[i].mass_fraction;
    }
    if (!isfinite(q_sum) || fabs(q_sum-1) > 1e-10)
        return fail(error, size, "Inlet mass fractions must sum to 1.");
    for (i = 0; i < count; ++i) {
        InletEnthalpyProperties r;
        double term, adjusted, next;
        if (c[i].mass_fraction == 0) continue;
        if (!isfinite(c[i].molar_mass) || c[i].molar_mass <= 0)
            return fail(error, size, "Inlet molar mass must be positive (kg/mol).");
        if (!inlet_component_properties(&c[i], &r, error, size)) return 0;
        flags |= r.warnings;
        term = c[i].mass_fraction / c[i].molar_mass * r.H_m;
        if (!isfinite(term)) return fail(error, size, "Non-finite inlet enthalpy contribution.");
        adjusted = term-compensation;
        next = H+adjusted;
        compensation = (next-H)-adjusted;
        H = next;
    }
    if (!isfinite(H)) return fail(error, size, "Non-finite total inlet enthalpy.");
    *H_in = H;
    if (warnings) *warnings = flags;
    if (error && size) error[0] = '\0';
    return 1;
}

/* 功能：按各组元质量分数求入口混合比焓。
 * 输入：c、count、H_in、error、size。
 * 输出：成功返回 1 并写入 J/kg。 */
int inlet_mixture_enthalpy(const InletComponent *c, size_t count,
                          double *H_in, char *error, size_t size)
{
    return inlet_mixture_enthalpy_ex(c, count, H_in, NULL, error, size);
}
