/* 文件功能：确定喷管喉部（临界截面）的压强、温度、流速与质量流率密度。
 * 输入：候选产物表、燃烧室参考状态、膨胀模型和喉部判据。
 * 输出：喉部截面 NozzleSection，并把喉部量写回参考状态供沿程计算使用。 */
#ifndef NOZZLE_THROAT_H
#define NOZZLE_THROAT_H

#include "nozzle_section.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 功能：求解喉部。
 * 输入：sys、ref（会被写入喉部量，含突然冻结所需的喉部组成）、model、
 *       mode、options（可为 NULL）、error、error_size。
 * 输出：成功返回 1 并写入 out；失败返回 0。 */
int nozzle_throat_solve(const NozzleSystem *sys, NozzleReference *ref,
                        NozzleModel model, NozzleThroatMode mode,
                        const NozzleSectionOptions *options,
                        NozzleSection *out, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
