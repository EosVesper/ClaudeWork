/* 文件功能：依据 NASA、CoolProp、AFRPL 与 Aerojet 数据计算液体推进剂入口焓。
 * 输入：液体种类、温度 K、压强 Pa 和质量分数。
 * 输出：摩尔焓、混合比焓及模型警告标志。 */ 
#ifndef INLET_ENTHALPY_H
#define INLET_ENTHALPY_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INLET_REFERENCE_ONLY = 0,
    INLET_NASA = 1,
    INLET_MMH_LIQUID = 2,
    INLET_N2O4_LIQUID = 3,
    INLET_O2_LIQUID = 4,
    INLET_H2_LIQUID = 5,
    INLET_CH4_LIQUID = 6,
    INLET_RP1_LIQUID = 7,
    INLET_H2O2_LIQUID = 8,
    INLET_N2H4_LIQUID = 9,
    INLET_CH3OH_LIQUID = 10,
    INLET_C2H5OH_LIQUID = 11,
    INLET_UDMH_LIQUID = 12
} InletEnthalpyModel;

#define INLET_MMH_T_MIN 220.79
#define INLET_MMH_T_MAX 397.15
#define INLET_N2O4_T_MIN 265.25
#define INLET_N2O4_T_MAX 381.25
#define INLET_N2O4_EXPERIMENT_T_MAX 294.35
#define INLET_P0 101325.0
#define INLET_RP1_T_MIN 250.0
#define INLET_RP1_T_MAX 450.0
#define INLET_H2O2_T_MIN 272.74
#define INLET_H2O2_T_MAX 423.35
#define INLET_N2H4_T_MIN 275.15
#define INLET_N2H4_T_MAX 386.40
#define INLET_CH3OH_T_MIN 176.0
#define INLET_CH3OH_T_MAX 337.8
#define INLET_C2H5OH_T_MIN 160.0
#define INLET_C2H5OH_T_MAX 351.5
#define INLET_UDMH_T_MIN 273.15
#define INLET_UDMH_T_MAX 344.15

enum {
    INLET_WARN_CP_EXTRAPOLATED = 1u,
    INLET_WARN_ENTHALPY_EXTRAPOLATED = 2u,
    INLET_WARN_PRESSURE_IGNORED = 4u,
    INLET_WARN_PHASE_UNCHECKED = 8u,
    INLET_INFO_PARAHYDROGEN = 16u,
    INLET_INFO_SATURATED_LIQUID = 32u
};

typedef struct {
    double H_m;
    double Cp_m;
    unsigned warnings;
} InletEnthalpyProperties;

typedef struct {
    const char *name;
    double mass_fraction;
    double molar_mass;
    double T;
    InletEnthalpyModel model;
    double T_ref;
    double H_m_ref;
    const char *nasa_name;
    double p;

} InletComponent;

/* 功能：按标准参考压强构造受支持的液体入口组元。
 * 输入：model、T、mass_fraction、out、error、error_size。
 * 输出：成功返回 1 并写入 InletComponent。 */
int inlet_liquid_component(InletEnthalpyModel model, double T, double mass_fraction,
                           InletComponent *out, char *error, size_t error_size);

/* 功能：按指定绝对压强构造受支持的液体入口组元。
 * 输入：model、T、p、mass_fraction、out、error、error_size。
 * 输出：成功返回 1 并写入 InletComponent。 */
int inlet_liquid_component_at_pressure(InletEnthalpyModel model, double T,
                                       double p, double mass_fraction,
                                       InletComponent *out, char *error, size_t error_size);

/* 功能：计算单个入口组元在所给状态下的焓和热容。
 * 输入：component、out、error、error_size。
 * 输出：成功返回 1 并写入物性和警告标志。 */
int inlet_component_properties(const InletComponent *component,
                               InletEnthalpyProperties *out,
                               char *error, size_t error_size);

/* 功能：按各组元质量分数求入口混合比焓并收集模型标志。
 * 输入：components、count、H_in、warnings、error、error_size。
 * 输出：成功返回 1 并写入 J/kg 和警告标志。 */
int inlet_mixture_enthalpy_ex(const InletComponent *components, size_t count,
                             double *H_in, unsigned *warnings,
                             char *error, size_t error_size);

/* 功能：计算单个入口组元的摩尔焓。
 * 输入：component、H_m、error、error_size。
 * 输出：成功返回 1 并写入 J/mol。 */
int inlet_component_enthalpy(const InletComponent *component, double *H_m,
                            char *error, size_t error_size);

/* 功能：按各组元质量分数求入口混合比焓。
 * 输入：components、count、H_in、error、error_size。
 * 输出：成功返回 1 并写入 J/kg。 */
int inlet_mixture_enthalpy(const InletComponent *components, size_t count,
                          double *H_in, char *error, size_t error_size);
#ifdef __cplusplus
}
#endif
#endif
