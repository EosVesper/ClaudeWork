/* 文件功能：按给定组成求混合产物焓与熵。
 * 输入：标准态物性、组成和绝对压强 Pa。 
 * 输出：每千克推进剂的焓 J/kg 与熵 J/(kg K)。 */ 
#include "mixture_properties.h"
#include <math.h>

/* 功能：按组成和压强求气相与凝相产物的总比熵。
 * 输入：s、p、n、S、e、size。
 * 输出：成功返回 1 并写入 J/(kg K)。 */
int mixture_entropy(const GibbsStandard *s,double p,const double *n,double *S,char *e,size_t size)
{
    size_t j;double ng=0,sum=0,correction=0,logp;
    if(!s||!n||!S||!s->N||s->N>BALANCE_MAX_PRODUCTS||s->L>s->N||
       !isfinite(p)||p<=0||!isfinite(s->p_0)||s->p_0<=0){
        if(e&&size)snprintf(e,size,"Invalid entropy arguments or pressure.");
        return 0;
    }
    for(j=0;j<s->N;++j){
        if(!isfinite(n[j])||n[j]<0){if(e&&size)snprintf(e,size,"Invalid species amount.");return 0;}
        if(j>=s->L)ng+=n[j];
    }
    if(!isfinite(ng)){if(e&&size)snprintf(e,size,"Gas amount overflow.");return 0;}
    logp=log(p)-log(s->p_0);
    for(j=0;j<s->N;++j){
        double molar,term,y,t;
        if(n[j]==0)continue;
        if(!s->available[j]||!isfinite(s->S_m0[j])){
            if(e&&size)snprintf(e,size,"Missing entropy of present species.");
            return 0;
        }
        molar=s->S_m0[j];
        if(j>=s->L)molar-=THERMO_R0*(log(n[j])-log(ng)+logp);
        term=n[j]*molar;y=term-correction;t=sum+y;correction=(t-sum)-y;sum=t;
    }
    if(!isfinite(sum)){if(e&&size)snprintf(e,size,"Mixture entropy overflow.");return 0;}
    *S=sum;if(e&&size)e[0]=0;return 1;
}

/* 功能：把混合物物性错误写入缓冲区。
 * 输入：e、n、e、n、"%s"、s。
 * 输出：返回失败值 0。 */
static int fail(char *e,size_t n,const char *s) {if(e&&n)snprintf(e,n,"%s",s);return 0;}

/* 功能：按组成求气相与凝相产物的总比焓。
 * 输入：s、n、H、e、size。
 * 输出：成功返回 1 并写入 J/kg。 */
int mixture_enthalpy(const GibbsStandard *s,const double *n,double *H,char *e,size_t size)
{
    size_t j;double sum=0,correction=0;
    if(!s||!n||!H||!s->N||s->N>BALANCE_MAX_PRODUCTS||s->L>s->N)
        return fail(e,size,"Invalid enthalpy arguments.");
    for(j=0;j<s->N;++j){
        double term,y,t;
        if(!isfinite(n[j])||n[j]<0) return fail(e,size,"Invalid product amount.");
        if(n[j]==0) continue;
        if(!s->available[j]||!isfinite(s->H_m0[j])) return fail(e,size,"Missing enthalpy of present species.");
        term=n[j]*s->H_m0[j];y=term-correction;t=sum+y;correction=(t-sum)-y;sum=t;
    }
    if(!isfinite(sum)) return fail(e,size,"Nonfinite mixture enthalpy.");
    *H=sum;if(e&&size)e[0]=0;return 1;
}
