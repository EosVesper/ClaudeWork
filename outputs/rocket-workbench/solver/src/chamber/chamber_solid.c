/* 文件功能：由预设固体推进剂的原始条件计算完整燃烧室出口状态。
 * 输入：推进剂配方、初温、绝对压强和可选比热参数。
 * 输出：组元假定化学式、入口焓及 HP 平衡求解后的 ChamberOutlet。 */
#include "chamber_high_level.h"
#include "inlet_enthalpy.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {SOLID_PU, SOLID_PS, SOLID_JPN} SolidKind;

typedef struct {
    const char *name;
    SolidKind kind;
    PropellantFormula formula;
    double T_ref;
    double H_ref;
} SolidCase;

/* 功能：把原子计量和组元配比换算为元素质量分数。
 * 输入：elements、M、atoms、q、J、formula、error、size。
 * 输出：成功返回 1 并写入推进剂配方数据。 */
static int from_atoms(const ChemicalElement *elements, size_t M,
                      const double *atoms, const double *q, size_t J,
                      PropellantFormula *formula, char *error, size_t size)
{
    double g[8*CHEM_MAX_ELEMENTS]={0};
    size_t i,k;
    if (M>CHEM_MAX_ELEMENTS || J>8) {
        if(error&&size)snprintf(error,size,"Too many solid reactant elements/components.");
        return 0;
    }
    for(i=0;i<J;++i) {
        double mass=0;
        for(k=0;k<M;++k)mass+=atoms[i*M+k]*elements[k].A_r;
        if(mass<=0 || !isfinite(mass)) {
            if(error&&size)snprintf(error,size,"Invalid solid reactant formula.");
            return 0;
        }
        for(k=0;k<M;++k)g[i*M+k]=atoms[i*M+k]*elements[k].A_r/mass;
    }
    return propellant_from_mass_fractions(elements,M,g,q,J,formula,error,size);
}

/* 功能：建立预设聚氨酯复合推进剂的组元配方。
 * 输入：c、error、size。
 * 输出：成功返回 1 并写入 SolidCase。 */
static int prepare_polyurethane(SolidCase *c, char *error, size_t size)
{
    const ChemicalElement elements[] = {
        {"Ca",40.08},{"Al",26.98},{"C",12.011},{"H",1.008},
        {"O",15.999},{"N",14.0067},{"Cl",35.453}
    };
    const char *names[] = {"NH4ClO4","Al","CaCO3","C220H441O61",
                           "C4H10O3","C9H6O2N2","C26H50O4","C13H12N2Cl2"};
    const double q[] = {.67,.15,.025,.11826,.0027,.0135,.02,.00054};
    const double H_f[] = {-290.648,0,-1207.850,-13957.828,
                          -633.086,-171.797,-1324.829,116.812};
    const double atoms[8][7] = {
        {0,0,0,4,4,1,1}, {0,1,0,0,0,0,0},
        {1,0,1,0,3,0,0}, {0,0,220,441,61,0,0},
        {0,0,4,10,3,0,0}, {0,0,9,6,2,2,0},
        {0,0,26,50,4,0,0}, {0,0,13,12,0,2,2}
    };
    InletComponent components[8]={0};
    double g[8*7];
    size_t i,k;
    for(i=0;i<8;++i) {
        double mass=0;
        for(k=0;k<7;++k)mass+=atoms[i][k]*elements[k].A_r;
        for(k=0;k<7;++k)g[i*7+k]=atoms[i][k]*elements[k].A_r/mass;
        components[i].name=names[i];
        components[i].mass_fraction=q[i];
        components[i].molar_mass=mass*1e-3;
        components[i].T=298.15;
        components[i].model=INLET_REFERENCE_ONLY;
        components[i].T_ref=298.15;
        components[i].H_m_ref=H_f[i]*1000;
    }
    if(!propellant_from_mass_fractions(elements,7,g,q,8,&c->formula,error,size) ||
       !inlet_mixture_enthalpy(components,8,&c->H_ref,error,size))return 0;
    c->name="PU textbook 5-31";
    c->kind=SOLID_PU;
    c->T_ref=298.15;
    return 1;
}

/* 功能：按配方名称选择并初始化预设固体推进剂。
 * 输入：name、out、error、size。
 * 输出：成功返回 1 并写入 SolidCase。 */
static int prepare_case(const char *name, SolidCase *out, char *error, size_t size)
{
    SolidCase c={0};
    if(!strcmp(name,"531") || !strcmp(name,"PU")) {
        if(!prepare_polyurethane(&c,error,size))return 0;
    } else if(!strcmp(name,"518") || !strcmp(name,"PS")) {

        const ChemicalElement e[]={{"C",12.011},{"H",1.008},{"O",15.999},
                                   {"N",14.0067},{"Cl",35.453},{"S",32.06},
                                   {"Al",26.98}};
        const double q[]={.675,.205,.055,.05,.015};
        const double a[][7]={
            {0,4,4,1,1,0,0}, {30,62,12,0,0,12,0},
            {8,8,0,0,0,0,0}, {0,0,0,0,0,0,1},
            {21,24,4,0,0,0,0}
        };
        if(!from_atoms(e,7,&a[0][0],q,5,&c.formula,error,size))return 0;
        c.name="PS textbook 5-18";
        c.kind=SOLID_PS;
        c.T_ref=298.16;
        c.H_ref=-1424098.0;
    } else if(!strcmp(name,"JPN")) {

        const ChemicalElement e[]={{"C",12.011},{"H",1.008},{"O",15.999},
                                   {"N",14.0067},{"K",39.0983},{"S",32.06}};
        const double q[]={.515,.43,.0325,.01,.0125};
        double a[5][6]={{0}};
        double x,NG_mass,DEP_mass,EC_mass,KS_mass;
        double H_KS;
        ThermoResult KS;

        const double nitrogen_fraction=.1325;
        const double base_mass=6*e[0].A_r+10*e[1].A_r+5*e[2].A_r;
        const double nitration_mass=-e[1].A_r+2*e[2].A_r+e[3].A_r;
        x=nitrogen_fraction*base_mass/(e[3].A_r-nitrogen_fraction*nitration_mass);
        a[0][0]=6;a[0][1]=10-x;a[0][2]=5+2*x;a[0][3]=x;
        a[1][0]=3;a[1][1]=5;a[1][2]=9;a[1][3]=3;
        a[2][0]=12;a[2][1]=14;a[2][2]=4;
        a[3][0]=17;a[3][1]=20;a[3][2]=1;a[3][3]=2;
        a[4][2]=4;a[4][4]=2;a[4][5]=1;
        if(!from_atoms(e,6,&a[0][0],q,5,&c.formula,error,size))return 0;

        KS=thermo_h("K2SO4(II)",298.15);
        if(!isfinite(KS.H)) {
            if(error&&size)snprintf(error,size,"JPN K2SO4(II): %s",thermo_last_error());
            return 0;
        }
        NG_mass=3*e[0].A_r+5*e[1].A_r+9*e[2].A_r+3*e[3].A_r;
        DEP_mass=12*e[0].A_r+14*e[1].A_r+4*e[2].A_r;
        EC_mass=17*e[0].A_r+20*e[1].A_r+e[2].A_r+2*e[3].A_r;
        KS_mass=4*e[2].A_r+2*e[4].A_r+e[5].A_r;
        H_KS=KS.H/(KS_mass*1e-3);
        c.H_ref=q[0]*(-2429e3) + q[1]*(-370.72e3/(NG_mass*1e-3)) +
                q[2]*(-753.08e3/(DEP_mass*1e-3)) +
                q[3]*(-105.08e3/(EC_mass*1e-3)) + q[4]*H_KS;
        c.name="JPN double-base";
        c.kind=SOLID_JPN;
        c.T_ref=298.15;
    } else {
        if(error&&size)snprintf(error,size,"Unknown solid case. Use 531, 518 or JPN.");
        return 0;
    }
    *out=c;
    return 1;
}

/* 功能：用 NASA 数据计算固体组元相对参考温度的显热。
 * 输入：name、q、T、T_ref、delta、error、size。
 * 输出：成功返回 1 并写入比焓增量。 */
static int nasa_sensible(const char *name, double q, double T, double T_ref,
                         double *delta, char *error, size_t size)
{
    ThermoResult now=thermo_h(name,T);
    ThermoResult ref;
    if(!isfinite(now.H) || !isfinite(now.MW) || now.MW<=0) {
        if(error&&size)snprintf(error,size,"%s at %.8g K: %s",name,T,thermo_last_error());
        return 0;
    }
    ref=thermo_h(name,T_ref);
    if(!isfinite(ref.H)) {
        if(error&&size)snprintf(error,size,"%s at %.8g K: %s",name,T_ref,thermo_last_error());
        return 0;
    }
    *delta=q*(now.H-ref.H)/(now.MW*1e-3);
    if(!isfinite(*delta)) {
        if(error&&size)snprintf(error,size,"Non-finite NASA solid enthalpy difference for %s.",name);
        return 0;
    }
    return 1;
}

/* 功能：计算 JPN 有机组元的入口显热修正。
 * 输入：T、T_ref。
 * 输出：返回比焓增量。 */
static double jpn_organic_sensible(double T, double T_ref)
{
    return 4184.0*(0.118+3.30e-4*(T+T_ref))*(T-T_ref);
}

/* 功能：汇总固体推进剂各组元的入口显热。
 * 输入：c、T、binder_cp、delta、error、size。
 * 输出：成功返回 1 并写入总比焓修正。 */
static int inlet_sensible(const SolidCase *c, double T, double binder_cp,
                          double *delta, char *error, size_t size)
{
    const double dT=T-c->T_ref;
    double d=0,term;
    if(c->kind==SOLID_PU || c->kind==SOLID_PS) {
        const double q_ap=c->kind==SOLID_PU?.67:.675;
        const double q_al=c->kind==SOLID_PU?.15:.05;
        if(!nasa_sensible("NH4CLO4(I)",q_ap,T,c->T_ref,&term,error,size))return 0;
        d+=term;
        if(!nasa_sensible("AL(cr)",q_al,T,c->T_ref,&term,error,size))return 0;
        d+=term;
        if(c->kind==SOLID_PU) {
            if(!nasa_sensible("CaCO3(cr)",.025,T,c->T_ref,&term,error,size))return 0;
            d+=term;

            d+=(.11826*binder_cp+.03674*1300.0)*dT;
        } else {

            const double styrene_cp=183.2/(.104151);
            d+=(.205*binder_cp+.055*styrene_cp+.015*1300.0)*dT;
        }
    } else {
        if(T<283.0 || T>343.0) {
            if(error&&size)snprintf(error,size,"JPN analogue heat capacity requires 283 <= Ti <= 343 K.");
            return 0;
        }
        d=.9875*jpn_organic_sensible(T,c->T_ref);
        if(!nasa_sensible("K2SO4(II)",.0125,T,c->T_ref,&term,error,size))return 0;
        d+=term;
    }
    if(!isfinite(d)) {
        if(error&&size)snprintf(error,size,"Non-finite solid inlet sensible enthalpy.");
        return 0;
    }
    *delta=d;
    return 1;
}

/* 功能：标记高层固发输入错误。
 * 输入：out、error 缓冲区和错误文本。
 * 输出：返回 0，并将 out 标记为无效。 */
static int fail(ChamberOutlet *out, char *error, size_t size, const char *message)
{
    if (out) out->valid=0;
    if (error && size) snprintf(error,size,"%s",message);
    return 0;
}

/* 功能：由预设固体推进剂条件计算入口焓、候选物种和平衡出口状态。
 * 输入：input、可选能量求解选项及输出缓冲区。
 * 输出：成功返回 1 和完整出口状态，失败返回 0 和错误文本。 */
int chamber_solve_solid(const SolidChamberInput *input,
                        const EnergyOptions *options, ChamberOutlet *out,
                        char *error, size_t error_size)
{
    SolidCase c;
    EnergyOptions defaults;
    const EnergyOptions *used_options;
    const char *path;
    MassBalance *balance;
    double binder_cp=1300.0,H_delta,H_in;
    int loaded=0,ok=0;

    if (!out) return fail(NULL,error,error_size,"Null chamber outlet.");
    memset(out,0,sizeof(*out));
    out->energy_status=ENERGY_INVALID;
    out->equilibrium_status=EQ_INVALID_INPUT;
    if (error && error_size) error[0]='\0';
    if (!input || !input->propellant)
        return fail(out,error,error_size,"Null solid chamber input or propellant.");
    if (!isfinite(input->T_in) || input->T_in<273.15 ||
        input->T_in>333.15 || !isfinite(input->p) || input->p<=0 ||
        !isfinite(input->m_dot_total) || input->m_dot_total<0 ||
        (input->cp_mode!=CHAMBER_SOLID_CP_DEFAULT &&
         input->cp_mode!=CHAMBER_SOLID_CP_BINDER &&
         input->cp_mode!=CHAMBER_SOLID_CP_EFFECTIVE) ||
        (input->cp_mode!=CHAMBER_SOLID_CP_DEFAULT &&
         (!isfinite(input->cp_value) || input->cp_value<=0 ||
          input->cp_value>=10000)))
        return fail(out,error,error_size,"Invalid solid chamber condition.");

    path=input->thermo_path ? input->thermo_path : "thermo.inp";
    defaults=energy_default_options(1500,5000);
    used_options=options ? options : &defaults;
    balance=(MassBalance *)calloc(1,sizeof(*balance));
    if (!balance) return fail(out,error,error_size,"Cannot allocate mass balance.");
    if (thermo_load(path)) {
        fail(out,error,error_size,thermo_last_error());
        goto done;
    }
    loaded=1;
    if (!prepare_case(input->propellant,&c,error,error_size)) goto done;
    if (input->cp_mode==CHAMBER_SOLID_CP_BINDER) {
        if (c.kind==SOLID_JPN) {
            fail(out,error,error_size,"Binder cp applies only to 531 or 518.");
            goto done;
        }
        binder_cp=input->cp_value;
    }
    if (input->cp_mode==CHAMBER_SOLID_CP_EFFECTIVE)
        H_delta=input->cp_value*(input->T_in-c.T_ref);
    else if (!inlet_sensible(&c,input->T_in,binder_cp,
                             &H_delta,error,error_size)) goto done;
    H_in=c.H_ref+H_delta;
    if (!isfinite(H_in)) {
        fail(out,error,error_size,"Non-finite solid inlet enthalpy.");
        goto done;
    }
    if (!mass_balance_from_nasa_phases(path,&c.formula,
                                       used_options->T_min,used_options->T_max,1,
                                       balance,error,error_size))
        goto done;
    ok=chamber_outlet_solve_hp(balance,input->p,H_in,input->m_dot_total,
                               used_options,out,error,error_size);
done:
    if (loaded) thermo_free();
    free(balance);
    return ok;
}


