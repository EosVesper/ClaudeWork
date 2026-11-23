/* 文件功能：在给定绝对压强及目标焓下求燃烧室平衡温度。
 * 输入：元素守恒模型、压强 Pa、入口目标焓 J/kg 和求解选项。
 * 输出：温度 K、产物焓熵、平衡组成及求解状态。 */ 
#include "chamber_energy.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* 功能：把能量求解错误写入缓冲区。
 * 输入：e、n、e、n、"%s"、s。
 * 输出：返回失败值 0。 */
static int fail(char *e,size_t n,const char *s) {if(e&&n)snprintf(e,n,"%s",s);return 0;}

/* 功能：生成指定温度区间的能量求解默认选项。
 * 输入：low、high。
 * 输出：返回 EnergyOptions。 */
EnergyOptions energy_default_options(double low,double high)
{
    EnergyOptions o={0};o.T_min=low;o.T_max=high;o.T_tolerance=1e-5;o.H_tolerance=.01;
    o.scan_steps=20;o.max_iterations=100;o.equilibrium=equilibrium_default_options();return o;
}
typedef struct {
    const MassBalance *b;const EnergyOptions *o;double p,H_in,H,T;
    GibbsStandard s;EquilibriumResult eq;int calls;char error[256];
    EnergyTracePhase trace_phase;
} Context;

/* 功能：在试探温度下求平衡组成和焓残差。
 * 输入：T、ptr、f。
 * 输出：成功返回 1 并写入残差。 */
static int evaluate(double T,void *ptr,double *f)
{
    Context *c=(Context *)ptr;++c->calls;c->T=T;
    if(!gibbs_prepare(c->b,T,THERMO_GAS_P0,&c->s,c->error,sizeof(c->error)))return 0;
    if(equilibrium_solve(c->b,&c->s,c->p,NULL,&c->o->equilibrium,&c->eq,c->error,sizeof(c->error))!=EQ_OK)return 0;
    if(!mixture_enthalpy(&c->s,c->eq.n,&c->H,c->error,sizeof(c->error)))return 0;
    *f=c->H-c->H_in;
    if (!isfinite(*f)) return 0;
    if (c->o->trace)
        c->o->trace(c->o->trace_context,c->trace_phase,c->calls,T,*f);
    return 1;
}

/* 功能：检查所有气相候选物种是否覆盖搜索温区。
 * 输入：b、low、high。
 * 输出：返回覆盖判断结果。 */
static int gas_coverage(const MassBalance *b,double low,double high)
{
    size_t j;int r;
    for(j=b->L;j<b->N;++j){
        double covered=low;
        if(b->range_count[j]==0)continue;
        for(r=0;r<b->range_count[j];++r)
            if(b->T_low[j][r]<=covered && b->T_high[j][r]>covered)covered=b->T_high[j][r];
        if(covered<high)return 0;
    }
    return 1;
}

/* 功能：联立化学平衡和能量守恒求燃烧室温度；p 用 Pa，H_in 用 J/kg。
 * 输入：b、p、H_in、o、out、error、size。
 * 输出：返回 EnergyStatus，并写入 EnergyResult 或错误信息。 */
EnergyStatus chamber_solve_hp(const MassBalance *b,double p,double H_in,
    const EnergyOptions *o,EnergyResult *out,char *error,size_t size)
{
    Context *c;double a,fa,z,fz=0;int i,found=0;EnergyStatus status=ENERGY_OK;
    if(!out)return ENERGY_INVALID;
    memset(out,0,sizeof(*out));out->T=out->H_product=out->S_product=out->residual=NAN;
    out->p=p;out->H_in=H_in;out->status=ENERGY_INVALID;
    if(!b||!o||!b->N||b->N>BALANCE_MAX_PRODUCTS||b->L>=b->N||!isfinite(p)||p<=0||!isfinite(H_in)||
       !isfinite(o->T_min)||!isfinite(o->T_max)||o->T_min<=0||o->T_min>=o->T_max||
       !isfinite(o->T_tolerance)||o->T_tolerance<=0||!isfinite(o->H_tolerance)||o->H_tolerance<=0||
       o->scan_steps<1||o->max_iterations<1||!gas_coverage(b,o->T_min,o->T_max)){
        fail(error,size,"Invalid HP input/options or gas temperature coverage.");return ENERGY_INVALID;
    }
    c=(Context *)calloc(1,sizeof(*c));if(!c){out->status=ENERGY_MEMORY;return ENERGY_MEMORY;}
    c->b=b;c->o=o;c->p=p;c->H_in=H_in;a=o->T_min;z=a;
    if(!evaluate(a,c,&fa)){status=ENERGY_INNER_FAILURE;goto done;}
    if(fa==0){out->root.x=a;out->root.low=out->root.high=a;found=1;}
    for(i=1;!found && i<=o->scan_steps;++i){
        z=o->T_min+(o->T_max-o->T_min)*((double)i/o->scan_steps);
        if(!evaluate(z,c,&fz)){status=ENERGY_INNER_FAILURE;goto done;}
        if(fz==0){out->root.x=z;out->root.low=out->root.high=z;found=1;break;}
        if((fa<0&&fz>0)||(fa>0&&fz<0)){
            found=1;c->trace_phase=ENERGY_TRACE_BRENT;
            out->root_status=brent_solve(evaluate,c,a,z,o->T_tolerance,o->H_tolerance,o->max_iterations,&out->root);
            if(out->root_status!=ROOT_OK){status=out->root_status==ROOT_CALLBACK?ENERGY_INNER_FAILURE:ENERGY_ROOT_FAILURE;goto done;}
            break;
        }
        a=z;fa=fz;
    }
    if(!found){status=ENERGY_NO_BRACKET;goto done;}

    c->trace_phase=ENERGY_TRACE_FINAL;
    if(!evaluate(out->root.x,c,&fz)){status=ENERGY_INNER_FAILURE;goto done;}
    if(fabs(fz)>o->H_tolerance){status=ENERGY_ROOT_FAILURE;goto done;}
    if(!mixture_entropy(&c->s,p,c->eq.n,&out->S_product,c->error,sizeof(c->error))){status=ENERGY_INNER_FAILURE;goto done;}
    out->T=c->T;out->H_product=c->H;out->residual=fz;out->equilibrium=c->eq;
done:
    out->status=status;out->evaluations=c->calls;
    if(status==ENERGY_INNER_FAILURE){if(error&&size)snprintf(error,size,"Inner failure at T=%.12g K: %s",c->T,c->error);}
    else if(status==ENERGY_NO_BRACKET)fail(error,size,"No sign-changing bracket found in temperature scan.");
    else if(status==ENERGY_ROOT_FAILURE)fail(error,size,"Root did not meet temperature/enthalpy tolerances; inspect phase boundaries and residuals.");
    else if(error&&size)error[0]=0;
    free(c);return status;
}
