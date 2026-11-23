/* 文件功能：由推进剂组元组成和配比建立假定化学式。
 * 输入：元素信息、组元质量分数或质量流量。
 * 输出：元素质量分数与原子摩尔数 N_k。 */ 
#ifndef PROPELLANT_FORMULA_H
#define PROPELLANT_FORMULA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHEM_MAX_ELEMENTS 16

typedef struct {
    char symbol[3];
    double A_r;
} ChemicalElement;

typedef struct {
    size_t M;
    ChemicalElement elements[CHEM_MAX_ELEMENTS];
    double g_p[CHEM_MAX_ELEMENTS];
    double N_k[CHEM_MAX_ELEMENTS];
} PropellantFormula;

/* 功能：由组元元素质量分数及质量流量建立假定化学式。
 * 输入：elements、M、g、m_dot、J、out、error、error_size。
 * 输出：成功返回 1 并写入 PropellantFormula。 */
int propellant_from_mass_flows(
    const ChemicalElement *elements, size_t M,
    const double *g, const double *m_dot, size_t J,
    PropellantFormula *out, char *error, size_t error_size);

/* 功能：由组元元素质量分数及组元质量分数建立假定化学式。
 * 输入：elements、M、g、q、J、out、error、error_size。
 * 输出：成功返回 1 并写入 PropellantFormula。 */
int propellant_from_mass_fractions(
    const ChemicalElement *elements, size_t M,
    const double *g, const double *q, size_t J,
    PropellantFormula *out, char *error, size_t error_size);

/* 功能：核对推进剂元素质量分数与 N_k 的一致性。
 * 输入：formula、error、error_size。
 * 输出：成功返回 1；失败写入错误信息。 */
int propellant_validate(const PropellantFormula *formula,
                        char *error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
