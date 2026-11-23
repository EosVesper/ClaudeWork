/* 文件功能：喷管热力计算模块的对外总接口，一次调用完成喉部、出口和性能计算。
 * 输入：燃烧室出口状态、膨胀模型、截面给定方式和环境压强。
 * 输出：喉部与出口截面、理想性能参数、出口组分及自检量。 */
#ifndef NOZZLE_HIGH_LEVEL_H
#define NOZZLE_HIGH_LEVEL_H

#include "nozzle_flow.h"
#include "nozzle_performance.h"
#include "product_properties.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 喷管计算输入。除 chamber 之外的字段都可以用 nozzle_default_input 的默认值。 */
typedef struct {
    const ChamberOutlet *chamber; /* 必需：燃烧室出口状态 */
    const MassBalance *balance;   /* 可选：已建好的候选产物表；NULL 时本模块自行重建 */
    const char *thermo_path;      /* 可选：NASA 数据库路径；NULL 表示当前目录 thermo.inp */
    const char *trans_path;       /* 可选：输运数据库路径；NULL 表示当前目录 trans.inp */
    NozzleModel model;            /* 膨胀过程模型 */
    NozzleThroatMode throat;      /* 喉部判据 */
    NozzleSectionSpec spec;       /* 出口截面给定方式 */
    double value;                 /* 与 spec 对应的数值 */
    double p_a;                   /* 环境压强，Pa；0 表示真空 */
    double m_dot;                 /* 总质量流量，kg/s；0 表示采用 chamber->m_dot_total */
    int n_profile;                /* 沿程剖面截面数；0 表示不算剖面 */
    double T_min;                 /* 候选产物表温度窗下限，K；<=0 取 200 */
    double T_max;                 /* 候选产物表温度窗上限，K；<=0 取 max(T_c, 6000) */
    double T_search_min;          /* 等熵方程温度搜索下界，K；<=0 取 T_min */
} NozzleInput;

/* 喷管计算输出。 */
typedef struct {
    int valid;               /* 1 表示结果可用 */
    NozzleStatus status;     /* 求解状态码 */
    NozzleModel model;
    NozzleThroatMode throat_mode;
    NozzleSectionSpec spec;
    double spec_value;
    /* 燃烧室参考量（来自 ChamberOutlet） */
    double p_c, T_c, h_c, s_c;
    double gamma_c, R_m_c, Mw_c, n_g_c;
    int chamber_temperature_evaluations;
    int chamber_brent_iterations;
    int chamber_equilibrium_iterations;
    int chamber_phase_iterations;
    /* 截面 */
    NozzleSection throat;       /* 喉部 */
    NozzleSection exit_section; /* 出口 */
    NozzlePerformance perf;     /* 理想性能参数 */
    /* 出口组分 */
    size_t N, L;
    double n_exit[BALANCE_MAX_PRODUCTS];
    /* 用实际燃烧室/出口组成计算的补充物性；不参与喷管流动求解。 */
    ProductProperties chamber_products;
    ProductProperties exit_products;
    char product_warning[256];
    /* 自检量 */
    double check_h;          /* 用重建表复算的 h 与燃烧室 h_c 之差，J/kg */
    double check_s;          /* 复算的 s 与 s_c 之差，J/(kg·K) */
    double check_mass;       /* 复算的质量闭合，kg/kg，应接近 1 */
    double check_n_deviation;/* 在 (p_c,T_c) 重解平衡与燃烧室组成的最大偏差，mol/kg */
    /* 沿程剖面与候选产物表 */
    int n_profile;
    NozzleSection *profile;      /* 由 nozzle_solve 分配，nozzle_result_free 释放 */
    const MassBalance *balance;  /* 实际使用的候选产物表（借用自 input 或自有） */
    MassBalance *owned_balance;  /* 本模块重建的表，由 nozzle_result_free 释放 */
    /* 数据库规范化统计 */
    size_t db_records;        /* 原始记录条数 */
    size_t db_species;        /* 合并后的组分个数 */
    size_t db_merged_groups;  /* 发生合并的组数 */
    size_t db_case_collisions;/* 仅大小写不同的名称组数 */
    char db_path[512];        /* 实际加载的规范化数据库路径 */
    char warning[512];           /* 非空表示结果可用但有需要注意的地方 */
    char error[512];             /* 非空表示失败原因 */
} NozzleResult;

/* 功能：生成默认输入：平衡膨胀、声速喉部、面积比 40、环境压强 101325 Pa。
 * 输入：无。
 * 输出：返回填好的 NozzleInput，chamber 仍为 NULL。 */
NozzleInput nozzle_default_input(void);

/* 功能：完成喷管热力计算。
 * 输入：input。
 * 输出：成功返回 1，result->valid=1；失败返回 0，result->error 给出原因。
 *       无论成功失败，都应在使用完结果后调用 nozzle_result_free。 */
int nozzle_solve(const NozzleInput *input, NozzleResult *result);

/* 功能：释放结果中动态分配的部分（剖面数组与重建的候选产物表）。
 * 输入：result（可为 NULL）。
 * 输出：无。可重复调用。 */
void nozzle_result_free(NozzleResult *result);

#ifdef __cplusplus
}
#endif

#endif
