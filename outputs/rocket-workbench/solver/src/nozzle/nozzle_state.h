/* 文件功能：由一个组成求解燃烧产物的热力性质：比焓、比熵、比热和声速。
 * 输入：候选产物表、标准态物性、绝对压强和摩尔数。
 * 输出：混合物性结构体 NozzleMixture。 */
#ifndef NOZZLE_STATE_H
#define NOZZLE_STATE_H

#include "nozzle_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 本模块的求解上下文：候选产物表与熵的参考压强。
 * p_0 必须与燃烧室一致（通常为 101325 Pa），否则熵会整体平移。 */
typedef struct {
    const MassBalance *balance;
    size_t N;   /* 候选产物总数 */
    size_t L;   /* 排在最前面的凝相数 */
    double p_0; /* 标准压强，Pa */
} NozzleSystem;

/* 一个组成下的混合物性。长度量与 ChamberOutlet 一致：以 1 kg 推进剂为基准。 */
typedef struct {
    double p;  /* 绝对压强，Pa */
    double T;  /* 温度，K */
    double v;  /* 比容，m^3/kg */
    double rho;/* 密度，kg/m^3 */
    double h;  /* 比焓，J/kg */
    double s;  /* 比熵，J/(kg·K) */
    double cp; /* 定压比热（冻结组成），J/(kg·K) */
    double cv; /* 定容比热，J/(kg·K) */
    double gamma;    /* 比热比 cp/cv */
    double R_m;      /* 等价气体常数 R0*n_g，J/(kg·K) */
    double Mw_gas;   /* 气相平均摩尔质量，kg/mol */
    double Mw_equiv; /* 等价摩尔质量 1/n_g，kg/mol */
    double n_g;      /* 气相摩尔数，mol/kg */
    double Y_c;      /* 凝相质量分数，kg/kg */
    double a_frozen; /* 冻结声速 sqrt(gamma*p*v)，m/s */
} NozzleMixture;

/* 功能：建立求解上下文。
 * 输入：balance 为已建立的候选产物表，p_0 为熵的参考压强 Pa。
 * 输出：成功返回 1；失败返回 0 并写入 error。 */
int nozzle_system_init(NozzleSystem *out, const MassBalance *balance, double p_0,
                       char *error, size_t error_size);

/* 功能：按给定组成、温度和压强求混合物性。
 * 输入：sys、在 std->T 下准备好的标准态物性 std、绝对压强 p、摩尔数 n。
 * 输出：成功返回 1 并写入 mix；失败返回 0 并写入 error。 */
int nozzle_mixture_evaluate(const NozzleSystem *sys, const GibbsStandard *std,
                            double p, const double *n, NozzleMixture *out,
                            char *error, size_t error_size);

/* 功能：按讲义式由两个状态的平均等熵指数求平均比热比。
 * 输入：p_c、RmT_c 为燃烧室压强与 R_m*T；p、RmT 为当前截面；
 *       gamma_c 为退化时返回的燃烧室比热比。
 * 输出：返回平均等熵指数，限定在 [1.02, 2.0] 之间。 */
double nozzle_gamma_average(double p_c, double RmT_c, double p, double RmT,
                            double gamma_c);

#ifdef __cplusplus
}
#endif

#endif
