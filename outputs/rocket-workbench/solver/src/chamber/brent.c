/* 文件功能：在给定区间内用 Brent 法求连续标量方程的根。
 * 输入：函数回调、异号区间端点及容差。
 * 输出：根的位置、残差和求解状态。 */ 
#include "brent.h"
#include <math.h>
#include <string.h>

/* 功能：判断两端函数值是否异号。
 * 输入：a、double b) { return (a<0 && b>0)||(a>0 && b<0。
 * 输出：返回布尔判断值。 */
static int opposite(double a,double b) { return (a<0 && b>0)||(a>0 && b<0); }

/* 功能：交换两个标量。
 * 输入：a、b。
 * 输出：通过指针更新两个输入值。 */
static void swap(double *a,double *b) { double t=*a;*a=*b;*b=t; }

/* 功能：用 Brent 法在异号区间中寻找零点。
 * 输入：fn、ctx、a、b、xtol、ftol、limit、out。
 * 输出：返回 RootStatus，并写入 RootResult。 */
RootStatus brent_solve(RootFunction fn,void *ctx,double a,double b,
                       double xtol,double ftol,int limit,RootResult *out)
{
    double fa,fb,c,fc,d;int bisected=1,i;
    if (!out) return ROOT_INVALID;
    memset(out,0,sizeof(*out));out->x=out->f=NAN;
    if (!fn || !isfinite(a)||!isfinite(b)||a==b || !isfinite(xtol)||xtol<=0 ||
        !isfinite(ftol)||ftol<0 || limit<1) return ROOT_INVALID;
    out->low=fmin(a,b);out->high=fmax(a,b);
    ++out->evaluations;
    if (!fn(a,ctx,&fa)||!isfinite(fa)) return ROOT_CALLBACK;
    ++out->evaluations;
    if (!fn(b,ctx,&fb)||!isfinite(fb)) return ROOT_CALLBACK;
    if (fabs(fa)<fabs(fb)) {swap(&a,&b);swap(&fa,&fb);}
    out->x=b;out->f=fb;
    if (fb==0) return ROOT_OK;
    if (!opposite(fa,fb)) return ROOT_NOT_BRACKETED;
    c=a;fc=fa;d=c;
    for (i=0;i<limit;++i) {
        double s,fs,bound=a+(b-a)*0.25;
        int reject;
        out->x=b;out->f=fb;out->low=fmin(a,b);out->high=fmax(a,b);
        if (fabs(fb)<=ftol && fabs(b-a)<=xtol) return ROOT_OK;
        if (fa!=fc && fb!=fc) {
            s=a*(fb/(fa-fb))*(fc/(fa-fc))+
              b*(fa/(fb-fa))*(fc/(fb-fc))+
              c*(fa/(fc-fa))*(fb/(fc-fb));
        } else s=b-(fb/(fb-fa))*(b-a);
        reject=!isfinite(s)||s<=fmin(bound,b)||s>=fmax(bound,b);
        if (bisected) reject=reject||fabs(s-b)>=fabs(b-c)/2||fabs(b-c)<xtol;
        else reject=reject||fabs(s-b)>=fabs(c-d)/2||fabs(c-d)<xtol;
        if (reject) {s=a+(b-a)/2;bisected=1;++out->bisections;}
        else {bisected=0;++out->interpolations;}
        if (s==a || s==b || !isfinite(s)) return ROOT_STAGNATION;
        ++out->evaluations;out->iterations=i+1;
        if (!fn(s,ctx,&fs)||!isfinite(fs)) return ROOT_CALLBACK;
        if (fs==0) {out->x=s;out->f=fs;out->low=out->high=s;return ROOT_OK;}
        d=c;c=b;fc=fb;
        if (opposite(fa,fs)) {b=s;fb=fs;} else {a=s;fa=fs;}
        if (fabs(fa)<fabs(fb)) {swap(&a,&b);swap(&fa,&fb);}
    }
    out->x=b;out->f=fb;out->low=fmin(a,b);out->high=fmax(a,b);
    return fabs(fb)<=ftol && fabs(b-a)<=xtol ? ROOT_OK : ROOT_LIMIT;
}
