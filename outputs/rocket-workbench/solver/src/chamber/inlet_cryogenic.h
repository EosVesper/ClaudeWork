/* 文件功能：调用 CoolProp 状态方程计算液氧、液氢和液甲烷入口物性。
 * 输入：液体组元、入口温度 K 和绝对压力 Pa。
 * 输出：入口摩尔焓、热容及适用性标志。 */
#ifndef INLET_CRYOGENIC_H
#define INLET_CRYOGENIC_H
#include "inlet_enthalpy.h"

/* 功能：用状态方程与液相边界检查求低温液体入口物性。
 * 输入：c、out、error、size
 * 输出：成功返回 1 并写入 InletEnthalpyProperties。 */
int inlet_cryogenic_properties(const InletComponent *c, InletEnthalpyProperties *out,
                              char *error, size_t size);
#endif
