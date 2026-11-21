/* 文件功能：提供可供命令行和前端共用的液体发动机基准工况。
 * 输入：型号代号或表中索引。
 * 输出：推进剂、入口温度、主推力室流量、混合比、室压、面积比和环境压强。 */
#ifndef ENGINE_PRESETS_H
#define ENGINE_PRESETS_H

#include <stddef.h>
#include "chamber_high_level.h"
#include "nozzle_high_level.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *id;
    const char *name;
    const char *oxidizer;
    const char *fuel;
    double T_ox;           /* 计算用入口温度，K；可由调用方覆盖 */
    double T_fuel;         /* 计算用入口温度，K；可由调用方覆盖 */
    double m_dot_total;    /* 主推力室流量，kg/s；0 表示未指定 */
    double mixture_ratio;  /* 主推力室氧燃质量比 */
    double p_c;            /* 燃烧室绝对压强，Pa */
    double area_ratio;     /* 喷管出口与喉部面积比 */
    double p_a;            /* 基准环境压强，Pa */
    const char *source;    /* 参数来源，供前端展示和追溯 */
} EnginePreset;

/* 功能：返回预设的总数。
 * 输入：无。
 * 输出：预设数量。 */
size_t engine_preset_count(void);

/* 功能：按索引取得只读预设。
 * 输入：从 0 开始的 index。
 * 输出：有效预设指针，越界时为 NULL。 */
const EnginePreset *engine_preset_at(size_t index);

/* 功能：按不区分大小写的代号取得只读预设。
 * 输入：id。
 * 输出：匹配的预设指针，未找到时为 NULL。 */
const EnginePreset *engine_preset_find(const char *id);

/* 功能：将预设值填入燃烧室和喷管输入；调用方随后可以覆盖任意工况值。
 * 输入：preset、liquid、nozzle；三个指针都必须非空。
 * 输出：成功返回 1；失败返回 0。 */
int engine_preset_apply(const EnginePreset *preset, LiquidChamberInput *liquid,
                        NozzleInput *nozzle);

#ifdef __cplusplus
}
#endif

#endif
