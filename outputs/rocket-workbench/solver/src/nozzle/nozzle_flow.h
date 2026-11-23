/* 文件功能：沿喷管轴线推进，给出压强与面积比的沿程分布。
 * 输入：参考状态、膨胀模型、出口压强、截面数。
 * 输出：包含燃烧室截面在内的 NozzleSection 数组。 */
#ifndef NOZZLE_FLOW_H
#define NOZZLE_FLOW_H

#include "nozzle_section.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 功能：按等比压强步长从燃烧室推进到给定出口压强。
 * 输入：sys、ref（须已确定喉部）、model、options、p_exit、n_sections、
 *       profile（容量不小于 n_sections）、error、error_size。
 * 输出：成功返回 1 并写入 profile；profile[0] 为燃烧室截面，
 *       profile[n_sections-1] 为 p_exit 对应截面。 */
int nozzle_flow_profile(const NozzleSystem *sys, NozzleReference *ref,
                        NozzleModel model, const NozzleSectionOptions *options,
                        double p_exit, int n_sections, NozzleSection *profile,
                        char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
