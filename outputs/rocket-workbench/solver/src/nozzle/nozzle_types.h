/* 文件功能：喷管热力计算模块共用的数据类型、状态枚举与文字转换。
 * 输入：无（仅类型定义）。
 * 输出：供本模块各子程序及调用方使用的结构体。 */
#ifndef NOZZLE_TYPES_H
#define NOZZLE_TYPES_H

#include <stddef.h>

#include "chamber_outlet.h"

/* 沿程剖面允许的最大截面数，防止调用方传入不合理值。 */
#define NOZZLE_MAX_SECTIONS 20001

/* 膨胀过程模型，对应讲义 5.7 的三种假设及 5.7.3 的突然冻结。 */
typedef enum {
    NOZZLE_MODEL_EQUILIBRIUM = 0,        /* 化学平衡：每个截面重新求平衡组分 */
    NOZZLE_MODEL_FROZEN_COMPOSITION = 1, /* 组分冻结：组分固定，温度随熵重解 */
    NOZZLE_MODEL_FROZEN_ALL = 2,         /* 完全冻结：组分与热力性质都不变 */
    NOZZLE_MODEL_SUDDEN_FREEZE = 3       /* 突然冻结：喉部前平衡、喉部后冻结 */
} NozzleModel;

/* 截面位置的给定方式，对应讲义 5.7.1 的"计算截面方程"。 */
typedef enum {
    NOZZLE_BY_PRESSURE = 0,       /* 给定绝对压强 p，单位 Pa */
    NOZZLE_BY_PRESSURE_RATIO = 1, /* 给定压强比 p/p_c */
    NOZZLE_BY_AREA_RATIO = 2,     /* 给定面积比 A/A_t */
    NOZZLE_BY_MACH = 3            /* 给定马赫数 Ma */
} NozzleSectionSpec;

/* 喉部确定方式，用于比较讲义 8.2 的气动法与严格质量流率极大法。 */
typedef enum {
    NOZZLE_THROAT_SONIC = 0,    /* 声速条件 u = a，a 取平均等熵指数声速 */
    NOZZLE_THROAT_GAMMA = 1,    /* 讲义 8.2 的临界压强比公式 */
    NOZZLE_THROAT_MASSFLUX = 2  /* 质量流率 G(p) 取极大，最严格 */
} NozzleThroatMode;

/* 求解状态码。NOZZLE_OK 之外的取值都表示结果不可用。 */
typedef enum {
    NOZZLE_OK = 0,
    NOZZLE_INVALID_INPUT,      /* 调用方参数不合法 */
    NOZZLE_BALANCE_FAILURE,    /* 候选产物表建立失败 */
    NOZZLE_THERMO_FAILURE,     /* 数据库物性查询失败 */
    NOZZLE_EQUILIBRIUM_FAILURE,/* 平衡组分求解失败 */
    NOZZLE_NO_BRACKET,         /* 等熵方程在给定温度区间内无根 */
    NOZZLE_ROOT_FAILURE,       /* 求根未收敛 */
    NOZZLE_THROAT_FAILURE,     /* 喉部求解失败 */
    NOZZLE_MEMORY,             /* 内存不足 */
    NOZZLE_CHAMBER_INVALID,    /* 燃烧室出口状态不可用 */
    NOZZLE_COMPOSITION_MISMATCH/* 候选产物表与燃烧室不一致 */
} NozzleStatus;

/* 一个截面上的全部热力参数。单位：SI。 */
typedef struct {
    double p;   /* 绝对压强，Pa */
    double T;   /* 温度，K */
    double v;   /* 比容，m^3/kg 推进剂 */
    double rho; /* 密度，kg/m^3 */
    double u;   /* 流速，m/s */
    double a;   /* 模型一致的声速，m/s */
    double a_frozen; /* 冻结声速，m/s，仅作对照 */
    double Ma;  /* 马赫数 u/a */
    double h;   /* 比焓，J/kg 推进剂 */
    double s;   /* 比熵，J/(kg·K) 推进剂 */
    double cp;  /* 定压比热，J/(kg·K) */
    double cv;  /* 定容比热，J/(kg·K) */
    double gamma;    /* 比热比 cp/cv */
    double gamma_avg;/* 相对燃烧室的平均等熵指数 */
    double R_m; /* 等价气体常数，J/(kg·K) */
    double Mw_gas;   /* 气相平均摩尔质量，kg/mol */
    double Mw_equiv; /* 等价摩尔质量 Mw = R0/R_m，kg/mol */
    double n_g; /* 气相摩尔数，mol/kg 推进剂 */
    double Y_c; /* 凝相质量分数 */
    double G;   /* 质量流率密度 rho*u，kg/(m^2·s) */
    double eps; /* 面积比 A/A_t */
    double s_residual; /* 等熵方程残差 |s-s_c|，J/(kg·K) */
    int iterations;    /* 温度迭代次数 */
    int converged;     /* 1 表示等熵方程已收敛 */
} NozzleSection;

/* 截面位置请求。按 spec 读取 value 的不同含义。 */
typedef struct {
    NozzleSectionSpec spec;
    double value;
} NozzleSectionRequest;

/* 等熵膨胀的参考状态：燃烧室出口。本结构把 NozzleSection 所需的
 * 全部"上游信息"集中在一起，避免各子程序重复读取 ChamberOutlet。 */
typedef struct {
    double p_c;     /* 燃烧室出口压强，Pa */
    double T_c;     /* 燃烧室出口温度，K */
    double h_c;     /* 比焓，J/kg */
    double s_c;     /* 比熵，J/(kg·K) */
    double gamma_c; /* 燃烧室产物冻结比热比 */
    double RmT_c;   /* R_m,c*T_c，m^2/s^2 */
    double n_c[BALANCE_MAX_PRODUCTS];      /* 燃烧室组成，mol/kg */
    double n_frozen[BALANCE_MAX_PRODUCTS]; /* 冻结模型采用的组成，mol/kg */
    int has_frozen; /* n_frozen 是否已设定 */
    double p_t;     /* 喉部压强，Pa */
    double G_t;     /* 喉部质量流率密度，kg/(m^2·s) */
    double u_t;     /* 喉部流速，m/s */
    double rho_t;   /* 喉部密度，kg/m^3 */
    int has_throat; /* 喉部量是否已算出 */
} NozzleReference;

/* 功能：把求解状态转换为文字。
 * 输入：status。
 * 输出：返回状态名称字符串。 */
const char *nozzle_status_name(NozzleStatus status);

/* 功能：把膨胀模型转换为文字。
 * 输入：model。
 * 输出：返回模型名称字符串。 */
const char *nozzle_model_name(NozzleModel model);

/* 功能：把截面给定方式转换为文字。
 * 输入：spec。
 * 输出：返回名称字符串。 */
const char *nozzle_section_spec_name(NozzleSectionSpec spec);

/* 功能：把喉部确定方式转换为文字。
 * 输入：mode。
 * 输出：返回名称字符串。 */
const char *nozzle_throat_mode_name(NozzleThroatMode mode);

/* 功能：返回稳定的英文代号，供 JSON/CSV 等机器可读输出使用。
 * 输入：model、spec、throat。
 * 输出：返回代号字符串。 */
const char *nozzle_model_code(NozzleModel model);
const char *nozzle_section_spec_code(NozzleSectionSpec spec);
const char *nozzle_throat_mode_code(NozzleThroatMode mode);

#endif
