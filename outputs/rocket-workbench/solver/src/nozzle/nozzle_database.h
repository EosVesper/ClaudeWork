/* 文件功能：把 NASA 九系数数据库规范化为"一个组分一条记录、多条温度段"。
 * 输入：原始数据库路径。
 * 输出：规范化后的数据库文件路径、合并统计与命名冲突清单。
 *
 * 背景：该数据库把同一凝相按温度区间（晶型、相变）拆成多条同名记录，
 * 例如 K2S(cr) 分 "Below lambda" 与 "Above lambda" 两条。燃烧室建表函数
 * 遇到重名会直接报错，导致温度窗无法放宽、喷管低温段的凝相无法参与计算。
 * 本模块在运行时生成一份等价的规范化副本，供物性解析与建表共同使用，
 * 从而得到完整的喷管候选产物表。 */
#ifndef NOZZLE_DATABASE_H
#define NOZZLE_DATABASE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 规范化结果统计。 */
typedef struct {
    int valid;                /* 1 表示规范化文件可用 */
    int cached;               /* 1 表示复用了上次生成的文件 */
    size_t records;           /* 原始记录条数 */
    size_t species;           /* 合并后的组分个数 */
    size_t merged_groups;     /* 发生合并的组数 */
    size_t case_collisions;   /* 仅大小写不同的名称组数 */
    size_t truncated_ranges;  /* 因书写格式限制被截断的温度段数 */
    char path[512];           /* 规范化文件的完整路径 */
    /* 仅大小写不同的名称，最多记录 8 个，用于提示物性可能被遮蔽。 */
    size_t shadowed_count;
    char shadowed[8][20];
} NozzleDatabase;

/* 功能：生成（或复用）规范化数据库。
 * 输入：source 为原始数据库路径。
 * 输出：成功返回 1 并写入 report；失败返回 0 并写入 error。
 *       规范化文件写在当前工作目录，失败时退到临时目录。 */
int nozzle_database_prepare(const char *source, NozzleDatabase *report,
                            char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
