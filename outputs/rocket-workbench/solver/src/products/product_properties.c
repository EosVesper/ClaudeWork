/* 对队友产物物性程序的工程化整理：复用候选产物编号与 NASA 九系数物性，
 * 根据实际平衡组成计算冻结/平衡比热，并从 trans.inp 估计气相输运性质。
 * 输入输出见 product_properties.h。 */
#include "product_properties.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thermo_properties.h"

#define PRODUCT_MAX_ACTIVE 96
#define PRODUCT_MAX_MATRIX (PRODUCT_MAX_ACTIVE + CHEM_MAX_ELEMENTS + 1)
#define PRODUCT_MAX_TRANSPORT 512
#define PRODUCT_MAX_RANGES 4

typedef struct {
    double low, high, coeff[4];
} TransportRange;

typedef struct {
    char name[32];
    TransportRange viscosity[PRODUCT_MAX_RANGES];
    TransportRange conductivity[PRODUCT_MAX_RANGES];
    int n_viscosity, n_conductivity;
} TransportSpecies;

static TransportSpecies transport_species[PRODUCT_MAX_TRANSPORT];
static int transport_count = 0;
static char transport_loaded_path[512];

static int fail(char *error, size_t size, const char *message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

/* 带列主元的消元。每次从原始矩阵复制，避免第二个右端项误用已消元的矩阵。 */
static int linear_solve(double source[PRODUCT_MAX_MATRIX][PRODUCT_MAX_MATRIX],
                        const double *rhs, int count, double *solution)
{
    double matrix[PRODUCT_MAX_MATRIX][PRODUCT_MAX_MATRIX];
    double b[PRODUCT_MAX_MATRIX];
    int i, j, k, pivot;
    for (i = 0; i < count; ++i) {
        b[i] = rhs[i];
        for (j = 0; j < count; ++j) matrix[i][j] = source[i][j];
    }
    for (i = 0; i < count; ++i) {
        double scale = 0.0;
        pivot = i;
        for (k = i; k < count; ++k)
            if (fabs(matrix[k][i]) > fabs(matrix[pivot][i])) pivot = k;
        for (j = i; j < count; ++j)
            if (fabs(matrix[pivot][j]) > scale) scale = fabs(matrix[pivot][j]);
        if (!isfinite(scale) || scale == 0.0 ||
            fabs(matrix[pivot][i]) < 1e-13 * scale) return 0;
        if (pivot != i) {
            for (j = i; j < count; ++j) {
                double v = matrix[i][j];
                matrix[i][j] = matrix[pivot][j];
                matrix[pivot][j] = v;
            }
            {
                double v = b[i]; b[i] = b[pivot]; b[pivot] = v;
            }
        }
        for (k = i + 1; k < count; ++k) {
            double factor = matrix[k][i] / matrix[i][i];
            for (j = i; j < count; ++j) matrix[k][j] -= factor * matrix[i][j];
            b[k] -= factor * b[i];
        }
    }
    for (i = count - 1; i >= 0; --i) {
        double v = b[i];
        for (j = i + 1; j < count; ++j) v -= matrix[i][j] * solution[j];
        solution[i] = v / matrix[i][i];
        if (!isfinite(solution[i])) return 0;
    }
    return 1;
}

/* 在已确定的活动凝相集合内，对平衡方程按 ln T 和 ln p 求导。 */
static void equilibrium_derivatives(const MassBalance *balance, const double *n,
                                    const double *H, const double *Cp, double T,
                                    double total_mass, double n_g,
                                    ProductProperties *out)
{
    int active[PRODUCT_MAX_ACTIVE], count = 0, dim, i, k;
    double A[PRODUCT_MAX_MATRIX][PRODUCT_MAX_MATRIX] = {{0}};
    double rhs_T[PRODUCT_MAX_MATRIX] = {0}, rhs_p[PRODUCT_MAX_MATRIX] = {0};
    double sol_T[PRODUCT_MAX_MATRIX] = {0}, sol_p[PRODUCT_MAX_MATRIX] = {0};
    double cp_sum = 0.0, h_change = 0.0, active_gas = 0.0;
    double ug_T, ug_p, denominator;
    size_t j;

    for (j = 0; j < balance->N; ++j) {
        double cutoff = (j < balance->L) ? 1e-12 : fmax(1e-14, n_g * 1e-12);
        if (n[j] <= cutoff) continue;
        if (count == PRODUCT_MAX_ACTIVE) {
            snprintf(out->equilibrium_message, sizeof(out->equilibrium_message),
                     "非零组分超过 %d 种，未计算平衡比热。", PRODUCT_MAX_ACTIVE);
            return;
        }
        active[count++] = (int)j;
        if (j >= balance->L) active_gas += n[j];
    }
    if (count == 0 || active_gas <= 0 || balance->M == 0) {
        snprintf(out->equilibrium_message, sizeof(out->equilibrium_message),
                 "活动组分或气相总摩尔数为空。");
        return;
    }
    dim = count + (int)balance->M + 1;
    if (dim > PRODUCT_MAX_MATRIX) return;

    for (i = 0; i < count; ++i) {
        j = (size_t)active[i];
        if (j >= balance->L) {
            A[i][i] = 1.0;
            A[i][dim - 1] = -1.0;
            rhs_p[i] = -1.0;
        }
        for (k = 0; k < (int)balance->M; ++k)
            A[i][count + k] = -balance->A[k][j];
        rhs_T[i] = H[j] / (THERMO_R0 * T);
    }
    for (k = 0; k < (int)balance->M; ++k) {
        int row = count + k;
        for (i = 0; i < count; ++i) {
            j = (size_t)active[i];
            A[row][i] = balance->A[k][j] *
                        ((j >= balance->L) ? n[j] : 1.0);
        }
    }
    for (i = 0; i < count; ++i) {
        j = (size_t)active[i];
        if (j >= balance->L) A[dim - 1][i] = n[j];
    }
    A[dim - 1][dim - 1] = -active_gas;

    if (!linear_solve(A, rhs_T, dim, sol_T) ||
        !linear_solve(A, rhs_p, dim, sol_p)) {
        snprintf(out->equilibrium_message, sizeof(out->equilibrium_message),
                 "平衡导数方程组奇异；相变点附近的比热未给出。");
        return;
    }
    for (j = 0; j < balance->N; ++j) cp_sum += n[j] * Cp[j];
    for (i = 0; i < count; ++i) {
        j = (size_t)active[i];
        h_change += H[j] * sol_T[i] *
                    ((j >= balance->L) ? n[j] : 1.0) / T;
    }
    ug_T = sol_T[dim - 1];
    ug_p = sol_p[dim - 1];
    denominator = 1.0 - ug_p;
    if (denominator <= 1e-10 || !isfinite(denominator)) {
        snprintf(out->equilibrium_message, sizeof(out->equilibrium_message),
                 "平衡定容比热的体积导数不可用。");
        return;
    }
    out->cp_equilibrium = (cp_sum + h_change) / total_mass;
    out->cv_equilibrium = out->cp_equilibrium -
        out->R_m * (1.0 + ug_T) * (1.0 + ug_T) / denominator;
    if (!isfinite(out->cp_equilibrium) || !isfinite(out->cv_equilibrium) ||
        out->cp_equilibrium <= 0.0 || out->cv_equilibrium <= 0.0) {
        snprintf(out->equilibrium_message, sizeof(out->equilibrium_message),
                 "平衡比热不满足正值检查。");
        return;
    }
    out->gamma_equilibrium = out->cp_equilibrium / out->cv_equilibrium;
    out->equilibrium_valid = 1;
}

static void fix_fortran_exponents(char *line)
{
    char *p;
    for (p = line; *p; ++p)
        if ((*p == 'E' || *p == 'e') && p[1] == ' ') p[1] = '+';
}

/* NASA trans.inp 是定宽格式；正负号可直接充当相邻浮点数的分隔符。 */
static int transport_load(const char *path)
{
    FILE *file;
    char line[256];
    TransportSpecies *current = NULL;
    if (transport_loaded_path[0] && strcmp(path, transport_loaded_path) == 0)
        return transport_count > 0;
    file = fopen(path, "r");
    if (!file) return 0;
    transport_count = 0;
    transport_loaded_path[0] = '\0';
    while (fgets(line, sizeof(line), file)) {
        if (line[0] != ' ' && line[0] != '\t' && line[0] != '\n') {
            if (!strstr(line, "V") || !strstr(line, "C")) {
                current = NULL;
                continue;
            }
            if (transport_count == PRODUCT_MAX_TRANSPORT) break;
            current = &transport_species[transport_count++];
            memset(current, 0, sizeof(*current));
            if (sscanf(line, "%31s", current->name) != 1) current = NULL;
        } else if (current && line[0] == ' ' &&
                   (line[1] == 'V' || line[1] == 'C')) {
            double low, high, c[4];
            TransportRange *range = NULL;
            fix_fortran_exponents(line);
            if (sscanf(line + 2, "%lf %lf %lf %lf %lf %lf",
                       &low, &high, &c[0], &c[1], &c[2], &c[3]) != 6)
                continue;
            if (line[1] == 'V' && current->n_viscosity < PRODUCT_MAX_RANGES)
                range = &current->viscosity[current->n_viscosity++];
            if (line[1] == 'C' && current->n_conductivity < PRODUCT_MAX_RANGES)
                range = &current->conductivity[current->n_conductivity++];
            if (range) {
                range->low = low;
                range->high = high;
                memcpy(range->coeff, c, sizeof(c));
            }
        }
    }
    fclose(file);
    if (transport_count > 0)
        snprintf(transport_loaded_path, sizeof(transport_loaded_path), "%s", path);
    return transport_count > 0;
}

static const TransportSpecies *transport_find(const char *name)
{
    int i;
    for (i = 0; i < transport_count; ++i)
        if (strcmp(transport_species[i].name, name) == 0)
            return &transport_species[i];
    return NULL;
}

static double transport_polynomial(const TransportRange *range, int count, double T)
{
    int i;
    for (i = 0; i < count; ++i) {
        const double *c = range[i].coeff;
        if (T >= range[i].low && T <= range[i].high)
            return exp(c[0] * log(T) + c[1] / T + c[2] / (T*T) + c[3]);
    }
    return NAN;
}

/* Wilke 型混合规则。输运数据仅对气相使用，Pr 采用气相质量比热。 */
static void transport_evaluate(const MassBalance *balance, const double *n,
                               const double *Cp, double T, double n_g,
                               const char *path, ProductProperties *out)
{
    int count = 0, i, j;
    double x[PRODUCT_MAX_ACTIVE], mu[PRODUCT_MAX_ACTIVE];
    double lambda[PRODUCT_MAX_ACTIVE], molecular_weight[PRODUCT_MAX_ACTIVE];
    double coverage = 0.0, gas_cp = 0.0, gas_mass = 0.0;
    size_t k;
    if (!transport_load(path)) {
        snprintf(out->transport_message, sizeof(out->transport_message),
                 "无法读取 trans.inp：%s", path);
        return;
    }
    for (k = balance->L; k < balance->N; ++k) {
        const TransportSpecies *species;
        double v, c;
        if (n[k] <= 0.0) continue;
        gas_cp += n[k] * Cp[k];
        gas_mass += n[k] * balance->molar_mass[k];
        if (n[k] / n_g < 1e-8) continue;
        species = transport_find(balance->names[k]);
        if (!species) continue;
        v = transport_polynomial(species->viscosity, species->n_viscosity, T);
        c = transport_polynomial(species->conductivity, species->n_conductivity, T);
        if (!isfinite(v) || !isfinite(c) || v <= 0.0 || c <= 0.0) continue;
        if (count == PRODUCT_MAX_ACTIVE) {
            snprintf(out->transport_message, sizeof(out->transport_message),
                     "气相组分超过输运混合规则的数量上限。");
            return;
        }
        x[count] = n[k] / n_g;
        mu[count] = v * 1e-7;             /* 微泊 -> Pa·s */
        lambda[count] = c * 1e-4;        /* 微瓦/(cm·K) -> W/(m·K) */
        molecular_weight[count] = balance->molar_mass[k];
        coverage += x[count];
        ++count;
    }
    out->transport_coverage = coverage;
    if (count == 0 || coverage < 0.999) {
        snprintf(out->transport_message, sizeof(out->transport_message),
                 "输运数据仅覆盖 %.4f 的气相摩尔分数，未输出混合输运性质。", coverage);
        return;
    }
    for (i = 0; i < count; ++i) x[i] /= coverage;
    for (i = 0; i < count; ++i) {
        double denom_mu = 0.0, denom_lambda = 0.0;
        for (j = 0; j < count; ++j) {
            double ratio = molecular_weight[j] / molecular_weight[i];
            double phi_mu = pow(1.0 + sqrt(mu[i] / mu[j]) * pow(ratio, 0.25), 2.0)
                          / sqrt(8.0 * (1.0 + 1.0 / ratio));
            double phi_lambda = pow(1.0 + sqrt(lambda[i] / lambda[j]) * pow(ratio, 0.25), 2.0)
                              / sqrt(8.0 * (1.0 + 1.0 / ratio));
            denom_mu += x[j] * phi_mu;
            denom_lambda += x[j] * phi_lambda;
        }
        out->mu += x[i] * mu[i] / denom_mu;
        out->conductivity += x[i] * lambda[i] / denom_lambda;
    }
    if (gas_mass > 0.0 && out->conductivity > 0.0)
        out->Pr = out->mu * (gas_cp / gas_mass) / out->conductivity;
    if (!isfinite(out->Pr) || out->Pr <= 0.0) {
        snprintf(out->transport_message, sizeof(out->transport_message),
                 "混合输运性质未通过正值检查。");
        return;
    }
    out->transport_valid = 1;
}

int product_properties_evaluate(const MassBalance *balance, const double *n,
                                double T, double p, const char *trans_path,
                                ProductProperties *out, char *error,
                                size_t error_size)
{
    double H[BALANCE_MAX_PRODUCTS] = {0}, Cp[BALANCE_MAX_PRODUCTS] = {0};
    double mass = 0.0, mass_gas = 0.0, mass_condensed = 0.0;
    double cp_sum = 0.0, n_g = 0.0;
    size_t j;
    if (!balance || !n || !out || balance->N == 0 ||
        balance->N > BALANCE_MAX_PRODUCTS || balance->L >= balance->N ||
        !isfinite(T) || T <= 0.0 || !isfinite(p) || p <= 0.0)
        return fail(error, error_size, "产物物性输入不合法。");
    memset(out, 0, sizeof(*out));
    for (j = 0; j < balance->N; ++j) {
        ThermoResult property;
        double m;
        if (!isfinite(n[j]) || n[j] < 0.0)
            return fail(error, error_size, "产物摩尔数不合法。");
        if (n[j] == 0.0) continue;
        property = thermo_h(balance->names[j], T);
        if (!isfinite(property.H) || !isfinite(property.Cp))
            return fail(error, error_size, thermo_last_error());
        H[j] = property.H;
        Cp[j] = property.Cp;
        m = n[j] * balance->molar_mass[j];
        mass += m;
        cp_sum += n[j] * Cp[j];
        if (j < balance->L) mass_condensed += m;
        else { mass_gas += m; n_g += n[j]; }
    }
    if (mass <= 0.0 || mass_gas <= 0.0 || n_g <= 0.0)
        return fail(error, error_size, "产物总质量或气相总量为零。");
    out->Y_c = mass_condensed / mass;
    out->M_gas = mass_gas / n_g;
    out->R_gas = THERMO_R0 / out->M_gas;
    out->R_m = THERMO_R0 * n_g / mass;
    out->cp_frozen = cp_sum / mass;
    out->cv_frozen = out->cp_frozen - out->R_m;
    if (!isfinite(out->cv_frozen) || out->cv_frozen <= 0.0)
        return fail(error, error_size, "冻结定容比热不合法。");
    out->gamma_frozen = out->cp_frozen / out->cv_frozen;
    out->valid = 1;

    equilibrium_derivatives(balance, n, H, Cp, T, mass, n_g, out);
    transport_evaluate(balance, n, Cp, T, n_g,
                       trans_path ? trans_path : "trans.inp", out);
    if (error && error_size) error[0] = '\0';
    return 1;
}
