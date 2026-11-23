/* 文件功能：在指定温度和压强下求含气相与凝相的化学平衡组成。
 * 输入：候选物种、标准化学势、压强和迭代选项。
 * 输出：各物种摩尔数、相稳定性与收敛状态。 */
#include "equilibrium_solver.h"
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define SMALL_DIM (2*CHEM_MAX_ELEMENTS+1)
typedef struct {
    double z[BALANCE_MAX_PRODUCTS],lambda[CHEM_MAX_ELEMENTS],z_g;
    double n[BALANCE_MAX_PRODUCTS],n_g,sum_n;
    double r[BALANCE_MAX_PRODUCTS],r_bal[CHEM_MAX_ELEMENTS],r_g;
    double scale[BALANCE_MAX_PRODUCTS];
    int active[BALANCE_MAX_PRODUCTS];
    double max_eq,max_bal,max_phase,violation,merit;
} Iterate;

/* 功能：生成平衡迭代的默认容差及步长选项。
 * 输入：无。
 * 输出：返回 EquilibriumOptions。 */
EquilibriumOptions equilibrium_default_options(void)
{
    EquilibriumOptions o={200,50,1e-9,1e-10,1e-10,.05,5.,1e-4,1e-14,
                          128,1e-9,1e-8,1e-12};
    return o;
}

/* 功能：将平衡求解状态转换为文字。
 * 输入：status。
 * 输出：返回状态名称字符串。 */ 
const char *equilibrium_status_name(EquilibriumStatus status)
{
    switch (status) {
    case EQ_OK:return "converged";
    case EQ_INVALID_INPUT:return "invalid input";
    case EQ_NUMERIC_ERROR:return "numeric range error";
    case EQ_LINEAR_FAILURE:return "linear solve failure";
    case EQ_LINE_SEARCH_FAILURE:return "line search failure";
    case EQ_STAGNATION:return "stagnation";
    case EQ_MAX_ITERATIONS:return "iteration limit";
    case EQ_PHASE_CYCLE:return "phase cycle limit";
    case EQ_PHASE_DEGENERATE:return "degenerate/singular phase system";
    default:return "unknown status";
    }
}

/* 功能：设置平衡求解的结束状态和诊断信息。
 * 输入：out、code、error、size、message。
 * 输出：返回 EquilibriumStatus。 */
static EquilibriumStatus finish(EquilibriumResult *out,EquilibriumStatus code,
                               char *error,size_t size,const char *message)
{
    if (out) { out->status=code;out->converged=(code==EQ_OK); }
    if (error && size) snprintf(error,size,"%s",message);
    return code;
}

/* 功能：检查数值是否为有限正数。
 * 输入：x。
 * 输出：返回布尔判断值。 */
static int positive(double x) { return isfinite(x) && x>0; }

/* 功能：核对末尾气相原子列是否对应指定元素。
 * 输入：b、k。
 * 输出：返回匹配判断结果。 */
static int atomic_column(const MassBalance *b,size_t k)
{
    size_t j,i;
    for (j=b->L;j<b->N;++j) {
        for (i=0;i<b->M;++i) if (b->A[i][j]!=(i==k ? 1. : 0.)) break;
        if (i==b->M) return (int)j;
    }
    return -1;
}

/* 功能：按当前气相对数变量和凝相数量计算全部平衡残差。
 * 输入：b、s、log_p、v。
 * 输出：成功返回 1 并更新 Iterate。 */
static int evaluate(const MassBalance *b,const GibbsStandard *s,double log_p,Iterate *v)
{
    size_t j,k;
    double sumsq=0,lower=log(DBL_MIN),upper=log(DBL_MAX);
    if (!isfinite(v->z_g) || v->z_g<lower || v->z_g>upper) return 0;
    v->n_g=exp(v->z_g);v->sum_n=0;
    v->max_eq=v->max_bal=v->max_phase=v->violation=0;
    for (k=0;k<b->M;++k) if (!isfinite(v->lambda[k])) return 0;
    for (j=0;j<b->N;++j) {
        if (j<b->L) {
            v->z[j]=NAN;
            if (!v->active[j]) v->n[j]=0;
            if (!isfinite(v->n[j])) return 0;
            if (!s->available[j]) { v->r[j]=NAN;continue; }
            v->r[j]=-s->y[j];
        } else {
            if (!isfinite(v->z[j]) || v->z[j]<lower || v->z[j]>upper) return 0;
            v->n[j]=exp(v->z[j]);v->sum_n+=v->n[j];
            v->r[j]=-s->y[j]+v->z[j]-v->z_g+log_p;
        }
        for (k=0;k<b->M;++k) v->r[j]-=b->A[k][j]*v->lambda[k];
        if (!isfinite(v->r[j])) return 0;
        if (j>=b->L) v->max_eq=fmax(v->max_eq,fabs(v->r[j]));
        else if (v->active[j]) v->max_phase=fmax(v->max_phase,fabs(v->r[j]));
        else v->violation=fmax(v->violation,-v->r[j]);
        if (j>=b->L || v->active[j]) sumsq+=v->r[j]*v->r[j];
    }
    for (k=0;k<b->M;++k) {
        double a=0;
        for (j=0;j<b->N;++j) a+=b->A[k][j]*v->n[j];
        v->r_bal[k]=(a-b->N_k[k])/b->N_k[k];
        v->max_bal=fmax(v->max_bal,fabs(v->r_bal[k]));
        sumsq+=v->r_bal[k]*v->r_bal[k];
    }
    v->r_g=v->sum_n/v->n_g-1;sumsq+=v->r_g*v->r_g;
    v->merit=.5*sumsq;
    return positive(v->n_g) && positive(v->sum_n) && isfinite(v->merit);
}

/* 功能：求解约化线性方程组。
 * 输入：dim、double C[SMALL_DIM][SMALL_DIM]、d、x。
 * 输出：成功返回 1 并写入解向量。 */
static int solve_small(size_t dim,double C[SMALL_DIM][SMALL_DIM],double *d,double *x)
{
    size_t i,j,k,pivot;
    for (i=0;i<dim;++i) {
        double scale=0;
        for (j=0;j<dim;++j) scale=fmax(scale,fabs(C[i][j]));
        if (!positive(scale) || !isfinite(d[i])) return 0;
        for (j=0;j<dim;++j) C[i][j]/=scale;
        d[i]/=scale;
    }
    for (k=0;k<dim;++k) {
        pivot=k;
        for (i=k+1;i<dim;++i) if (fabs(C[i][k])>fabs(C[pivot][k])) pivot=i;
        if (!isfinite(C[pivot][k]) || fabs(C[pivot][k])<1e-14) return 0;
        if (pivot!=k) {
            double tmp=d[k];d[k]=d[pivot];d[pivot]=tmp;
            for (j=k;j<dim;++j) { tmp=C[k][j];C[k][j]=C[pivot][j];C[pivot][j]=tmp; }
        }
        for (i=k+1;i<dim;++i) {
            double factor=C[i][k]/C[k][k];C[i][k]=0;
            for (j=k+1;j<dim;++j) C[i][j]-=factor*C[k][j];
            d[i]-=factor*d[k];
        }
    }
    for (i=dim;i-- >0;) {
        double sum=d[i];
        for (j=i+1;j<dim;++j) sum-=C[i][j]*x[j];
        x[i]=sum/C[i][i];if (!isfinite(x[i])) return 0;
    }
    return 1;
}

/* 功能：构建平衡迭代的约化方向；气相为对数摩尔数步长，凝相为摩尔数步长。
 * 输入：b、v、change、dlambda、dzg、slope。
 * 输出：成功返回 1 并写入方向及活动相数据。 */
static int direction(const MassBalance *b,const Iterate *v,double *change,
                     double *dlambda,double *dzg,double *slope)
{
    double C[SMALL_DIM][SMALL_DIM]={{0}},d[SMALL_DIM]={0},x[SMALL_DIM]={0};
    size_t list[CHEM_MAX_ELEMENTS],K=0,j,k,i,c,dim;
    double linear_error=0,jnorm=0,rnorm=0;
    for (j=0;j<b->L;++j) if (v->active[j]) {
        if (K==b->M) return 0;
        list[K++]=j;
    }
    dim=b->M+1+K;
    for (k=0;k<b->M;++k) {
        double a=0,t=0,condensed=0;
        for (j=b->L;j<b->N;++j) {
            double an=b->A[k][j]*v->n[j];a+=an;t+=an*v->r[j];
            for (i=0;i<b->M;++i) C[k][i]+=an*b->A[i][j];
        }
        for (j=0;j<b->L;++j) condensed+=b->A[k][j]*v->n[j];
        C[k][b->M]=a;C[b->M][k]=a;d[k]=b->N_k[k]-a-condensed+t;
        for (c=0;c<K;++c) {
            j=list[c];
            C[k][b->M+1+c]=b->A[k][j]*v->scale[j];
            C[b->M+1+c][k]=b->A[k][j];
        }
    }
    d[b->M]=v->n_g-v->sum_n;
    for (j=b->L;j<b->N;++j) d[b->M]+=v->n[j]*v->r[j];
    for (c=0;c<K;++c) d[b->M+1+c]=v->r[list[c]];
    if (!solve_small(dim,C,d,x)) return 0;
    for (k=0;k<b->M;++k) dlambda[k]=x[k];
    *dzg=x[b->M];
    for (j=0;j<b->L;++j) change[j]=0;
    for (c=0;c<K;++c) change[list[c]]=x[b->M+1+c]*v->scale[list[c]];
    for (j=b->L;j<b->N;++j) {
        change[j]=*dzg-v->r[j];
        for (k=0;k<b->M;++k) change[j]+=b->A[k][j]*dlambda[k];
        if (!isfinite(change[j])) return 0;
    }
    *slope=0;
    for (j=0;j<b->N;++j) if (j>=b->L || v->active[j]) {
        double jr=j<b->L ? 0 : change[j]-*dzg;
        for (k=0;k<b->M;++k) jr-=b->A[k][j]*dlambda[k];
        linear_error=fmax(linear_error,fabs(jr+v->r[j]));
        jnorm=fmax(jnorm,fabs(jr));rnorm=fmax(rnorm,fabs(v->r[j]));
        *slope+=v->r[j]*jr;
    }
    for (k=0;k<b->M;++k) {
        double jr=0;
        for (j=0;j<b->N;++j)
            jr+=b->A[k][j]*(j<b->L ? change[j] : v->n[j]*change[j])/b->N_k[k];
        linear_error=fmax(linear_error,fabs(jr+v->r_bal[k]));
        jnorm=fmax(jnorm,fabs(jr));rnorm=fmax(rnorm,fabs(v->r_bal[k]));
        *slope+=v->r_bal[k]*jr;
    }
    {
        double jr=-v->sum_n/v->n_g*(*dzg);
        for (j=b->L;j<b->N;++j) jr+=v->n[j]/v->n_g*change[j];
        linear_error=fmax(linear_error,fabs(jr+v->r_g));
        jnorm=fmax(jnorm,fabs(jr));rnorm=fmax(rnorm,fabs(v->r_g));
        *slope+=v->r_g*jr;
    }
    return isfinite(*slope) && *slope<0 &&
        linear_error<=1e-9*fmax(1.,fmax(jnorm,rnorm));
}

/* 功能：把当前平衡迭代点和诊断量写入结果结构。
 * 输入：b、s、v、o、out。
 * 输出：更新 EquilibriumResult。 */
static void snapshot(const MassBalance *b,const GibbsStandard *s,const Iterate *v,
                     const EquilibriumOptions *o,EquilibriumResult *out)
{
    size_t j,k;out->active_count=0;
    for (j=0;j<b->N;++j) {
        out->n[j]=v->n[j];out->z[j]=v->z[j];
        if (j<b->L) {
            out->active_condensed[j]=v->active[j];out->active_count+=v->active[j]!=0;
            out->phase_stability[j]=v->r[j];
            out->phase_status[j]=!s->available[j] ? PHASE_UNAVAILABLE :
                (v->active[j] ? PHASE_ACTIVE : PHASE_ABSENT);
            if (s->available[j] && fabs(v->r[j])<=o->tol_phase_add &&
                fabs(v->n[j])<=o->tol_phase_amount*v->scale[j])
                out->phase_status[j]=PHASE_BOUNDARY;
        }
    }
    for (k=0;k<b->M;++k) out->lambda[k]=v->lambda[k];
    out->z_g=v->z_g;out->n_g=v->n_g;
    out->max_equilibrium_residual=v->max_eq;out->max_balance_residual=v->max_bal;
    out->max_condensed_residual=v->max_phase;out->max_phase_violation=v->violation;
    out->total_residual=fabs(v->r_g);out->merit=v->merit;
}

/* 功能：检查内层平衡方程的各项残差是否满足容差。
 * 输入：v、o。
 * 输出：返回收敛判断结果。 */
static int inner_converged(const Iterate *v,const EquilibriumOptions *o)
{
    return v->max_eq<=o->tol_equilibrium && v->max_bal<=o->tol_balance &&
        fabs(v->r_g)<=o->tol_total && v->max_phase<=o->tol_phase;
}

/* 功能：比较两个候选凝相的元素计量比例。
 * 输入：b、i、j。
 * 输出：返回计量比例或不匹配标志。 */
static double same_composition(const MassBalance *b,size_t i,size_t j)
{
    size_t k;double ratio=0;
    for (k=0;k<b->M;++k) if (b->A[k][i]>0) { ratio=b->A[k][j]/b->A[k][i];break; }
    if (!positive(ratio)) return 0;
    for (k=0;k<b->M;++k)
        if (fabs(b->A[k][j]-ratio*b->A[k][i])>1e-12*fmax(1.,b->A[k][j])) return 0;
    return ratio;
}

/* 功能：处理同组成凝相的优势相替换。
 * 输入：b、s、v、o、out。
 * 输出：更新活动相及迭代状态。 */
static void replace_dominated(const MassBalance *b,const GibbsStandard *s,
                             Iterate *v,const EquilibriumOptions *o,EquilibriumResult *out)
{
    size_t i,j;
    for (i=0;i<b->L;++i) if (v->active[i]) {
        size_t best=i;double ratio_best=1,best_y=s->y[i];
        for (j=0;j<b->L;++j) if (s->available[j]) {
            double ratio=same_composition(b,i,j);
            if (ratio>0 && s->y[j]/ratio>best_y+o->tol_phase_add) {
                best=j;ratio_best=ratio;best_y=s->y[j]/ratio;
            }
        }
        if (best!=i) {
            v->n[best]+=v->n[i]/ratio_best;v->n[i]=0;v->active[i]=0;
            if (!v->active[best]) ++out->phase_additions;
            v->active[best]=1;++out->phase_removals;
        }
    }
}

/* 功能：在固定温度与绝对压强下求气相和可出现凝相的平衡组分。
 * 输入：b、s、p、initial_n、options、out、error、size。
 * 输出：返回 EquilibriumStatus，并写入全部 n_j 与相稳定性。 */
EquilibriumStatus equilibrium_solve(const MassBalance *b,const GibbsStandard *s,double p,
    const double *initial_n,const EquilibriumOptions *options,
    EquilibriumResult *out,char *error,size_t size)
{
    EquilibriumOptions o=options ? *options : equilibrium_default_options();
    Iterate v={0},trial={0};
    double init[BALANCE_MAX_PRODUCTS],change[BALANCE_MAX_PRODUCTS];
    double dlambda[CHEM_MAX_ELEMENTS],dzg,log_p;
    int atom[CHEM_MAX_ELEMENTS],is_atom[BALANCE_MAX_PRODUCTS]={0},outer,iteration;
    uint64_t history[256];
    size_t j,k;
    if (initial_n && b && b->N>0 && b->N<=BALANCE_MAX_PRODUCTS) {
        memcpy(init,initial_n,b->N*sizeof(double));initial_n=init;
    }
    if (out) { memset(out,0,sizeof(*out));out->Phi=NAN;out->g=NAN; }
    if (!b || !s || !out || b->M==0 || b->M>CHEM_MAX_ELEMENTS ||
        b->N==0 || b->N>BALANCE_MAX_PRODUCTS || b->L>=b->N || b->N!=s->N || b->L!=s->L ||
        !positive(p) || !positive(s->T) || !positive(s->p_0) || o.max_iterations<0 ||
        o.max_backtracks<1 || !positive(o.tol_equilibrium) || !positive(o.tol_balance) ||
        !positive(o.tol_total) || !positive(o.initial_fraction) || o.initial_fraction>=1 ||
        !positive(o.max_log_step) || !positive(o.armijo_c) || o.armijo_c>=.5 ||
        !positive(o.min_step) || o.min_step>1 || o.max_phase_iterations<1 ||
        o.max_phase_iterations>256 || !positive(o.tol_phase) ||
        !positive(o.tol_phase_add) || o.tol_phase_add<o.tol_phase || !positive(o.tol_phase_amount))
        return finish(out,EQ_INVALID_INPUT,error,size,"Invalid model, state or solver options.");
    for (k=0;k<b->M;++k) {
        if (!positive(b->N_k[k]))
            return finish(out,EQ_INVALID_INPUT,error,size,"Element inventories must be positive.");
        for (j=0;j<b->N;++j) if (!isfinite(b->A[k][j]) || b->A[k][j]<0)
            return finish(out,EQ_INVALID_INPUT,error,size,"Invalid stoichiometric matrix.");
        atom[k]=atomic_column(b,k);
        if (atom[k]>=0) is_atom[atom[k]]=1;
        else if (!initial_n)
            return finish(out,EQ_INVALID_INPUT,error,size,"No atomic gas carrier: provide initial_n.");
    }
    for (j=0;j<b->N;++j) {
        double atoms=0;v.scale[j]=DBL_MAX;
        if (strcmp(b->names[j],s->names[j]) ||
            ((j>=b->L || s->available[j]) && !isfinite(s->y[j])))
            return finish(out,EQ_INVALID_INPUT,error,size,"Property order or potential invalid.");
        for (k=0;k<b->M;++k) {
            atoms+=b->A[k][j];
            if (b->A[k][j]>0) v.scale[j]=fmin(v.scale[j],b->N_k[k]/b->A[k][j]);
        }
        if (!positive(atoms) || !positive(v.scale[j]))
            return finish(out,EQ_INVALID_INPUT,error,size,"Invalid element carrier or amount scale.");
    }
    log_p=log(p)-log(s->p_0);
    if (initial_n) {
        for (j=0;j<b->N;++j) {
            if (!isfinite(initial_n[j]) || (j<b->L ? initial_n[j]<0 : initial_n[j]<=0))
                return finish(out,EQ_INVALID_INPUT,error,size,"Gas initial amounts must be positive; condensed nonnegative.");
            v.n[j]=initial_n[j];
            if (j<b->L) {
                if (!s->available[j]) v.n[j]=0;
                v.active[j]=v.n[j]>0;
            }
        }
    } else {
        double epsilon=DBL_MAX;
        for (k=0;k<b->M;++k) {
            double B=0;
            for (j=b->L;j<b->N;++j) if (!is_atom[j]) B+=b->A[k][j];
            if (B>0) epsilon=fmin(epsilon,o.initial_fraction*b->N_k[k]/B);
        }
        if (epsilon==DBL_MAX) epsilon=0;
        for (j=b->L;j<b->N;++j) if (!is_atom[j]) v.n[j]=epsilon;
        for (k=0;k<b->M;++k) {
            double used=0;
            for (j=b->L;j<b->N;++j) if (!is_atom[j]) used+=b->A[k][j]*v.n[j];
            v.n[atom[k]]=b->N_k[k]-used;
        }
    }
    for (j=b->L;j<b->N;++j) { v.z[j]=log(v.n[j]);v.sum_n+=v.n[j]; }
    v.z_g=log(v.sum_n);
    for (k=0;k<b->M;++k) if (atom[k]>=0)
        v.lambda[k]=-s->y[atom[k]]+v.z[atom[k]]-v.z_g+log_p;
    for (outer=0;outer<o.max_phase_iterations;++outer) {
        uint64_t signature=UINT64_C(1469598103934665603);
        int repeats=0,h,remove=-1,add=-1;
        double most_negative=0;
        replace_dominated(b,s,&v,&o,out);
        out->phase_iterations=outer+1;
        for (j=0;j<b->L;++j) { signature^=(uint64_t)(v.active[j]!=0);signature*=UINT64_C(1099511628211); }
        for (h=0;h<outer;++h) if (history[h]==signature) ++repeats;
        history[outer]=signature;
        if (repeats>=2)
            return finish(out,EQ_PHASE_CYCLE,error,size,"Repeated active phase set; inspect boundary/degeneracy.");
        if (!evaluate(b,s,log_p,&v))
            return finish(out,EQ_NUMERIC_ERROR,error,size,"State outside representable numeric range.");
        for (iteration=0;;++iteration) {
            double eta,max_change,slope;int backtrack,accepted=0;
            snapshot(b,s,&v,&o,out);
            if (inner_converged(&v,&o)) break;
            if (iteration>=o.max_iterations)
                return finish(out,EQ_MAX_ITERATIONS,error,size,"Inner residuals above tolerance at iteration limit.");
            if (!direction(b,&v,change,dlambda,&dzg,&slope))
                return finish(out,out->active_count ? EQ_PHASE_DEGENERATE : EQ_LINEAR_FAILURE,
                    error,size,"Reduced matrix singular/ill-conditioned or Newton residual failed; no unique verified phase solution.");
            max_change=fabs(dzg);
            for (j=0;j<b->N;++j)
                max_change=fmax(max_change,fabs(j<b->L ? change[j]/v.scale[j] : change[j]));
            eta=max_change>o.max_log_step ? o.max_log_step/max_change : 1;
            for (backtrack=0;backtrack<o.max_backtracks && eta>=o.min_step;++backtrack) {
                trial=v;
                for (j=0;j<b->N;++j) {
                    if (j<b->L) trial.n[j]+=eta*change[j];
                    else trial.z[j]+=eta*change[j];
                }
                for (k=0;k<b->M;++k) trial.lambda[k]+=eta*dlambda[k];
                trial.z_g+=eta*dzg;
                if (evaluate(b,s,log_p,&trial) && trial.merit<=v.merit+o.armijo_c*eta*slope) {
                    accepted=1;break;
                }
                eta*=.5;++out->backtracks;
            }
            if (!accepted)
                return finish(out,EQ_LINE_SEARCH_FAILURE,error,size,"No acceptable Armijo step; not converged.");
            if (eta*max_change<1e-14 && !inner_converged(&trial,&o))
                return finish(out,EQ_STAGNATION,error,size,"Tiny scaled step with unresolved residuals.");
            v=trial;++out->iterations;
        }

        for (j=0;j<b->L;++j) if (v.active[j] && v.n[j]/v.scale[j]<most_negative) {
            most_negative=v.n[j]/v.scale[j];remove=(int)j;
        }
        if (remove>=0) {
            v.active[remove]=0;v.n[remove]=0;++out->phase_removals;continue;
        }
        most_negative=-o.tol_phase_add;
        for (j=0;j<b->L;++j)
            if (s->available[j] && !v.active[j] && v.r[j]<most_negative) {
                most_negative=v.r[j];add=(int)j;
            }
        if (add>=0) { v.active[add]=1;v.n[add]=0;++out->phase_additions;continue; }
        for (j=0;j<b->L;++j) if (v.active[j] && v.n[j]>o.tol_phase_amount*v.scale[j]) {
            size_t other;
            for (other=0;other<b->L;++other) if (other!=j && s->available[other]) {
                double ratio=same_composition(b,j,other);
                if (ratio>0 && fabs(s->y[other]/ratio-s->y[j])<=o.tol_phase)
                    return finish(out,EQ_PHASE_DEGENERATE,error,size,"Equal-potential phases: fixed T,p cannot determine their separate amounts.");
            }
        }
        {
            GibbsResult objective;
            if (!gibbs_evaluate(s,p,v.n,&objective,error,size))
                return finish(out,EQ_NUMERIC_ERROR,error,size,"Cannot evaluate physical Gibbs objective.");
            out->Phi=objective.Phi;out->g=objective.g;
        }
        return finish(out,EQ_OK,error,size,"");
    }
    return finish(out,EQ_PHASE_CYCLE,error,size,"Active phase iteration limit reached; not physical convergence.");
}
