/* 文件功能：把喷管计算结果输出为人读表格、CSV 和 JSON。
 * 输入：NozzleResult。
 * 输出：写入指定输出流或文件。 */
#ifndef NOZZLE_REPORT_H
#define NOZZLE_REPORT_H

#include <stdio.h>

#include "nozzle_high_level.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 功能：在输出流上打印完整结果（自检、喉部、出口、性能、对照与组分）。
 * 输入：stream、result、all_species（1 表示列出零含量组分）。
 * 输出：无。 */
void nozzle_print(FILE *stream, const NozzleResult *result, int all_species);

/* 功能：把沿程剖面写成 CSV（UTF-8，含 # 注释行给出算例摘要）。
 * 输入：result、path、error、error_size。
 * 输出：成功返回 1；失败返回 0 并写入 error。 */
int nozzle_write_csv(const NozzleResult *result, const char *path,
                     char *error, size_t error_size);

/* 功能：把完整结果写成 JSON，供前端读取。
 * 输入：result、path、error、error_size。
 * 输出：成功返回 1；失败返回 0 并写入 error。 */
int nozzle_write_json(const NozzleResult *result, const char *path,
                      char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
