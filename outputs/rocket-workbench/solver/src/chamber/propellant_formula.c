/* 文件功能：由推进剂组元组成和配比建立假定化学式。
 * 输入：元素信息、组元质量分数或质量流量。
 * 输出：元素质量分数与原子摩尔数 N_k。 */ 
#include "propellant_formula.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRACTION_TOL 1e-10

/* 功能：把推进剂配方错误写入缓冲区。
 * 输入：error、size、message。
 * 输出：返回失败值 0。 */
static int fail(char *error, size_t size, const char *message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

/* 功能：判断两个元素符号是否相同。
 * 输入：a、b。
 * 输出：返回布尔判断值。 */
static int same_element(const char *a, const char *b)
{
    return toupper((unsigned char)a[0]) == toupper((unsigned char)b[0]) &&
           toupper((unsigned char)a[1]) == toupper((unsigned char)b[1]);
}

/* 功能：验证元素表的符号、原子质量及去重情况。
 * 输入：elements、M、error、size。
 * 输出：成功返回 1；失败写入错误信息。 */
static int check_elements(const ChemicalElement *elements, size_t M,
                          char *error, size_t size)
{
    size_t k, i;
    if (!elements || M == 0 || M > CHEM_MAX_ELEMENTS)
        return fail(error, size, "Invalid element count or element table.");
    for (k = 0; k < M; ++k) {
        const char *s = elements[k].symbol;
        if (!isalpha((unsigned char)s[0]) ||
            (s[1] && !isalpha((unsigned char)s[1])) || s[2] != '\0' ||
            (toupper((unsigned char)s[0]) == 'E' && s[1] == '\0') ||
            !isfinite(elements[k].A_r) || elements[k].A_r <= 0)
            return fail(error, size, "Invalid element symbol or atomic weight; electrons are excluded.");
        for (i = 0; i < k; ++i)
            if (same_element(s, elements[i].symbol))
                return fail(error, size, "Duplicate element in element table.");
    }
    return 1;
}

/* 功能：核对推进剂元素质量分数与 N_k 的一致性。
 * 输入：f、error、size。
 * 输出：成功返回 1；失败写入错误信息。 */
int propellant_validate(const PropellantFormula *f, char *error, size_t size)
{
    size_t k;
    double mass_sum = 0.0;
    if (!f) return fail(error, size, "Null propellant formula.");
    if (!check_elements(f->elements, f->M, error, size)) return 0;
    for (k = 0; k < f->M; ++k) {
        double mass = f->N_k[k] * f->elements[k].A_r * 1e-3;
        if (!isfinite(f->g_p[k]) || f->g_p[k] <= 0 ||
            !isfinite(f->N_k[k]) || f->N_k[k] <= 0 ||
            !isfinite(mass) || fabs(mass - f->g_p[k]) > FRACTION_TOL)
            return fail(error, size, "Inconsistent g_p and N_k in propellant formula.");
        mass_sum += mass;
    }
    if (fabs(mass_sum - 1.0) > FRACTION_TOL)
        return fail(error, size, "Propellant formula does not represent 1 kg.");
    if (error && size) error[0] = '\0';
    return 1;
}

/* 功能：由组元元素质量分数及组元质量分数建立假定化学式。
 * 输入：elements、M、g、q、J、out、error、size。
 * 输出：成功返回 1 并写入 PropellantFormula。 */
int propellant_from_mass_fractions(
    const ChemicalElement *elements, size_t M,
    const double *g, const double *q, size_t J,
    PropellantFormula *out, char *error, size_t size)
{
    PropellantFormula result = {0};
    size_t j, k;
    double q_sum = 0.0;
    if (!g || !q || !out || J == 0 || J > (size_t)-1 / CHEM_MAX_ELEMENTS)
        return fail(error, size, "Invalid component input.");
    if (!check_elements(elements, M, error, size)) return 0;
    for (j = 0; j < J; ++j) {
        double sum_g = 0.0;
        if (!isfinite(q[j]) || q[j] < 0 || q[j] > 1)
            return fail(error, size, "Component mass fractions must be in [0, 1].");
        q_sum += q[j];
        for (k = 0; k < M; ++k) {
            double value = g[j*M + k];
            if (!isfinite(value) || value < 0 || value > 1)
                return fail(error, size, "Element mass fractions must be in [0, 1].");
            sum_g += value;
        }
        if (fabs(sum_g - 1.0) > FRACTION_TOL)
            return fail(error, size, "Element mass fractions of each component must sum to 1.");
    }
    if (fabs(q_sum - 1.0) > FRACTION_TOL)
        return fail(error, size, "Component mass fractions must sum to 1.");
    for (k = 0; k < M; ++k) {
        double g_p = 0.0;
        for (j = 0; j < J; ++j) g_p += q[j] * g[j*M + k];
        if (g_p > 0.0) {
            size_t row = result.M++;
            result.elements[row] = elements[k];
            result.g_p[row] = g_p;

            result.N_k[row] = g_p / (elements[k].A_r * 1e-3);
        }
    }
    if (!propellant_validate(&result, error, size)) return 0;
    *out = result;
    return 1;
}

/* 功能：由组元元素质量分数及质量流量建立假定化学式。
 * 输入：elements、M、g、m_dot、J、out、error、size。
 * 输出：成功返回 1 并写入 PropellantFormula。 */
int propellant_from_mass_flows(
    const ChemicalElement *elements, size_t M,
    const double *g, const double *m_dot, size_t J,
    PropellantFormula *out, char *error, size_t size)
{
    size_t j;
    double sum = 0.0, *q;
    int ok;
    if (!m_dot || J == 0 || J > (size_t)-1 / sizeof(double))
        return fail(error, size, "Invalid mass flow input.");
    for (j = 0; j < J; ++j) {
        if (!isfinite(m_dot[j]) || m_dot[j] < 0)
            return fail(error, size, "Mass flows must be finite and nonnegative (kg/s).");
        sum += m_dot[j];
    }
    if (!isfinite(sum) || sum <= 0)
        return fail(error, size, "Total mass flow must be finite and positive.");
    q = (double *)malloc(J * sizeof(*q));
    if (!q) return fail(error, size, "Cannot allocate component mass fractions.");
    for (j = 0; j < J; ++j) q[j] = m_dot[j] / sum;
    ok = propellant_from_mass_fractions(elements, M, g, q, J, out, error, size);
    free(q);
    return ok;
}
