/* 文件功能：解析固发命令行并展示高层接口返回的燃烧室出口状态。
 * 输入：预设配方、初温、绝对压强和可选比热参数。
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

/* 功能：将固发命令行参数交给高层接口并打印结果。
 * 输入：argc、argv。
 * 输出：成功返回 0，参数错误返回 1，计算失败返回 2。 */
int main(int argc,char **argv)
{
    static ChamberOutlet outlet;
    SolidChamberInput input={0};
    char error[512]={0};
    int show_all=0;
    size_t j;

    if (argc<4 || !number(argv[2],&input.T_in) ||
        !number(argv[3],&input.p) ||
        input.T_in<273.15 || input.T_in>333.15 || input.p<=0)
        goto usage;
    for (j=4;j<(size_t)argc;++j) {
        if (!strcmp(argv[j],"--all")) show_all=1;
        else if (!strcmp(argv[j],"--cp-eff") && j+1<(size_t)argc &&
                 number(argv[++j],&input.cp_value) &&
                 input.cp_value>0 && input.cp_value<10000) {
            if (input.cp_mode!=CHAMBER_SOLID_CP_DEFAULT) goto usage;
            input.cp_mode=CHAMBER_SOLID_CP_EFFECTIVE;
        } else if (!strcmp(argv[j],"--cp-binder") && j+1<(size_t)argc &&
                    number(argv[++j],&input.cp_value) &&
                    input.cp_value>0 && input.cp_value<10000) {
            if (input.cp_mode!=CHAMBER_SOLID_CP_DEFAULT) goto usage;
            input.cp_mode=CHAMBER_SOLID_CP_BINDER;
        } else goto usage;
    }
    input.propellant=argv[1];
    if (!chamber_solve_solid(&input,NULL,&outlet,error,sizeof(error))) {
        fprintf(stderr,"ERROR: %s\n",error);
        return 2;
    }

    printf("p_c=%.12g Pa; T_c=%.12g K; H_c=%.12g J/kg; S_c=%.12g J/(kg*K)\n",
           outlet.p_c,outlet.T_c,outlet.h_c,outlet.s_c);
    printf("temperature_evaluations=%d; brent_iterations=%d; equilibrium_iterations=%d; phase_iterations=%d\n",
           outlet.temperature_evaluations,outlet.brent_iterations,
           outlet.equilibrium_iterations,outlet.phase_iterations);
    printf("n_g=%.12g mol/kg\n",outlet.n_g);
    printf("j species phase n_j[mol/kg] x_j(gas) mass_fraction\n");
    for (j=0;j<outlet.N;++j) {
        const ChamberOutletSpecies *species=&outlet.species[j];
        if (!show_all && species->phase==CHAMBER_PHASE_GAS &&
            species->n/outlet.n_g<1e-6) continue;
        if (!show_all && species->phase==CHAMBER_PHASE_CONDENSED &&
            species->n==0) continue;
        printf("%lu %-18s %-9s %.10g %.10g %.10g\n",
               (unsigned long)species->j,species->name,
               species->phase==CHAMBER_PHASE_CONDENSED?"condensed":"gas",
               species->n,
               species->phase==CHAMBER_PHASE_CONDENSED?0:species->n/outlet.n_g,
               species->n*species->molar_mass);
    }
    return 0;
usage:
    fprintf(stderr,"Usage: solid_chamber 531|518|JPN Ti_K p_absolute_Pa [--cp-binder J_per_kg_K | --cp-eff J_per_kg_K] [--all]\n"
                   "Ti range: 273.15-333.15 K (JPN: 283-333.15 K). Default binder cp=1300; --cp-eff restores whole-mixture constant-cp mode. Run in the code folder.\n");
    return 1;
}
