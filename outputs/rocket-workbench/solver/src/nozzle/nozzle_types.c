/* 文件功能：把本模块的枚举量转换为中文文字，供报告与日志使用。
 * 输入：状态码、模型、截面给定方式、喉部判据。
 * 输出：返回常量字符串。 */
#include "nozzle_types.h"

/* 功能：把求解状态转换为文字。
 * 输入：status。
 * 输出：返回状态名称字符串。 */
const char *nozzle_status_name(NozzleStatus status)
{
    switch (status) {
    case NOZZLE_OK: return "成功";
    case NOZZLE_INVALID_INPUT: return "输入不合法";
    case NOZZLE_BALANCE_FAILURE: return "候选产物表建立失败";
    case NOZZLE_THERMO_FAILURE: return "物性查询失败";
    case NOZZLE_EQUILIBRIUM_FAILURE: return "平衡组分求解失败";
    case NOZZLE_NO_BRACKET: return "等熵方程无根";
    case NOZZLE_ROOT_FAILURE: return "求根未收敛";
    case NOZZLE_THROAT_FAILURE: return "喉部求解失败";
    case NOZZLE_MEMORY: return "内存不足";
    case NOZZLE_CHAMBER_INVALID: return "燃烧室状态无效";
    case NOZZLE_COMPOSITION_MISMATCH: return "候选产物表与燃烧室不一致";
    default: return "未知状态";
    }
}

/* 功能：把膨胀模型转换为文字。
 * 输入：model。
 * 输出：返回模型名称字符串。 */
const char *nozzle_model_name(NozzleModel model)
{
    switch (model) {
    case NOZZLE_MODEL_EQUILIBRIUM: return "化学平衡膨胀";
    case NOZZLE_MODEL_FROZEN_COMPOSITION: return "组分冻结膨胀";
    case NOZZLE_MODEL_FROZEN_ALL: return "完全冻结膨胀";
    case NOZZLE_MODEL_SUDDEN_FREEZE: return "突然冻结膨胀";
    default: return "未知模型";
    }
}

/* 功能：把截面给定方式转换为文字。
 * 输入：spec。
 * 输出：返回名称字符串。 */
const char *nozzle_section_spec_name(NozzleSectionSpec spec)
{
    switch (spec) {
    case NOZZLE_BY_PRESSURE: return "出口压强 p_e";
    case NOZZLE_BY_PRESSURE_RATIO: return "压强比 p_e/p_c";
    case NOZZLE_BY_AREA_RATIO: return "面积比 A_e/A_t";
    case NOZZLE_BY_MACH: return "出口马赫数 Ma_e";
    default: return "未知方式";
    }
}

/* 功能：把喉部判据转换为文字。
 * 输入：mode。
 * 输出：返回名称字符串。 */
const char *nozzle_throat_mode_name(NozzleThroatMode mode)
{
    switch (mode) {
    case NOZZLE_THROAT_SONIC: return "声速条件 u=a";
    case NOZZLE_THROAT_GAMMA: return "临界压强比（8.2）";
    case NOZZLE_THROAT_MASSFLUX: return "质量流率极大";
    default: return "未知判据";
    }
}

/* 功能：返回膨胀模型的英文代号。
 * 输入：model。
 * 输出：返回代号字符串。 */
const char *nozzle_model_code(NozzleModel model)
{
    switch (model) {
    case NOZZLE_MODEL_EQUILIBRIUM: return "equilibrium";
    case NOZZLE_MODEL_FROZEN_COMPOSITION: return "frozen_composition";
    case NOZZLE_MODEL_FROZEN_ALL: return "frozen_all";
    case NOZZLE_MODEL_SUDDEN_FREEZE: return "sudden_freeze";
    default: return "unknown";
    }
}

/* 功能：返回截面给定方式的英文代号。
 * 输入：spec。
 * 输出：返回代号字符串。 */
const char *nozzle_section_spec_code(NozzleSectionSpec spec)
{
    switch (spec) {
    case NOZZLE_BY_PRESSURE: return "pressure";
    case NOZZLE_BY_PRESSURE_RATIO: return "pressure_ratio";
    case NOZZLE_BY_AREA_RATIO: return "area_ratio";
    case NOZZLE_BY_MACH: return "mach";
    default: return "unknown";
    }
}

/* 功能：返回喉部判据的英文代号。
 * 输入：mode。
 * 输出：返回代号字符串。 */
const char *nozzle_throat_mode_code(NozzleThroatMode mode)
{
    switch (mode) {
    case NOZZLE_THROAT_SONIC: return "sonic";
    case NOZZLE_THROAT_GAMMA: return "gamma";
    case NOZZLE_THROAT_MASSFLUX: return "massflux";
    default: return "unknown";
    }
}
