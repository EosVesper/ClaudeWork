/* 文件功能：保存液体发动机基准工况，并提供检索与输入结构初始化。
 * 输入：型号代号或索引。
 * 输出：只读基准参数，或燃烧室/喷管输入结构。 */
#include "engine_presets.h"
#include <ctype.h>
#include <string.h>

static const EnginePreset presets[] = {
    {"TQ-12B", "天鹊-12B", "LOX", "LCH4", 90.0, 111.0, 316.704,
     2.9, 14.0e6, 20.5, 101325.0, "第一次大作业报告.docx，表4～5"},
    {"TQ-15B", "天鹊-15B（真空型）", "LOX", "LCH4", 90.0, 111.0, 307.580,
     2.9, 14.0e6, 50.0, 0.0, "第一次大作业报告.docx，表7～8"},
    {"YF-100K", "YF-100K系列参考模型", "LOX", "RP1", 90.17, 298.15, 422.583,
     2.6, 18.7e6, 35.0, 101325.0, "第一次大作业报告.docx，表10～11"},
    {"SSME", "SSME（100%额定工况）", "LOX", "LH2", 90.0, 20.0, 0.0,
     6.0, 3006.0 * 6894.757293168, 77.5, 101325.0,
     "NASA NTRS 19910014930, https://ntrs.nasa.gov/api/citations/19910014930/downloads/19910014930.pdf"},
    {"MERLIN-1D+", "Merlin 1D+（海平面型）", "LOX", "RP1", 90.0, 298.15, 0.0,
     2.32, 11.0e6, 16.0, 101325.0,
     "Strathclyde Merlin Specifications Table 5.11, https://stax.strath.ac.uk/downloads/s1784m21v"},
    {"RAPTOR-2", "Raptor 2（海平面型）", "LOX", "LCH4", 90.0, 111.0, 663.4,
     3.6, 30.0e6, 32.0, 101325.0,
     "DLR EUCASS2025-641 Table 1, https://elib.dlr.de/221988/1/EUCASS2025-641.pdf"},
    {"RL10A-3-3", "RL10A-3-3", "LOX", "LH2", 90.0, 20.0, 0.0,
     5.0, 395.0 * 6894.757293168, 57.0, 0.0,
     "NASA NTRS 19910018879 RL10 Evolution, https://ntrs.nasa.gov/api/citations/19910018879/downloads/19910018879.pdf"},
    {"VULCAIN-2", "Vulcain 2", "LOX", "LH2", 90.0, 20.0, 0.0,
     6.10, 11.5e6, 58.5, 101325.0,
     "ESA Bulletin 102 Table 1, https://www.esa.int/esapub/bulletin/bullet102/Coulon102.pdf"}
};

/* 功能：返回预设的总数。
 * 输入：无。
 * 输出：预设数量。 */
size_t engine_preset_count(void)
{
    return sizeof(presets) / sizeof(presets[0]);
}

/* 功能：按索引取得只读预设。
 * 输入：index。
 * 输出：预设指针，越界时为 NULL。 */
const EnginePreset *engine_preset_at(size_t index)
{
    return index < engine_preset_count() ? &presets[index] : NULL;
}

/* 功能：比较两个代号，忽略 ASCII 字母大小写。
 * 输入：a、b。
 * 输出：相同返回 1，否则返回 0。 */
static int same_id(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0;
        ++a;
        ++b;
    }
    return *a == *b;
}

/* 功能：按代号查找预设。
 * 输入：id。
 * 输出：匹配指针，未找到时为 NULL。 */
const EnginePreset *engine_preset_find(const char *id)
{
    size_t i;
    for (i = 0; i < engine_preset_count(); ++i)
        if (same_id(id, presets[i].id)) return &presets[i];
    return NULL;
}

/* 功能：根据预设初始化燃烧室和喷管输入。
 * 输入：preset、liquid、nozzle。
 * 输出：成功返回 1，空指针返回 0。 */
int engine_preset_apply(const EnginePreset *preset, LiquidChamberInput *liquid,
                        NozzleInput *nozzle)
{
    if (!preset || !liquid || !nozzle) return 0;
    memset(liquid, 0, sizeof(*liquid));
    liquid->oxidizer = preset->oxidizer;
    liquid->fuel = preset->fuel;
    liquid->T_ox = preset->T_ox;
    liquid->T_fuel = preset->T_fuel;
    liquid->m_dot_total = preset->m_dot_total;
    liquid->mixture_ratio = preset->mixture_ratio;
    liquid->p = preset->p_c;
    liquid->heat_case = CHAMBER_LIQUID_ADIABATIC;
    *nozzle = nozzle_default_input();
    nozzle->spec = NOZZLE_BY_AREA_RATIO;
    nozzle->value = preset->area_ratio;
    nozzle->p_a = preset->p_a;
    return 1;
}
