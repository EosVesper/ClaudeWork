/* 文件功能：喷管热力计算模块的对外总接口，一次调用完成喉部、出口和性能计算。
 * 输入：燃烧室出口状态、膨胀模型、截面给定方式和环境压强。
 * 输出：喉部与出口截面、理想性能参数、出口组分及自检量。 */
#include "nozzle_high_level.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nozzle_balance.h"
#include "nozzle_throat.h"

/* 功能：把状态错误写入缓冲区并把状态码置位。
 * 输入：result、status、message。
 * 输出：返回失败值 0。 */
static int fail(NozzleResult *result, NozzleStatus status, const char *message)
{
    result->valid = 0;
    result->status = status;
    /* message 可能就指向 result->error，此时无需再复制。 */
    if (message && message != result->error)
        snprintf(result->error, sizeof(result->error), "%s", message);
    return 0;
}

/* 功能：生成默认输入。
 * 输入：无。
 * 输出：返回填好的 NozzleInput。 */
NozzleInput nozzle_default_input(void)
{
    NozzleInput input;
    memset(&input, 0, sizeof(input));
    input.model = NOZZLE_MODEL_EQUILIBRIUM;
    input.throat = NOZZLE_THROAT_SONIC;
    input.spec = NOZZLE_BY_AREA_RATIO;
    input.value = 40.0;
    input.p_a = 101325.0;
    input.n_profile = 0;
    /* 候选产物表已由本模块规范化，温度窗可取得足够宽，覆盖喷管膨胀的低温段。 */
    input.T_min = 200.0;
    input.T_max = 0.0;
    input.T_search_min = 300.0;
    return input;
}

/* 功能：释放结果中动态分配的部分。
 * 输入：result。
 * 输出：无。 */
void nozzle_result_free(NozzleResult *result)
{
    if (!result) return;
    free(result->profile);
    result->profile = NULL;
    result->n_profile = 0;
    nozzle_balance_free(result->owned_balance);
    result->owned_balance = NULL;
    result->balance = NULL;
}

/* 功能：把燃烧室组成按名称映射到候选产物表的顺序。
 * 输入：chamber、balance、n。
 * 输出：matched 返回匹配上的组分数。 */
static size_t map_chamber_composition(const ChamberOutlet *chamber,
                                      const MassBalance *balance, double *n)
{
    size_t j, i, matched = 0;
    for (j = 0; j < balance->N; ++j) {
        n[j] = 0.0;
        for (i = 0; i < chamber->N; ++i)
            if (strcmp(balance->names[j], chamber->species[i].name) == 0) {
                n[j] = chamber->species[i].n;
                ++matched;
                break;
            }
    }
    return matched;
}

/* 功能：完成喷管热力计算。
 * 输入：input。
 * 输出：成功返回 1，result->valid=1；失败返回 0。 */
int nozzle_solve(const NozzleInput *input, NozzleResult *result)
{
    NozzleSystem sys;
    NozzleReference ref;
    NozzleSectionOptions options;
    GibbsStandard standard;
    double n_c[BALANCE_MAX_PRODUCTS];
    NozzleMixture mix;
    NozzleSectionRequest request;
    double T_min, T_max, search_min;
    double m_dot;
    size_t matched;

    if (!input || !result)
        return 0;
    memset(result, 0, sizeof(*result));
    result->status = NOZZLE_INVALID_INPUT;
    if (!input->chamber)
        return fail(result, NOZZLE_INVALID_INPUT, "未提供燃烧室出口状态。");
    if (!input->chamber->valid)
        return fail(result, NOZZLE_CHAMBER_INVALID, "燃烧室出口状态无效。");

    /* 1. 候选产物表：优先使用调用方给的表，否则按温度窗重建。
     * 数据库已在本模块内部规范化（同名记录合并），因此温度窗可以放宽到
     * 覆盖喷管膨胀的低温度，把低温凝相也纳入候选。 */
    T_min = (input->T_min > 0.0) ? input->T_min : 200.0;
    T_max = (input->T_max > 0.0) ? input->T_max
                                 : fmax(6000.0, input->chamber->T_c);
    if (input->balance) {
        result->balance = input->balance;
    } else {
        result->owned_balance = nozzle_balance_build(input->chamber, input->thermo_path,
                                                     T_min, T_max, NULL,
                                                     result->error, sizeof(result->error));
        if (!result->owned_balance) {
            result->status = NOZZLE_BALANCE_FAILURE;
            return 0;
        }
        result->balance = result->owned_balance;
    }
    if (!nozzle_system_init(&sys, result->balance, input->chamber->p_0,
                            result->error, sizeof(result->error))) {
        result->status = NOZZLE_BALANCE_FAILURE;
        return 0;
    }
    /* 记录数据库规范化的统计，便于在报告里说明候选表是怎么来的。 */
    {
        const NozzleDatabase *db = nozzle_database_info();
        if (db) {
            result->db_records = db->records;
            result->db_species = db->species;
            result->db_merged_groups = db->merged_groups;
            result->db_case_collisions = db->case_collisions;
            snprintf(result->db_path, sizeof(result->db_path), "%s", db->path);
        }
    }

    /* 2. 自检：用重建的候选表复算燃烧室状态。 */
    matched = map_chamber_composition(input->chamber, result->balance, n_c);
    if (matched < input->chamber->N) {
        snprintf(result->warning, sizeof(result->warning),
                 "有 %zu 个燃烧室候选产物未出现在重建的候选表中，结果可能偏离。",
                 input->chamber->N - matched);
    }
    /* 若候选表中出现仅大小写不同的组分，物性查询会命中文件里靠前的那一条，
     * 造成静默错配，这里给出提示（例如 Co 会被 CO 遮蔽）。 */
    {
        const NozzleDatabase *db = nozzle_database_info();
        size_t j, k;
        if (db && db->shadowed_count > 0 && result->warning[0] == '\0') {
            for (j = 0; j < result->balance->N && result->warning[0] == '\0'; ++j)
                for (k = 0; k < db->shadowed_count; ++k)
                    if (strcmp(result->balance->names[j], db->shadowed[k]) == 0) {
                        snprintf(result->warning, sizeof(result->warning),
                                 "组分 %s 与另一组分仅大小写不同，物性可能被同名的另一条记录遮蔽。",
                                 result->balance->names[j]);
                        break;
                    }
        }
    }
    if (!gibbs_prepare(result->balance, input->chamber->T_c, input->chamber->p_0,
                       &standard, result->error, sizeof(result->error))) {
        result->status = NOZZLE_THERMO_FAILURE;
        return 0;
    }
    if (!nozzle_mixture_evaluate(&sys, &standard, input->chamber->p_c, n_c, &mix,
                                 result->error, sizeof(result->error))) {
        result->status = NOZZLE_THERMO_FAILURE;
        return 0;
    }
    result->check_h = mix.h - input->chamber->h_c;
    result->check_s = mix.s - input->chamber->s_c;
    {
        size_t j;
        double mass = 0.0;
        for (j = 0; j < result->balance->N; ++j)
            mass += n_c[j] * result->balance->molar_mass[j];
        result->check_mass = mass;
    }
    {
        EquilibriumResult eq;
        EquilibriumOptions eq_options = equilibrium_default_options();
        EquilibriumStatus status = equilibrium_solve(result->balance, &standard,
                                                     input->chamber->p_c, n_c,
                                                     &eq_options, &eq,
                                                     result->error, sizeof(result->error));
        if (status != EQ_OK) {
            result->status = NOZZLE_EQUILIBRIUM_FAILURE;
            return 0;
        }
        {
            size_t j;
            double deviation = 0.0;
            for (j = 0; j < result->balance->N; ++j) {
                double d = fabs(eq.n[j] - n_c[j]);
                if (d > deviation) deviation = d;
            }
            result->check_n_deviation = deviation;
        }
    }
    /* 质量闭合用数据库自带的摩尔质量（由数据库原子量算得），与燃烧室用
     * 元素表原子量算得的摩尔质量来自两套原子量，相差约 1e-5，故判据取 1e-4。 */
    if (fabs(result->check_s) > 10.0 || fabs(result->check_mass - 1.0) > 1e-4) {
        return fail(result, NOZZLE_COMPOSITION_MISMATCH,
                    "候选产物表与燃烧室状态不一致：熵或质量闭合偏差过大。");
    }

    /* 3. 组装参考状态。 */
    memset(&ref, 0, sizeof(ref));
    ref.p_c = input->chamber->p_c;
    ref.T_c = input->chamber->T_c;
    ref.h_c = input->chamber->h_c;
    ref.s_c = input->chamber->s_c;
    ref.gamma_c = mix.gamma;
    ref.RmT_c = mix.R_m * mix.T;
    memcpy(ref.n_c, n_c, sys.N * sizeof(n_c[0]));

    result->p_c = ref.p_c;
    result->T_c = ref.T_c;
    result->h_c = ref.h_c;
    result->s_c = ref.s_c;
    result->chamber_temperature_evaluations = input->chamber->temperature_evaluations;
    result->chamber_brent_iterations = input->chamber->brent_iterations;
    result->chamber_equilibrium_iterations = input->chamber->equilibrium_iterations;
    result->chamber_phase_iterations = input->chamber->phase_iterations;
    result->gamma_c = ref.gamma_c;
    result->R_m_c = mix.R_m;
    result->Mw_c = mix.Mw_equiv;
    result->n_g_c = mix.n_g;
    result->model = input->model;
    result->throat_mode = input->throat;
    result->spec = input->spec;
    result->spec_value = input->value;
    result->N = sys.N;
    result->L = sys.L;

    /* 4. 等熵方程求解窗口与喉部。熵搜索窗与候选表窗独立，默认下界 300 K。 */
    search_min = (input->T_search_min > 0.0) ? input->T_search_min : T_min;
    options = nozzle_section_default_options(search_min, ref.T_c);
    if (!nozzle_throat_solve(&sys, &ref, input->model, input->throat, &options,
                             &result->throat, result->error, sizeof(result->error))) {
        result->status = NOZZLE_THROAT_FAILURE;
        return 0;
    }

    /* 5. 出口截面。 */
    request.spec = input->spec;
    request.value = input->value;
    if (!nozzle_section_solve(&sys, &ref, input->model, &request, &options,
                              &result->exit_section, result->n_exit,
                              result->error, sizeof(result->error))) {
        result->status = NOZZLE_ROOT_FAILURE;
        return 0;
    }

    /* 6. 理想性能参数。出口压强不低于喉部说明喷管未达临界状态，给出提示。 */
    if (result->exit_section.p >= result->throat.p && result->warning[0] == '\0')
        snprintf(result->warning, sizeof(result->warning),
                 "出口压强不低于喉部压强，喷管未达临界状态，性能参数仅供参考。");
    m_dot = (input->m_dot > 0.0) ? input->m_dot : input->chamber->m_dot_total;
    if (!nozzle_performance_solve(&ref, &result->throat, &result->exit_section,
                                  input->p_a, m_dot, &result->perf,
                                  result->error, sizeof(result->error))) {
        result->status = NOZZLE_INVALID_INPUT;
        return 0;
    }

    /* 7. 沿程剖面（可选）。 */
    if (input->n_profile >= 2) {
        result->profile = (NozzleSection *)calloc((size_t)input->n_profile,
                                                  sizeof(*result->profile));
        if (!result->profile)
            return fail(result, NOZZLE_MEMORY, "沿程剖面数组内存不足。");
        if (!nozzle_flow_profile(&sys, &ref, input->model, &options,
                                 result->exit_section.p, input->n_profile,
                                 result->profile, result->error,
                                 sizeof(result->error))) {
            /* 沿程剖面只是附加展示，失败不应影响主结果。 */
            free(result->profile);
            result->profile = NULL;
            result->n_profile = 0;
            result->error[0] = '\0';
            if (result->warning[0] == '\0')
                snprintf(result->warning, sizeof(result->warning),
                         "沿程剖面计算失败，已跳过；喉部与指定截面的结果不受影响。");
        } else {
            result->n_profile = input->n_profile;
        }
    }
    /* 补充产物物性只读取已收敛的状态；缺少输运数据不影响热力主结果。 */
    {
        char property_error[256] = {0};
        if (!product_properties_evaluate(result->balance, n_c, ref.T_c, ref.p_c,
                                         input->trans_path, &result->chamber_products,
                                         property_error, sizeof(property_error)))
            snprintf(result->product_warning, sizeof(result->product_warning),
                     "燃烧室产物物性：%s", property_error);
        if (!product_properties_evaluate(result->balance, result->n_exit,
                                         result->exit_section.T, result->exit_section.p,
                                         input->trans_path, &result->exit_products,
                                         property_error, sizeof(property_error)) &&
            result->product_warning[0] == '\0')
            snprintf(result->product_warning, sizeof(result->product_warning),
                     "喷管出口产物物性：%s", property_error);
    }
    result->valid = 1;
    result->status = NOZZLE_OK;
    return 1;
}
