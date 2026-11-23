/* 文件功能：解析液发命令行并展示高层接口返回的燃烧室出口状态。
 * 输入：氧化剂、燃料、入口温度、质量流量、氧燃比、压强和传热工况。
 * 输出：出口温度、焓、熵及候选产物组成。 */
#include "chamber_high_level.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 功能：解析有限浮点数命令行参数。
 * 输入：字符串 s 和数值输出指针。
 * 输出：成功返回 1 并写入数值，失败返回 0。 */
static int number(const char *s,double *value)
{
    char *end;
    errno=0;
    *value=strtod(s,&end);
    return errno==0 && end!=s && *end=='\0' && isfinite(*value);
}

/* 功能：将液发命令行参数交给高层接口并打印结果。
 * 输入：argc、argv。
 * 输出：成功返回 0，参数错误返回 1，计算失败返回 2。 */
int main(int argc,char **argv)
{
    static ChamberOutlet outlet;
    LiquidChamberInput input={0};
    char error[512]={0};
    double heat=0;
    int show_all=0,case_number=1,valid=1;
    size_t i;

    if (argc<8 || argc>12 ||
        !number(argv[3],&input.T_ox) || !number(argv[4],&input.T_fuel) ||
        !number(argv[5],&input.m_dot_total) ||
        !number(argv[6],&input.mixture_ratio) ||
        !number(argv[7],&input.p) ||
        input.m_dot_total<=0 || input.mixture_ratio<=0 || input.p<=0)
        valid=0;
    if (valid && argc>8) {
        if (!strcmp(argv[8],"--case")) {
            if (argc<10 || (strcmp(argv[9],"1") && strcmp(argv[9],"2") &&
                            strcmp(argv[9],"3"))) valid=0;
            else {
                case_number=argv[9][0]-'0';
                if (case_number==1) {
                    if (argc==11 && !strcmp(argv[10],"--all")) show_all=1;
                    else if (argc!=10) valid=0;
                } else {
                    if (argc<11 || !number(argv[10],&heat) || heat<0) valid=0;
                    if (argc==12 && !strcmp(argv[11],"--all")) show_all=1;
                    else if (argc!=11) valid=0;
                }
            }
        } else if (!strcmp(argv[8],"--all")) {
            if (argc==9) show_all=1; else valid=0;
        } else if (number(argv[8],&heat) && heat>=0) {
            case_number=3;
            if (argc==10 && !strcmp(argv[9],"--all")) show_all=1;
            else if (argc!=9) valid=0;
        } else valid=0;
    }
    if (!valid) {
        fprintf(stderr,"Usage: liquid_chamber OX FUEL T_ox_K T_fuel_K total_kg_per_s O_over_F p_chamber_Pa [--case 1 [--all] | --case 2 Qn_J_per_kg_fuel [--all] | --case 3 Qc_J_per_kg_product [--all]]\n"
                       "OX=LOX|N2O4|H2O2; FUEL=LH2|LCH4|MMH|UDMH|RP1|N2H4|CH3OH|C2H5OH\n"
                       "Legacy: one nonnegative heat-loss value after p means --case 3.\n");
        return 1;
    }
    input.oxidizer=argv[1];
    input.fuel=argv[2];
    input.heat_case=(ChamberLiquidHeatCase)case_number;
    input.heat=heat;
    if (!chamber_solve_liquid(&input,NULL,&outlet,error,sizeof(error))) {
        fprintf(stderr,"ERROR: %s\n",error);
        return 2;
    }

    printf("p_c=%.12g Pa; T_c=%.12g K; H_c=%.12g J/kg; S_c=%.12g J/(kg*K)\n",
           outlet.p_c,outlet.T_c,outlet.h_c,outlet.s_c);
    printf("temperature_evaluations=%d; brent_iterations=%d; equilibrium_iterations=%d; phase_iterations=%d\n",
           outlet.temperature_evaluations,outlet.brent_iterations,
           outlet.equilibrium_iterations,outlet.phase_iterations);
    printf("n_g=%.12g mol/kg\n",outlet.n_g);
    printf("j species phase n_j[mol/kg] x_j(gas) mdot_j[kg/s]\n");
    for (i=0;i<outlet.N;++i) {
        const ChamberOutletSpecies *species=&outlet.species[i];
        if (!show_all && species->phase==CHAMBER_PHASE_GAS &&
            species->n/outlet.n_g<1e-6) continue;
        if (!show_all && species->phase==CHAMBER_PHASE_CONDENSED &&
            species->n==0) continue;
        printf("%lu %-18s %-9s %.10g %.10g %.10g\n",
               (unsigned long)species->j,species->name,
               species->phase==CHAMBER_PHASE_CONDENSED?"condensed":"gas",
               species->n,
               species->phase==CHAMBER_PHASE_CONDENSED?0:species->n/outlet.n_g,
               species->n*species->molar_mass*input.m_dot_total);
    }
    return 0;
}
