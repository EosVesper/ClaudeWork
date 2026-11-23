/* 文件功能：把喷管计算结果输出为人读表格、CSV 和 JSON。
 * 输入：NozzleResult。
 * 输出：写入指定输出流或文件。 */
#include "nozzle_report.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* 功能：打印一条分隔线。
 * 输入：stream、字符。
 * 输出：无。 */
static void rule(FILE *stream, char c)
{
    int i;
    for (i = 0; i < 78; ++i) fputc(c, stream);
    fputc('\n', stream);
}

/* 功能：打印一组"名称 = 数值 单位"的键值对。
 * 输入：stream、名称、数值、格式、单位。
 * 输出：无。 */
static void field(FILE *stream, const char *name, double value, const char *format,
                  const char *unit)
{
    fprintf(stream, "  %-14s ", name);
    fprintf(stream, format, value);
    if (unit && unit[0]) fprintf(stream, " %s", unit);
    fputc('\n', stream);
}

/* 功能：打印一个截面的主要参数。
 * 输入：stream、标题、截面。
 * 输出：无。 */
static void print_section(FILE *stream, const char *title, const NozzleSection *s)
{
    fprintf(stream, "  [%s]\n", title);
    field(stream, "压强 p", s->p, "%12.6g", "Pa");
    field(stream, "温度 T", s->T, "%12.4f", "K");
    field(stream, "流速 u", s->u, "%12.3f", "m/s");
    field(stream, "声速 a", s->a, "%12.3f", "m/s");
    field(stream, "马赫数 Ma", s->Ma, "%12.5f", "");
    field(stream, "密度 rho", s->rho, "%12.6g", "kg/m^3");
    field(stream, "比容 v", s->v, "%12.6g", "m^3/kg");
    field(stream, "比焓 h", s->h, "%12.1f", "J/kg");
    field(stream, "比熵 s", s->s, "%12.4f", "J/(kg*K)");
    field(stream, "比热比 gamma", s->gamma, "%12.5f", "");
    field(stream, "平均等熵指数", s->gamma_avg, "%12.5f", "");
    field(stream, "比热 cp", s->cp, "%12.2f", "J/(kg*K)");
    field(stream, "质量流率密度 G", s->G, "%12.4g", "kg/(m^2*s)");
    field(stream, "凝相质量分数", s->Y_c, "%12.4g", "");
    if (s->eps > 0.0) field(stream, "面积比 A/A_t", s->eps, "%12.6f", "");
    field(stream, "熵残差 |ds|", fabs(s->s_residual), "%12.3e", "J/(kg*K)");
    fprintf(stream, "  %-14s %d\n", "温度迭代次数", s->iterations);
}

/* 功能：打印两种方法（热力法 8.1 与气动法 8.2）的对照。
 * 输入：stream、result。
 * 输出：无。 */
static void print_comparison(FILE *stream, const NozzleResult *r)
{
    const NozzlePerformance *p = &r->perf;
    fprintf(stream, "  %-16s %14s %14s %10s\n", "量", "热力法(8.1)", "气动法(8.2)", "相对差");
    fprintf(stream, "  %-16s %14.4f %14.4f %9.2f%%\n", "c* [m/s]", p->c_star,
            p->c_star_gamma, 100.0 * (p->c_star - p->c_star_gamma) / p->c_star_gamma);
    fprintf(stream, "  %-16s %14.4f %14.4f %9.2f%%\n", "u_e [m/s]", p->u_e,
            p->u_e_gamma, 100.0 * (p->u_e - p->u_e_gamma) / p->u_e_gamma);
    fprintf(stream, "  %-16s %14.4f %14.4f %9.2f%%\n", "I_s,vac [m/s]", p->I_sp_vac,
            p->I_sp_vac_gamma, 100.0 * (p->I_sp_vac - p->I_sp_vac_gamma) / p->I_sp_vac_gamma);
    fprintf(stream, "  %-16s %14.6f %14.6f %9.2f%%\n", "C_F,vac", p->C_F_vac,
            p->C_F_vac_gamma, 100.0 * (p->C_F_vac - p->C_F_vac_gamma) / p->C_F_vac_gamma);
    if (p->eps_gamma > 0.0)
        fprintf(stream, "  %-16s %14.6f %14.6f %9.2f%%\n", "面积比", p->eps,
                p->eps_gamma, 100.0 * (p->eps - p->eps_gamma) / p->eps_gamma);
}

/* 功能：打印出口组分表。
 * 输入：stream、result、all_species。
 * 输出：无。 */
static void print_composition(FILE *stream, const NozzleResult *r, int all_species)
{
    size_t j;
    if (!r->balance) return;
    fprintf(stream, "  %-20s %-6s %16s %14s\n", "组分", "相态", "n_j[mol/kg]", "质量分数");
    for (j = 0; j < r->balance->N; ++j) {
        const char *phase = (j < r->balance->L) ? "凝相" : "气相";
        double mass = r->n_exit[j] * r->balance->molar_mass[j];
        if (!all_species && r->n_exit[j] <= 1e-9) continue;
        fprintf(stream, "  %-20s %-6s %16.9g %14.6g\n", r->balance->names[j], phase,
                r->n_exit[j], mass);
    }
}

/* 输出由实际组成计算的补充物性，平衡导数和输运数据不足时分别标明。 */
static void print_product_properties(FILE *stream, const char *label,
                                     const ProductProperties *p)
{
    fprintf(stream, "  [%s]\n", label);
    if (!p->valid) { fprintf(stream, "  基本产物物性不可用\n"); return; }
    field(stream, "凝相质量分数", p->Y_c, "%12.6g", "kg/kg");
    field(stream, "气相平均摩尔质量", p->M_gas * 1000.0, "%12.4f", "g/mol");
    field(stream, "冻结 cp", p->cp_frozen, "%12.2f", "J/(kg*K)");
    field(stream, "冻结 cv", p->cv_frozen, "%12.2f", "J/(kg*K)");
    field(stream, "冻结 gamma", p->gamma_frozen, "%12.5f", "");
    if (p->equilibrium_valid) {
        field(stream, "平衡 cp", p->cp_equilibrium, "%12.2f", "J/(kg*K)");
        field(stream, "平衡 cv", p->cv_equilibrium, "%12.2f", "J/(kg*K)");
        field(stream, "平衡 gamma", p->gamma_equilibrium, "%12.5f", "");
    } else if (p->equilibrium_message[0])
        fprintf(stream, "  平衡导数：%s\n", p->equilibrium_message);
    if (p->transport_valid) {
        field(stream, "气相黏度", p->mu, "%12.6e", "Pa*s");
        field(stream, "气相导热系数", p->conductivity, "%12.6f", "W/(m*K)");
        field(stream, "Pr", p->Pr, "%12.5f", "");
    } else if (p->transport_message[0])
        fprintf(stream, "  输运性质：%s\n", p->transport_message);
}

/* 功能：按截面给定方式生成这一截面的标题。
 * 输入：spec、value。
 * 输出：返回静态缓冲区中的字符串（每次调用覆盖）。 */
static const char *section_label(NozzleSectionSpec spec, double value)
{
    static char text[64];
    switch (spec) {
    case NOZZLE_BY_AREA_RATIO:
        return "出口截面（按面积比确定）";
    case NOZZLE_BY_PRESSURE:
        snprintf(text, sizeof(text), "指定截面（p = %.6g Pa）", value);
        return text;
    case NOZZLE_BY_PRESSURE_RATIO:
        snprintf(text, sizeof(text), "指定截面（p/p_c = %.6g）", value);
        return text;
    case NOZZLE_BY_MACH:
        snprintf(text, sizeof(text), "指定截面（Ma = %.6g）", value);
        return text;
    default:
        return "指定截面";
    }
}

/* 功能：在输出流上打印完整结果。
 * 输入：stream、result、all_species。
 * 输出：无。 */
void nozzle_print(FILE *stream, const NozzleResult *r, int all_species)
{
    if (!stream || !r) return;
    rule(stream, '=');
    fprintf(stream, " 喷管热力计算\n");
    rule(stream, '=');
    fprintf(stream, " 膨胀模型：%s      喉部判据：%s\n", nozzle_model_name(r->model),
            nozzle_throat_mode_name(r->throat_mode));
    fprintf(stream, " 计算截面：%s\n\n", section_label(r->spec, r->spec_value));

    fprintf(stream, "[一] 燃烧室参考状态\n");
    field(stream, "p_c", r->p_c, "%14.6g", "Pa");
    field(stream, "T_c", r->T_c, "%14.4f", "K");
    field(stream, "h_c", r->h_c, "%14.1f", "J/kg");
    field(stream, "s_c", r->s_c, "%14.4f", "J/(kg*K)");
    field(stream, "gamma_c", r->gamma_c, "%14.6f", "");
    field(stream, "R_m,c", r->R_m_c, "%14.4f", "J/(kg*K)");
    field(stream, "Mw_c", r->Mw_c * 1000.0, "%14.4f", "g/mol");
    field(stream, "n_g,c", r->n_g_c, "%14.6f", "mol/kg");
    fprintf(stream, "  温度试算 %d 次；Brent 迭代 %d 次；最终平衡迭代 %d 次；凝相调整 %d 轮\n",
            r->chamber_temperature_evaluations, r->chamber_brent_iterations,
            r->chamber_equilibrium_iterations, r->chamber_phase_iterations);

    fprintf(stream, "\n[二] 自检：用重建的候选产物表复算燃烧室状态\n");
    if (r->db_records > 0)
        fprintf(stream, "  %-14s %zu 条记录 -> %zu 个组分（合并 %zu 组，大小写同名 %zu 组）\n",
                "数据库规范化", r->db_records, r->db_species, r->db_merged_groups,
                r->db_case_collisions);
    field(stream, "h 偏差", r->check_h, "%14.3e", "J/kg");
    field(stream, "s 偏差", r->check_s, "%14.3e", "J/(kg*K)");
    field(stream, "质量闭合", r->check_mass, "%14.10f", "kg/kg");
    field(stream, "平衡重解偏差", r->check_n_deviation, "%14.3e", "mol/kg");

    fprintf(stream, "\n[三] 喉部（临界截面）\n");
    print_section(stream, "喉部", &r->throat);
    fprintf(stream, "\n[四] %s\n", section_label(r->spec, r->spec_value));
    print_section(stream, "出口", &r->exit_section);

    fprintf(stream, "\n[五] 推力室理想性能参数\n");
    field(stream, "特征速度 c*", r->perf.c_star, "%14.4f", "m/s");
    field(stream, "出口速度 u_e", r->perf.u_e, "%14.4f", "m/s");
    field(stream, "比冲 I_s", r->perf.I_sp, "%14.4f", "m/s");
    field(stream, "真空比冲 I_s,vac", r->perf.I_sp_vac, "%14.4f", "m/s");
    field(stream, "推力系数 C_F", r->perf.C_F, "%14.6f", "");
    field(stream, "真空推力系数", r->perf.C_F_vac, "%14.6f", "");
    field(stream, "环境压强 p_a", r->perf.p_a, "%14.6g", "Pa");
    if (r->perf.m_dot > 0.0) {
        field(stream, "总质量流量", r->perf.m_dot, "%14.6g", "kg/s");
        field(stream, "喉部面积 A_t", r->perf.A_t, "%14.6e", "m^2");
        field(stream, "出口面积 A_e", r->perf.A_e, "%14.6e", "m^2");
        field(stream, "喉部直径 d_t", r->perf.d_t * 1000.0, "%14.4f", "mm");
        field(stream, "出口直径 d_e", r->perf.d_e * 1000.0, "%14.4f", "mm");
        field(stream, "推力 F", r->perf.F, "%14.6g", "N");
        field(stream, "真空推力 F_vac", r->perf.F_vac, "%14.6g", "N");
    } else {
        fprintf(stream, "  （总质量流量未知，只给出比参数，不给推力与几何尺寸）\n");
    }

    fprintf(stream, "\n[六] 两种方法对照\n");
    print_comparison(stream, r);

    fprintf(stream, "\n[七] 出口组分（mol/kg 推进剂）\n");
    print_composition(stream, r, all_species);

    fprintf(stream, "\n[八] 产物补充物性\n");
    print_product_properties(stream, "燃烧室", &r->chamber_products);
    print_product_properties(stream, "喷管出口", &r->exit_products);

    /* 沿程分布：截面较多时等间隔抽稀，最多 40 行，末行一定是出口。 */
    if (r->profile && r->n_profile >= 2) {
        int stride = (r->n_profile + 39) / 40;
        int k;
        if (stride < 1) stride = 1;
        fprintf(stream, "\n[九] 沿程分布（共 %d 个截面，每 %d 个取 1）\n",
                r->n_profile, stride);
        fprintf(stream, "  %4s %12s %9s %9s %8s %7s %9s\n",
                "i", "p[Pa]", "T[K]", "u[m/s]", "Ma", "gamma", "A/A_t");
        for (k = 0; k < r->n_profile; k += stride) {
            const NozzleSection *s = &r->profile[k];
            char eps_text[16];
            /* 面积比只在喉部以后有意义，亚音速段留空。 */
            if (s->eps > 0.0) snprintf(eps_text, sizeof(eps_text), "%9.4f", s->eps);
            else snprintf(eps_text, sizeof(eps_text), "%9s", "-");
            fprintf(stream, "  %4d %12.5g %9.2f %9.1f %8.4f %7.4f %s\n",
                    k, s->p, s->T, s->u, s->Ma, s->gamma, eps_text);
        }
        if ((r->n_profile - 1) % stride != 0) {
            const NozzleSection *s = &r->profile[r->n_profile - 1];
            char eps_text[16];
            if (s->eps > 0.0) snprintf(eps_text, sizeof(eps_text), "%9.4f", s->eps);
            else snprintf(eps_text, sizeof(eps_text), "%9s", "-");
            fprintf(stream, "  %4d %12.5g %9.2f %9.1f %8.4f %7.4f %s  (出口)\n",
                    r->n_profile - 1, s->p, s->T, s->u, s->Ma, s->gamma, eps_text);
        }
    }

    if (r->warning[0]) {
        fprintf(stream, "\n注意：%s\n", r->warning);
    }
    if (r->product_warning[0])
        fprintf(stream, "\n补充物性提示：%s\n", r->product_warning);
    rule(stream, '=');
}

/* 功能：写 CSV 列头。
 * 输入：stream。
 * 输出：无。 */
static void csv_header(FILE *stream)
{
    fprintf(stream, "index,p_Pa,T_K,u_m_s,Ma,gamma,gamma_avg,rho_kg_m3,a_m_s,"
                    "h_J_kg,s_J_kgK,eps,Y_c,Mw_equiv_kg_mol\n");
}

/* 功能：写一行 CSV。
 * 输入：stream、序号、截面。
 * 输出：无。 */
static void csv_row(FILE *stream, int index, const NozzleSection *s)
{
    fprintf(stream, "%d,%.8g,%.6f,%.6f,%.6f,%.6f,%.6f,%.8g,%.6f,%.2f,%.6f,%.6g,%.6g,%.8g\n",
            index, s->p, s->T, s->u, s->Ma, s->gamma, s->gamma_avg, s->rho, s->a,
            s->h, s->s, s->eps, s->Y_c, s->Mw_equiv);
}

/* 功能：把沿程剖面写成 CSV。
 * 输入：result、path、error、size。
 * 输出：成功返回 1；失败返回 0。 */
int nozzle_write_csv(const NozzleResult *r, const char *path,
                     char *error, size_t error_size)
{
    FILE *file;
    int i;
    if (!r || !r->valid || !path) {
        if (error && error_size) snprintf(error, error_size, "CSV 输出参数不合法。");
        return 0;
    }
    file = fopen(path, "w");
    if (!file) {
        if (error && error_size) snprintf(error, error_size, "无法写入 CSV：%s", path);
        return 0;
    }
    fprintf(file, "# 喷管热力计算沿程剖面\n");
    fprintf(file, "# model=%s throat=%s spec=%s value=%.8g\n",
            nozzle_model_code(r->model), nozzle_throat_mode_code(r->throat_mode),
            nozzle_section_spec_code(r->spec), r->spec_value);
    fprintf(file, "# p_c=%.8g Pa T_c=%.6f K s_c=%.6f J/(kg*K)\n", r->p_c, r->T_c, r->s_c);
    fprintf(file, "# p_t=%.8g Pa T_t=%.6f K G_t=%.8g kg/(m^2*s)\n",
            r->throat.p, r->throat.T, r->throat.G);
    fprintf(file, "# p_e=%.8g Pa T_e=%.6f K eps=%.8g\n",
            r->exit_section.p, r->exit_section.T, r->exit_section.eps);
    fprintf(file, "# c_star=%.6f m/s Isp_vac=%.6f m/s C_F_vac=%.6f\n",
            r->perf.c_star, r->perf.I_sp_vac, r->perf.C_F_vac);
    csv_header(file);
    if (r->profile && r->n_profile >= 2) {
        for (i = 0; i < r->n_profile; ++i) csv_row(file, i, &r->profile[i]);
    } else {
        /* 未计算剖面时，至少给出喉部与出口两行。 */
        csv_row(file, 0, &r->throat);
        csv_row(file, 1, &r->exit_section);
    }
    fclose(file);
    if (error && error_size) error[0] = '\0';
    return 1;
}

/* 功能：输出一个有限数值，非有限值写成 null 以保证 JSON 合法。
 * 输入：stream、value。
 * 输出：无。 */
static void jnum(FILE *stream, double value)
{
    if (isfinite(value)) fprintf(stream, "%.10g", value);
    else fprintf(stream, "null");
}

/* 功能：输出一个截面对象。
 * 输入：stream、缩进、名称、截面、是否为最后一项。
 * 输出：无。 */
static void jsection(FILE *stream, const char *indent, const char *name,
                     const NozzleSection *s, int last)
{
    fprintf(stream, "%s\"%s\": {\n", indent, name);
    fprintf(stream, "%s  \"p_Pa\": ", indent); jnum(stream, s->p); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"T_K\": ", indent); jnum(stream, s->T); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"u_m_s\": ", indent); jnum(stream, s->u); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"a_m_s\": ", indent); jnum(stream, s->a); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"Ma\": ", indent); jnum(stream, s->Ma); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"rho_kg_m3\": ", indent); jnum(stream, s->rho); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"h_J_kg\": ", indent); jnum(stream, s->h); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"s_J_kgK\": ", indent); jnum(stream, s->s); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"cp_J_kgK\": ", indent); jnum(stream, s->cp); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"cv_J_kgK\": ", indent); jnum(stream, s->cv); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"gamma\": ", indent); jnum(stream, s->gamma); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"gamma_avg\": ", indent); jnum(stream, s->gamma_avg); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"R_m\": ", indent); jnum(stream, s->R_m); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"Mw_gas_kg_mol\": ", indent); jnum(stream, s->Mw_gas); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"G_kg_m2s\": ", indent); jnum(stream, s->G); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"eps\": ", indent); jnum(stream, s->eps); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"Y_c\": ", indent); jnum(stream, s->Y_c); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"entropy_residual\": ", indent); jnum(stream, s->s_residual); fprintf(stream, ",\n");
    fprintf(stream, "%s  \"iterations\": %d\n", indent, s->iterations);
    fprintf(stream, "%s}%s\n", indent, last ? "" : ",");
}

/* 输出一个产物补充物性对象；各组结果的有效性用独立布尔值表示。 */
static void jproduct(FILE *stream, const char *name, const ProductProperties *p,
                     int last)
{
    fprintf(stream, "    \"%s\": {\n", name);
    fprintf(stream, "      \"valid\": %s,\n", p->valid ? "true" : "false");
    fprintf(stream, "      \"Y_c\": "); jnum(stream, p->Y_c); fprintf(stream, ",\n");
    fprintf(stream, "      \"M_gas_kg_mol\": "); jnum(stream, p->M_gas); fprintf(stream, ",\n");
    fprintf(stream, "      \"R_gas_J_kgK\": "); jnum(stream, p->R_gas); fprintf(stream, ",\n");
    fprintf(stream, "      \"R_m_J_kgK\": "); jnum(stream, p->R_m); fprintf(stream, ",\n");
    fprintf(stream, "      \"cp_frozen_J_kgK\": "); jnum(stream, p->cp_frozen); fprintf(stream, ",\n");
    fprintf(stream, "      \"cv_frozen_J_kgK\": "); jnum(stream, p->cv_frozen); fprintf(stream, ",\n");
    fprintf(stream, "      \"gamma_frozen\": "); jnum(stream, p->gamma_frozen); fprintf(stream, ",\n");
    fprintf(stream, "      \"equilibrium_valid\": %s,\n",
            p->equilibrium_valid ? "true" : "false");
    fprintf(stream, "      \"cp_equilibrium_J_kgK\": "); jnum(stream, p->cp_equilibrium); fprintf(stream, ",\n");
    fprintf(stream, "      \"cv_equilibrium_J_kgK\": "); jnum(stream, p->cv_equilibrium); fprintf(stream, ",\n");
    fprintf(stream, "      \"gamma_equilibrium\": "); jnum(stream, p->gamma_equilibrium); fprintf(stream, ",\n");
    fprintf(stream, "      \"transport_valid\": %s,\n",
            p->transport_valid ? "true" : "false");
    fprintf(stream, "      \"transport_coverage\": "); jnum(stream, p->transport_coverage); fprintf(stream, ",\n");
    fprintf(stream, "      \"mu_Pa_s\": "); jnum(stream, p->mu); fprintf(stream, ",\n");
    fprintf(stream, "      \"conductivity_W_mK\": "); jnum(stream, p->conductivity); fprintf(stream, ",\n");
    fprintf(stream, "      \"Pr\": "); jnum(stream, p->Pr); fprintf(stream, "\n");
    fprintf(stream, "    }%s\n", last ? "" : ",");
}

/* 功能：把完整结果写成 JSON。
 * 输入：result、path、error、size。
 * 输出：成功返回 1；失败返回 0。 */
int nozzle_write_json(const NozzleResult *r, const char *path,
                      char *error, size_t error_size)
{
    FILE *file;
    int i, first;
    if (!r || !r->valid || !path) {
        if (error && error_size) snprintf(error, error_size, "JSON 输出参数不合法。");
        return 0;
    }
    file = fopen(path, "w");
    if (!file) {
        if (error && error_size) snprintf(error, error_size, "无法写入 JSON：%s", path);
        return 0;
    }
    fprintf(file, "{\n");
    fprintf(file, "  \"schema_version\": \"1.2\",\n");
    fprintf(file, "  \"module\": \"nozzle\",\n");
    fprintf(file, "  \"model\": \"%s\",\n", nozzle_model_code(r->model));
    fprintf(file, "  \"model_label\": \"%s\",\n", nozzle_model_name(r->model));
    fprintf(file, "  \"throat_mode\": \"%s\",\n", nozzle_throat_mode_code(r->throat_mode));
    fprintf(file, "  \"section\": {\"spec\": \"%s\", \"label\": \"%s\", \"value\": ",
            nozzle_section_spec_code(r->spec), nozzle_section_spec_name(r->spec));
    jnum(file, r->spec_value);
    fprintf(file, "},\n");

    fprintf(file, "  \"chamber\": {\n");
    fprintf(file, "    \"p_c_Pa\": "); jnum(file, r->p_c); fprintf(file, ",\n");
    fprintf(file, "    \"T_c_K\": "); jnum(file, r->T_c); fprintf(file, ",\n");
    fprintf(file, "    \"h_c_J_kg\": "); jnum(file, r->h_c); fprintf(file, ",\n");
    fprintf(file, "    \"s_c_J_kgK\": "); jnum(file, r->s_c); fprintf(file, ",\n");
    fprintf(file, "    \"gamma_c\": "); jnum(file, r->gamma_c); fprintf(file, ",\n");
    fprintf(file, "    \"R_m_c\": "); jnum(file, r->R_m_c); fprintf(file, ",\n");
    fprintf(file, "    \"Mw_c_kg_mol\": "); jnum(file, r->Mw_c); fprintf(file, ",\n");
    fprintf(file, "    \"temperature_evaluations\": %d,\n", r->chamber_temperature_evaluations);
    fprintf(file, "    \"brent_iterations\": %d,\n", r->chamber_brent_iterations);
    fprintf(file, "    \"equilibrium_iterations\": %d,\n", r->chamber_equilibrium_iterations);
    fprintf(file, "    \"phase_iterations\": %d\n", r->chamber_phase_iterations);
    fprintf(file, "  },\n");

    jsection(file, "  ", "throat", &r->throat, 0);
    jsection(file, "  ", "exit", &r->exit_section, 0);

    fprintf(file, "  \"performance\": {\n");
    fprintf(file, "    \"c_star_m_s\": "); jnum(file, r->perf.c_star); fprintf(file, ",\n");
    fprintf(file, "    \"u_e_m_s\": "); jnum(file, r->perf.u_e); fprintf(file, ",\n");
    fprintf(file, "    \"Isp_m_s\": "); jnum(file, r->perf.I_sp); fprintf(file, ",\n");
    fprintf(file, "    \"Isp_vac_m_s\": "); jnum(file, r->perf.I_sp_vac); fprintf(file, ",\n");
    fprintf(file, "    \"C_F\": "); jnum(file, r->perf.C_F); fprintf(file, ",\n");
    fprintf(file, "    \"C_F_vac\": "); jnum(file, r->perf.C_F_vac); fprintf(file, ",\n");
    fprintf(file, "    \"F_N\": "); jnum(file, r->perf.F); fprintf(file, ",\n");
    fprintf(file, "    \"F_vac_N\": "); jnum(file, r->perf.F_vac); fprintf(file, ",\n");
    fprintf(file, "    \"m_dot_kg_s\": "); jnum(file, r->perf.m_dot); fprintf(file, ",\n");
    fprintf(file, "    \"p_a_Pa\": "); jnum(file, r->perf.p_a); fprintf(file, ",\n");
    fprintf(file, "    \"A_t_m2\": "); jnum(file, r->perf.A_t); fprintf(file, ",\n");
    fprintf(file, "    \"A_e_m2\": "); jnum(file, r->perf.A_e); fprintf(file, ",\n");
    fprintf(file, "    \"d_t_m\": "); jnum(file, r->perf.d_t); fprintf(file, ",\n");
    fprintf(file, "    \"d_e_m\": "); jnum(file, r->perf.d_e); fprintf(file, ",\n");
    fprintf(file, "    \"eps\": "); jnum(file, r->perf.eps); fprintf(file, ",\n");
    fprintf(file, "    \"gamma_avg\": "); jnum(file, r->perf.gamma_avg); fprintf(file, ",\n");
    fprintf(file, "    \"gamma_func\": "); jnum(file, r->perf.gamma_func); fprintf(file, ",\n");
    fprintf(file, "    \"c_star_gamma_m_s\": "); jnum(file, r->perf.c_star_gamma); fprintf(file, ",\n");
    fprintf(file, "    \"u_e_gamma_m_s\": "); jnum(file, r->perf.u_e_gamma); fprintf(file, ",\n");
    fprintf(file, "    \"Isp_vac_gamma_m_s\": "); jnum(file, r->perf.I_sp_vac_gamma); fprintf(file, ",\n");
    fprintf(file, "    \"C_F_vac_gamma\": "); jnum(file, r->perf.C_F_vac_gamma); fprintf(file, ",\n");
    fprintf(file, "    \"eps_gamma\": "); jnum(file, r->perf.eps_gamma); fprintf(file, "\n");
    fprintf(file, "  },\n");

    fprintf(file, "  \"checks\": {\n");
    fprintf(file, "    \"h_deviation_J_kg\": "); jnum(file, r->check_h); fprintf(file, ",\n");
    fprintf(file, "    \"s_deviation_J_kgK\": "); jnum(file, r->check_s); fprintf(file, ",\n");
    fprintf(file, "    \"mass_closure\": "); jnum(file, r->check_mass); fprintf(file, ",\n");
    fprintf(file, "    \"equilibrium_deviation_mol_kg\": ");
    jnum(file, r->check_n_deviation); fprintf(file, "\n");
    fprintf(file, "  },\n");

    fprintf(file, "  \"database\": {\n");
    fprintf(file, "    \"records\": %zu,\n", r->db_records);
    fprintf(file, "    \"species\": %zu,\n", r->db_species);
    fprintf(file, "    \"merged_groups\": %zu,\n", r->db_merged_groups);
    fprintf(file, "    \"case_collisions\": %zu,\n", r->db_case_collisions);
    fprintf(file, "    \"path\": \"%s\"\n", r->db_path);
    fprintf(file, "  },\n");

    fprintf(file, "  \"product_properties\": {\n");
    jproduct(file, "chamber", &r->chamber_products, 0);
    jproduct(file, "exit", &r->exit_products, 1);
    fprintf(file, "  },\n");

    fprintf(file, "  \"profile\": [\n");
    for (i = 0; i < r->n_profile; ++i) {
        const NozzleSection *s = &r->profile[i];
        fprintf(file, "    {\"i\": %d, \"p_Pa\": ", i); jnum(file, s->p);
        fprintf(file, ", \"T_K\": "); jnum(file, s->T);
        fprintf(file, ", \"u_m_s\": "); jnum(file, s->u);
        fprintf(file, ", \"Ma\": "); jnum(file, s->Ma);
        fprintf(file, ", \"gamma\": "); jnum(file, s->gamma);
        fprintf(file, ", \"rho_kg_m3\": "); jnum(file, s->rho);
        fprintf(file, ", \"h_J_kg\": "); jnum(file, s->h);
        fprintf(file, ", \"s_J_kgK\": "); jnum(file, s->s);
        fprintf(file, ", \"a_m_s\": "); jnum(file, s->a);
        fprintf(file, ", \"G_kg_m2s\": "); jnum(file, s->G);
        fprintf(file, ", \"eps\": "); jnum(file, s->eps);
        fprintf(file, ", \"Y_c\": "); jnum(file, s->Y_c);
        fprintf(file, "}%s\n", (i + 1 < r->n_profile) ? "," : "");
    }
    fprintf(file, "  ],\n");

    fprintf(file, "  \"composition\": [\n");
    first = 1;
    for (i = 0; i < (int)r->N; ++i) {
        if (!r->balance || r->n_exit[i] <= 1e-9) continue;
        fprintf(file, "%s    {\"name\": \"%s\", \"phase\": \"%s\", \"n_mol_kg\": ",
                first ? "" : ",\n", r->balance->names[i],
                (i < (int)r->L) ? "condensed" : "gas");
        jnum(file, r->n_exit[i]);
        fprintf(file, "}");
        first = 0;
    }
    fprintf(file, "\n  ]\n");
    fprintf(file, "}\n");
    fclose(file);
    if (error && error_size) error[0] = '\0';
    return 1;
}
