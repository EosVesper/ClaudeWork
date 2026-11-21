/* 根据已求得的组成计算燃烧产物的补充热力性质和气相输运性质。
 * 输入：候选产物表、摩尔数（mol/kg 推进剂）、温度、压强及 trans.inp 路径。
 * 输出：凝相质量分数、平均摩尔质量、冻结/平衡比热及可用时的黏度、导热系数、Pr。
 * 本模块不改变燃烧室或喷管的平衡组成和流动求解结果。 */
#ifndef PRODUCT_PROPERTIES_H
#define PRODUCT_PROPERTIES_H

#include <stddef.h>
#include "mass_balance.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int valid;
    double Y_c;                /* 凝相质量分数，kg/kg */
    double M_gas;              /* 气相平均摩尔质量，kg/mol */
    double R_gas;              /* 每 kg 气相的气体常数，J/(kg·K) */
    double R_m;                /* 每 kg 总推进剂的等价气体常数，J/(kg·K) */
    double cp_frozen;          /* 冻结组成的定压比热，J/(kg·K) */
    double cv_frozen;          /* 冻结组成的定容比热，J/(kg·K) */
    double gamma_frozen;
    int equilibrium_valid;     /* 相集合固定时的平衡导数是否可求 */
    double cp_equilibrium;     /* 平衡定压比热，J/(kg·K) */
    double cv_equilibrium;     /* 平衡定容比热，J/(kg·K) */
    double gamma_equilibrium;
    int transport_valid;
    double mu;                /* 气相混合黏度，Pa·s */
    double conductivity;      /* 气相混合导热系数，W/(m·K) */
    double Pr;                /* 采用气相比热的普朗特数 */
    double transport_coverage;/* 数据覆盖的气相摩尔分数 */
    char equilibrium_message[256];
    char transport_message[256];
} ProductProperties;

/* 功能：用当前已加载的 NASA thermo.inp 和指定 trans.inp 计算物性。
 * 输入：balance、n、T(K)、p(Pa)、trans_path（NULL 时当前目录 trans.inp）。
 * 输出：基本热力性质成功返回 1，out->valid=1；平衡导数与输运性质各有独立有效位。
 *       基本物性失败返回 0，并写入 error。 */
int product_properties_evaluate(const MassBalance *balance, const double *n,
                                double T, double p, const char *trans_path,
                                ProductProperties *out, char *error,
                                size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
