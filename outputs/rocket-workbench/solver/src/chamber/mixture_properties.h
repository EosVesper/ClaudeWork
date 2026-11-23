/* 文件功能：按给定组成求混合产物焓与熵。
 * 输入：标准态物性、组成和绝对压强 Pa。
 * 输出：每千克推进剂的焓 J/kg 与熵 J/(kg K)。 */ 
#ifndef MIXTURE_PROPERTIES_H
#define MIXTURE_PROPERTIES_H
#include "gibbs_objective.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 功能：按组成求气相与凝相产物的总比焓。
 * 输入：s、n、H、error、error_size。
 * 输出：成功返回 1 并写入 J/kg。 */
int mixture_enthalpy(const GibbsStandard *s,const double *n,double *H,
                     char *error,size_t error_size);

/* 功能：按组成和压强求气相与凝相产物的总比熵。
 * 输入：s、p、n、S、error、error_size。
 * 输出：成功返回 1 并写入 J/(kg K)。 */
int mixture_entropy(const GibbsStandard *s,double p,const double *n,double *S,
                    char *error,size_t error_size);
#ifdef __cplusplus
}
#endif
#endif
