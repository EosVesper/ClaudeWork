/* 文件功能：燃烧室和喷管一体化计算的命令行程序。
 * 输入：推进剂与工况参数、喷管模型与截面选项。
 * 输出：屏幕表格，可选 CSV 与 JSON 文件。 */
#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "chamber_high_level.h"
#include "engine_presets.h"
#include "nozzle_high_level.h"
#include "nozzle_report.h"

/* 功能：打印用法说明。
 * 输入：name 为程序名。
 * 输出：无。 */
static void usage(const char *name)
{
    printf("用法：\n");
    printf("  %s <氧化剂> <燃料> <T_ox> <T_fuel> <mdot> <O/F> <p_c> [选项]\n", name);
    printf("  %s --solid <配方> <T_in> <p_c> [选项]\n", name);
    printf("  %s --engine <型号代号> [选项]\n", name);
    printf("  %s --list-engines\n", name);
    printf("\n液发示例：\n");
    printf("  %s LOX LCH4 90 111 1 3.6 30000000 --eps 40 --profile 25\n", name);
    printf("\n固发示例：\n");
    printf("  %s --solid JPN 298.15 7000000 --eps 12\n", name);
    printf("\n选项：\n");
    printf("  --eps E        面积比 A_e/A_t（默认 40）\n");
    printf("  --pe P         给定出口压强 Pa（替代面积比）\n");
    printf("  --pr R         给定压强比 p_e/p_c（替代面积比）\n");
    printf("  --mach M       给定出口马赫数（替代面积比）\n");
    printf("  --pa P         环境压强 Pa（默认 101325，0 表示真空）\n");
    printf("  --case C Q     液发热量情形：1 绝热（不填 Q）；2 燃料吸热（J/kg 燃料）；\n");
    printf("                 3 产物散热（J/kg 产物）\n");
    printf("  --cp-binder C  固发黏合剂比热 J/(kg·K)，仅 531/518\n");
    printf("  --cp-eff C     固发整剂等效比热 J/(kg·K)\n");
    printf("  --Tox T       液发氧化剂入口温度 K\n");
    printf("  --Tfuel T     液发燃料入口温度 K\n");
    printf("  --of K        液发主推力室氧燃质量比\n");
    printf("  --pc P        液发燃烧室绝对压强 Pa\n");
    printf("  --mdot M      主推力室质量流量 kg/s；液发可为 0，固发须大于 0\n");
    printf("  --model M      膨胀模型：eq 平衡（默认）、fc 组分冻结、\n");
    printf("                 fa 完全冻结、sf 突然冻结\n");
    printf("  --throat T     喉部判据：sonic 声速（默认）、gamma 临界压强比、\n");
    printf("                 massflux 质量流率极大\n");
    printf("  --profile N    沿程剖面截面数（默认 0，不算剖面）\n");
    printf("  --Tmin T       候选产物表温度窗下限 K（默认 200）\n");
    printf("  --Tmax T       候选产物表温度窗上限 K（默认 max(6000, T_c)）\n");
    printf("  --all          列出全部候选组分（含零含量）\n");
    printf("  --csv FILE     输出沿程剖面 CSV\n");
    printf("  --json FILE    输出完整结果 JSON\n");
    printf("  --thermo FILE  指定 NASA 数据库路径（默认当前目录 thermo.inp）\n");
    printf("  --trans FILE   指定 NASA 输运数据库路径（默认当前目录 trans.inp）\n");
}

/* 功能：列出全部可选液体发动机预设及其基准参数。
 * 输入：无。
 * 输出：型号代号、名称和主要工况参数。 */
static void list_engines(void)
{
    size_t k;
    for (k = 0; k < engine_preset_count(); ++k) {
        const EnginePreset *p = engine_preset_at(k);
        printf("%-12s %s  %s/%s  O/F=%.4g  p_c=%.8g Pa  eps=%.4g\n",
               p->id, p->name, p->oxidizer, p->fuel,
               p->mixture_ratio, p->p_c, p->area_ratio);
    }
}

/* UTF-8 TSV contract for the local workbench. Preset names and sources are
 * fixed strings from engine_presets.c and contain no tabs or newlines. */
static void list_engine_data(void)
{
    for (size_t k = 0; k < engine_preset_count(); ++k) {
        const EnginePreset *p = engine_preset_at(k);
        printf("%s\t%s\t%s\t%s\t%.12g\t%.12g\t%.12g\t%.12g\t%.12g\t%.12g\t%.12g\t%s\n",
               p->id,p->name,p->oxidizer,p->fuel,p->T_ox,p->T_fuel,p->mixture_ratio,
               p->p_c,p->area_ratio,p->p_a,p->m_dot_total,p->source);
    }
}

static void print_energy_trace(void *context, EnergyTracePhase phase, int evaluation,
                               double temperature, double residual)
{
    FILE *file = context;
    fprintf(file, "TRACE\t%d\t%d\t%.17g\t%.17g\n",
            evaluation, (int)phase, temperature, residual);
}

/* 功能：解析一个有限的命令行浮点数。
 * 输入：文本及输出指针。
 * 输出：有效时返回 1 并写入数值，否则返回 0。 */
static int parse_number(const char *text, double *value)
{
    char *end;
    errno = 0;
    *value = strtod(text, &end);
    return errno == 0 && end != text && *end == '\0' && isfinite(*value);
}

/* 功能：解析模型代号。
 * 输入：text。
 * 输出：成功返回 1 并写入 model。 */
static int parse_model(const char *text, NozzleModel *model)
{
    if (strcmp(text, "eq") == 0) { *model = NOZZLE_MODEL_EQUILIBRIUM; return 1; }
    if (strcmp(text, "fc") == 0) { *model = NOZZLE_MODEL_FROZEN_COMPOSITION; return 1; }
    if (strcmp(text, "fa") == 0) { *model = NOZZLE_MODEL_FROZEN_ALL; return 1; }
    if (strcmp(text, "sf") == 0) { *model = NOZZLE_MODEL_SUDDEN_FREEZE; return 1; }
    return 0;
}

/* 功能：解析喉部判据代号。
 * 输入：text。
 * 输出：成功返回 1 并写入 mode。 */
static int parse_throat(const char *text, NozzleThroatMode *mode)
{
    if (strcmp(text, "sonic") == 0) { *mode = NOZZLE_THROAT_SONIC; return 1; }
    if (strcmp(text, "gamma") == 0) { *mode = NOZZLE_THROAT_GAMMA; return 1; }
    if (strcmp(text, "massflux") == 0) { *mode = NOZZLE_THROAT_MASSFLUX; return 1; }
    return 0;
}

int main(int argc, char **argv)
{
    static ChamberOutlet chamber;
    static NozzleResult result;
    NozzleInput nozzle = nozzle_default_input();
    LiquidChamberInput liquid = {0};
    SolidChamberInput solid = {0};
    char error[512] = {0};
    const char *csv_path = NULL;
    const char *json_path = NULL;
    const char *thermo_path = NULL;
    const EnginePreset *engine = NULL;
    EnergyOptions energy_options;
    const EnergyOptions *energy = NULL;
    int all_species = 0;
    int is_solid = 0;
    int i = 1;

#ifdef _WIN32
    /* 程序输出 UTF-8，而中文 Windows 控制台默认按 GBK 解码，会显示为乱码；
     * 这里把控制台输出代码页切到 UTF-8。重定向到文件时不受影响。 */
    SetConsoleOutputCP(65001);
#endif
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    if (strcmp(argv[i], "--list-engines") == 0 && argc == 2) {
        list_engines();
        return 0;
    }
    if (strcmp(argv[i], "--list-engine-data") == 0 && argc == 2) {
        list_engine_data();
        return 0;
    }
    if (strcmp(argv[i], "--engine") == 0) {
        if (argc < 3 || !(engine = engine_preset_find(argv[i + 1]))) {
            fprintf(stderr, "未知的发动机型号。可用型号：\n");
            list_engines();
            return 1;
        }
        engine_preset_apply(engine, &liquid, &nozzle);
        i += 2;
    } else if (strcmp(argv[i], "--solid") == 0) {
        is_solid = 1;
        if (argc < i + 4) {
            usage(argv[0]);
            return 1;
        }
        solid.propellant = argv[i + 1];
        solid.T_in = atof(argv[i + 2]);
        solid.p = atof(argv[i + 3]);
        solid.m_dot_total = 0.0;
        solid.cp_mode = CHAMBER_SOLID_CP_DEFAULT;
        solid.cp_value = 0.0;
        solid.thermo_path = NULL;
        i += 4;
    } else {
        if (argc < i + 7) {
            usage(argv[0]);
            return 1;
        }
        liquid.oxidizer = argv[i];
        liquid.fuel = argv[i + 1];
        liquid.T_ox = atof(argv[i + 2]);
        liquid.T_fuel = atof(argv[i + 3]);
        liquid.m_dot_total = atof(argv[i + 4]);
        liquid.mixture_ratio = atof(argv[i + 5]);
        liquid.p = atof(argv[i + 6]);
        liquid.heat_case = CHAMBER_LIQUID_ADIABATIC;
        liquid.heat = 0.0;
        liquid.thermo_path = NULL;
        i += 7;
    }

    for (; i < argc; ++i) {
        const char *option = argv[i];
        if (strcmp(option, "--trace") == 0) {
            energy_options = energy_default_options(1500,5000);
            energy_options.trace = print_energy_trace;
            energy_options.trace_context = stdout;
            energy = &energy_options;
        } else if (strcmp(option, "--all") == 0) {
            all_species = 1;
        } else if (strcmp(option, "--eps") == 0 && i + 1 < argc) {
            nozzle.spec = NOZZLE_BY_AREA_RATIO;
            nozzle.value = atof(argv[++i]);
        } else if (strcmp(option, "--pe") == 0 && i + 1 < argc) {
            nozzle.spec = NOZZLE_BY_PRESSURE;
            nozzle.value = atof(argv[++i]);
        } else if (strcmp(option, "--pr") == 0 && i + 1 < argc) {
            nozzle.spec = NOZZLE_BY_PRESSURE_RATIO;
            nozzle.value = atof(argv[++i]);
        } else if (strcmp(option, "--mach") == 0 && i + 1 < argc) {
            nozzle.spec = NOZZLE_BY_MACH;
            nozzle.value = atof(argv[++i]);
        } else if (strcmp(option, "--pa") == 0 && i + 1 < argc) {
            nozzle.p_a = atof(argv[++i]);
        } else if (strcmp(option, "--case") == 0 && !is_solid && i + 1 < argc) {
            const char *case_text = argv[++i];
            if (strcmp(case_text, "1") == 0) {
                liquid.heat_case = CHAMBER_LIQUID_ADIABATIC;
                liquid.heat = 0.0;
            } else if ((strcmp(case_text, "2") == 0 || strcmp(case_text, "3") == 0) && i + 1 < argc) {
                liquid.heat_case = strcmp(case_text, "2") == 0
                    ? CHAMBER_LIQUID_HEAT_IN_PER_KG_FUEL
                    : CHAMBER_LIQUID_HEAT_LOSS_PER_KG_PRODUCT;
                liquid.heat = atof(argv[++i]);
                if (!isfinite(liquid.heat) || liquid.heat < 0.0) {
                    fprintf(stderr, "热量 Q 必须为非负有限数。\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "液发热量情形必须为 1、2 或 3；情形 2/3 还需输入热量 Q。\n");
                return 1;
            }
        } else if (strcmp(option, "--cp-binder") == 0 && is_solid && i + 1 < argc) {
            if (solid.cp_mode != CHAMBER_SOLID_CP_DEFAULT) return 1;
            solid.cp_mode = CHAMBER_SOLID_CP_BINDER;
            solid.cp_value = atof(argv[++i]);
        } else if (strcmp(option, "--cp-eff") == 0 && is_solid && i + 1 < argc) {
            if (solid.cp_mode != CHAMBER_SOLID_CP_DEFAULT) return 1;
            solid.cp_mode = CHAMBER_SOLID_CP_EFFECTIVE;
            solid.cp_value = atof(argv[++i]);
        } else if (strcmp(option, "--Tox") == 0 && !is_solid && i + 1 < argc) {
            if (!parse_number(argv[++i], &liquid.T_ox) || liquid.T_ox <= 0.0) {
                fprintf(stderr, "液发氧化剂入口温度必须为正的有限数。\n");
                return 1;
            }
        } else if (strcmp(option, "--Tfuel") == 0 && !is_solid && i + 1 < argc) {
            if (!parse_number(argv[++i], &liquid.T_fuel) || liquid.T_fuel <= 0.0) {
                fprintf(stderr, "液发燃料入口温度必须为正的有限数。\n");
                return 1;
            }
        } else if (strcmp(option, "--of") == 0 && !is_solid && i + 1 < argc) {
            if (!parse_number(argv[++i], &liquid.mixture_ratio) || liquid.mixture_ratio <= 0.0) {
                fprintf(stderr, "液发氧燃质量比必须为正的有限数。\n");
                return 1;
            }
        } else if (strcmp(option, "--pc") == 0 && !is_solid && i + 1 < argc) {
            if (!parse_number(argv[++i], &liquid.p) || liquid.p <= 0.0) {
                fprintf(stderr, "液发室压必须为正的有限数，单位 Pa。\n");
                return 1;
            }
        } else if (strcmp(option, "--mdot") == 0 && i + 1 < argc) {
            double value;
            if (!parse_number(argv[++i], &value) || value < 0.0 || (is_solid && value == 0.0)) {
                fprintf(stderr, "质量流量必须为非负有限数；固发不得为 0。\n");
                return 1;
            }
            if (is_solid) solid.m_dot_total = value;
            else liquid.m_dot_total = value;
        } else if (strcmp(option, "--model") == 0 && i + 1 < argc) {
            if (!parse_model(argv[++i], &nozzle.model)) {
                fprintf(stderr, "未知的膨胀模型代号。\n");
                return 1;
            }
        } else if (strcmp(option, "--throat") == 0 && i + 1 < argc) {
            if (!parse_throat(argv[++i], &nozzle.throat)) {
                fprintf(stderr, "未知的喉部判据代号。\n");
                return 1;
            }
        } else if (strcmp(option, "--profile") == 0 && i + 1 < argc) {
            nozzle.n_profile = atoi(argv[++i]);
        } else if (strcmp(option, "--Tmin") == 0 && i + 1 < argc) {
            nozzle.T_min = atof(argv[++i]);
        } else if (strcmp(option, "--Tmax") == 0 && i + 1 < argc) {
            nozzle.T_max = atof(argv[++i]);
        } else if (strcmp(option, "--csv") == 0 && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (strcmp(option, "--thermo") == 0 && i + 1 < argc) {
            /* 数据库路径同时交给燃烧室与喷管，保证两者用同一份数据。 */
            thermo_path = argv[++i];
            nozzle.thermo_path = thermo_path;
            if (is_solid) solid.thermo_path = thermo_path;
            else liquid.thermo_path = thermo_path;
        } else if (strcmp(option, "--trans") == 0 && i + 1 < argc) {
            nozzle.trans_path = argv[++i];
        } else if (strcmp(option, "--json") == 0 && i + 1 < argc) {
            json_path = argv[++i];
        } else {
            fprintf(stderr, "无法识别的选项：%s\n", option);
            usage(argv[0]);
            return 1;
        }
    }

    /* 先求燃烧室出口状态。 */
    if (is_solid) {
        if (!chamber_solve_solid(&solid, energy, &chamber, error, sizeof(error))) {
            fprintf(stderr, "燃烧室求解失败：%s\n", error);
            return 1;
        }
        printf("算例：固体推进剂 %s，入口温度 %.2f K，室压 %.6g Pa\n",
               solid.propellant, solid.T_in, solid.p);
    } else {
        if (!chamber_solve_liquid(&liquid, energy, &chamber, error, sizeof(error))) {
            fprintf(stderr, "燃烧室求解失败：%s\n", error);
            return 1;
        }
        if (engine) printf("发动机预设：%s（%s）\n", engine->name, engine->id);
        printf("算例：%s/%s，T_ox=%.2f K，T_fuel=%.2f K，O/F=%.4g，室压 %.6g Pa\n",
               liquid.oxidizer, liquid.fuel, liquid.T_ox, liquid.T_fuel,
               liquid.mixture_ratio, liquid.p);
    }
    printf("燃烧室出口：T_c = %.4f K，p_c = %.6g Pa，s_c = %.4f J/(kg*K)\n",
           chamber.T_c, chamber.p_c, chamber.s_c);

    /* 再以燃烧室出口状态求喷管流动。 */
    nozzle.chamber = &chamber;
    if (!nozzle_solve(&nozzle, &result)) {
        fprintf(stderr, "喷管求解失败：%s\n", result.error);
        nozzle_result_free(&result);
        return 1;
    }
    printf("\n");
    nozzle_print(stdout, &result, all_species);

    if (csv_path) {
        if (!nozzle_write_csv(&result, csv_path, error, sizeof(error)))
            fprintf(stderr, "CSV 输出失败：%s\n", error);
        else
            printf("已写出 CSV：%s\n", csv_path);
    }
    if (json_path) {
        if (!nozzle_write_json(&result, json_path, error, sizeof(error)))
            fprintf(stderr, "JSON 输出失败：%s\n", error);
        else
            printf("已写出 JSON：%s\n", json_path);
    }
    nozzle_result_free(&result);
    return 0;
}
