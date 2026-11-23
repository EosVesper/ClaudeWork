/* 文件功能：准备标准态物性并评价混合物的吉布斯目标函数。
 * 输入：候选物种、温度、标准压强、实际压强和组成。
 * 输出：标准态数据、目标函数值和错误信息。 */  
#include "gibbs_objective.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* 功能：把目标函数错误写入缓冲区。
 * 输入：error、size、message。
 * 输出：返回失败值 0。 */
static int fail(char *error, size_t size, const char *message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

/* 功能：从 NASA 物性准备指定温度的标准态化学势和焓熵。
 * 输入：b、T、p_0、out、error、size。
 * 输出：成功返回 1 并写入 GibbsStandard。 */
int gibbs_prepare(const MassBalance *b, double T, double p_0,
                   GibbsStandard *out, char *error, size_t size)
{
    GibbsStandard *s;
    size_t j;
    double entropy_shift;
    if (!b || !out || b->L >= b->N || b->N == 0 || b->N > BALANCE_MAX_PRODUCTS ||
        !isfinite(T) || T <= 0 || !isfinite(p_0) || p_0 <= 0)
        return fail(error,size,"Invalid state or non-gas model in gibbs_prepare.");
    s = (GibbsStandard *)calloc(1,sizeof(*s));
    if (!s) return fail(error,size,"Cannot allocate Gibbs standard properties.");
    s->N=b->N;s->L=b->L;s->T=T;s->p_0=p_0;

    entropy_shift=THERMO_R0*(log(p_0)-log(THERMO_NASA_GAS_P0));
    for (j=0;j<b->N;++j) {
        ThermoResult property;
        strcpy(s->names[j],b->names[j]);

        if (b->range_count[j]>0 && ((j<b->L)!=(b->nasa_phase[j]!=0))) {
            free(s);return fail(error,size,"Phase metadata/order mismatch.");
        }
        s->available[j]=mass_balance_species_available(b,j,T);
        if (j<b->L && !s->available[j]) {
            s->H_m0[j]=s->S_m0[j]=s->mu_0[j]=s->y[j]=NAN;
            continue;
        }
        property=thermo_h(b->names[j],T);
        if (!isfinite(property.H) || !isfinite(property.S)) {
            if (error && size) snprintf(error,size,"Properties for %s: %s",
                                       b->names[j],thermo_last_error());
            free(s);
            return 0;
        }
        strcpy(s->names[j],b->names[j]);
        s->H_m0[j]=property.H;
        s->S_m0[j]=property.S-(j<b->L ? 0 : entropy_shift);
        s->mu_0[j]=s->H_m0[j]-T*s->S_m0[j];
        s->y[j]=-s->mu_0[j]/(THERMO_R0*T);
        if (!isfinite(s->mu_0[j]) || !isfinite(s->y[j])) {
            free(s);
            return fail(error,size,"Non-finite standard chemical potential.");
        }
    }
    *out=*s;
    free(s);
    if (error && size) error[0]='\0';
    return 1;
}

/* 功能：按给定组分和压强计算吉布斯目标函数。
 * 输入：s、p、n、out、error、size。
 * 输出：成功返回 1 并写入 GibbsEvaluation。 */
int gibbs_evaluate(const GibbsStandard *s, double p, const double *n,
                    GibbsResult *out, char *error, size_t size)
{
    GibbsResult result={0};
    size_t j;
    double log_pressure;
    if (!s || !n || !out || s->N == 0 || s->N > BALANCE_MAX_PRODUCTS || s->L>=s->N ||
        !isfinite(s->T) || s->T <= 0 || !isfinite(s->p_0) || s->p_0 <= 0 ||
        !isfinite(p) || p <= 0)
        return fail(error,size,"Invalid Gibbs state; pressure must be absolute and positive (Pa).");
    for (j=0;j<s->N;++j) {
        if (!isfinite(n[j]) || n[j]<0)
            return fail(error,size,"Invalid composition or standard properties.");
        if (j<s->L && !s->available[j]) {
            if (n[j]!=0) return fail(error,size,"Nonzero amount of unavailable condensed phase.");
            continue;
        }
        if (!isfinite(s->y[j])) return fail(error,size,"Invalid standard potential.");
        if (j>=s->L) result.n_g+=n[j];
    }
    if (!isfinite(result.n_g) || result.n_g<=0)
        return fail(error,size,"Total gas amount must be finite and positive.");
    log_pressure=log(p)-log(s->p_0);
    for (j=0;j<s->N;++j) {

        if (n[j]>0) {
            result.Phi_j[j]=-n[j]*s->y[j];
            if (j>=s->L) result.Phi_j[j]+=n[j]*(log(n[j])-log(result.n_g)+log_pressure);
        }
        result.Phi+=result.Phi_j[j];
    }
    result.g=THERMO_R0*s->T*result.Phi;
    if (!isfinite(result.Phi) || !isfinite(result.g))
        return fail(error,size,"Gibbs objective overflow.");
    *out=result;
    if (error && size) error[0]='\0';
    return 1;
}
