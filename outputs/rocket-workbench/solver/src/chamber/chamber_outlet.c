/* 文件功能：将燃烧室求解结果整理为可供后续模块调用的完整出口状态。
 * 输入：守恒模型、能量求解结果或目标焓及可选总质量流量。
 * 输出：出口状态、全部候选物种及其编号、相态和摩尔质量。 */
#include "chamber_outlet.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 功能：标记出口状态无效并记录错误。
 * 输入：out、error、size、message。
 * 输出：返回失败值 0。 */
static int fail(ChamberOutlet *out, char *error, size_t size, const char *message)
{
    if (out) out->valid = 0;
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

/* 功能：把已收敛的能量结果与完整物种元数据复制为独立的出口状态；m_dot_total 单位 kg/s，0 表示未知。
 * 输入：b、energy、m_dot_total、out、error、size。
 * 输出：成功返回 1，out 包含全部候选物种且 valid=1；失败返回 0、valid=0。 */
int chamber_outlet_from_energy(const MassBalance *b, const EnergyResult *energy,
                               double m_dot_total, ChamberOutlet *out,
                               char *error, size_t size)
{
    size_t j, k;
    double mass_sum = 0.0;
    if (!out) return fail(NULL, error, size, "Null chamber outlet.");
    memset(out, 0, sizeof(*out));
    out->p_c = out->T_c = out->h_c = out->s_c = NAN;
    out->energy_status = energy ? energy->status : ENERGY_INVALID;
    out->equilibrium_status = energy ? energy->equilibrium.status : EQ_INVALID_INPUT;
    if (!b || !energy || b->M == 0 || b->M > CHEM_MAX_ELEMENTS ||
        b->N == 0 || b->N > BALANCE_MAX_PRODUCTS || b->L >= b->N ||
        energy->status != ENERGY_OK || energy->equilibrium.status != EQ_OK ||
        !energy->equilibrium.converged || !isfinite(energy->p) || energy->p <= 0 ||
        !isfinite(energy->T) || energy->T <= 0 ||
        !isfinite(energy->H_product) || !isfinite(energy->S_product) ||
        !isfinite(energy->residual) ||
        !isfinite(m_dot_total) || m_dot_total < 0)
        return fail(out, error, size, "Invalid or unconverged chamber state.");

    out->M = b->M;
    out->N = b->N;
    out->L = b->L;
    out->p_c = energy->p;
    out->T_c = energy->T;
    out->h_c = energy->H_product;
    out->s_c = energy->S_product;
    out->p_0 = THERMO_GAS_P0;
    out->m_dot_total = m_dot_total;
    out->energy_residual = energy->residual;
    out->temperature_evaluations = energy->evaluations;
    out->brent_iterations = energy->root.iterations;
    out->equilibrium_iterations = energy->equilibrium.iterations;
    out->phase_iterations = energy->equilibrium.phase_iterations;
    for (k = 0; k < b->M; ++k) {
        out->elements[k] = b->elements[k];
        out->N_k[k] = b->N_k[k];
        if (!isfinite(b->N_k[k]) || b->N_k[k] <= 0 ||
            !isfinite(b->elements[k].A_r) || b->elements[k].A_r <= 0)
            return fail(out, error, size, "Invalid element inventory.");
    }
    for (j = 0; j < b->N; ++j) {
        ChamberOutletSpecies *species = &out->species[j];
        double molar_mass = 0.0;
        species->j = j + 1;
        memcpy(species->name, b->names[j], sizeof(species->name));
        species->name[NASA_NAME_LENGTH] = '\0';
        species->phase = j < b->L ? CHAMBER_PHASE_CONDENSED : CHAMBER_PHASE_GAS;
        species->n = energy->equilibrium.n[j];
        if (!isfinite(species->n) || species->n < 0 || species->name[0] == '\0')
            return fail(out, error, size, "Invalid species amount or name.");
        for (k = 0; k < b->M; ++k) {
            double atoms = b->A[k][j];
            if (!isfinite(atoms) || atoms < 0)
                return fail(out, error, size, "Invalid species elemental composition.");
            species->A_k[k] = atoms;
            molar_mass += atoms * b->elements[k].A_r * 1e-3;
        }
        if (!isfinite(molar_mass) || molar_mass <= 0)
            return fail(out, error, size, "Invalid species molar mass.");
        species->molar_mass = molar_mass;
        mass_sum += species->n * molar_mass;
        if (j >= b->L) out->n_g += species->n;
    }
    out->mass_closure = mass_sum;
    if (!isfinite(out->n_g) || out->n_g <= 0 ||
        !isfinite(mass_sum) || fabs(mass_sum - 1.0) > 1e-6)
        return fail(out, error, size, "Chamber outlet mass closure failed.");
    out->valid = 1;
    if (error && size) error[0] = '\0';
    return 1;
}

/* 功能：由守恒模型、绝对压强 p(Pa) 和目标焓 H_in(J/kg) 求完整出口；m_dot_total 为 kg/s，options 为空时搜温 1500～5000 K。
 * 输入：balance、p、H_in、m_dot_total、options、out、error、error_size。
 * 输出：成功返回 1，out 包含 p_c、T_c、h_c、s_c 及全部 n_j；失败返回 0、valid=0。 */
int chamber_outlet_solve_hp(const MassBalance *balance, double p, double H_in,
                            double m_dot_total, const EnergyOptions *options,
                            ChamberOutlet *out, char *error, size_t error_size)
{
    EnergyOptions defaults;
    EnergyResult *energy;
    EnergyStatus status;
    int ok;
    if (!out) return fail(NULL, error, error_size, "Null chamber outlet.");
    memset(out, 0, sizeof(*out));
    out->p_c = out->T_c = out->h_c = out->s_c = NAN;
    out->energy_status = ENERGY_INVALID;
    out->equilibrium_status = EQ_INVALID_INPUT;
    if (!balance || !isfinite(m_dot_total) || m_dot_total < 0)
        return fail(out, error, error_size, "Invalid chamber input or mass flow.");
    defaults = energy_default_options(1500, 5000);
    energy = (EnergyResult *)calloc(1, sizeof(*energy));
    if (!energy) return fail(out, error, error_size, "Cannot allocate energy result.");
    status = chamber_solve_hp(balance, p, H_in, options ? options : &defaults,
                              energy, error, error_size);
    if (status != ENERGY_OK) {
        out->energy_status = status;
        out->equilibrium_status = energy->equilibrium.status;
        free(energy);
        return 0;
    }
    ok = chamber_outlet_from_energy(balance, energy, m_dot_total,
                                    out, error, error_size);
    free(energy);
    return ok;
}
