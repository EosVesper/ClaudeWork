 /* 文件功能：在指定温度和压强下求含气相与凝相的化学平衡组成。
 * 输入：候选物种、标准化学势、压强和迭代选项。
 * 输出：各物种摩尔数、相稳定性与收敛状态。 */  
#ifndef EQUILIBRIUM_SOLVER_H
#define EQUILIBRIUM_SOLVER_H

#include "gibbs_objective.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EQ_OK=0, EQ_INVALID_INPUT, EQ_NUMERIC_ERROR, EQ_LINEAR_FAILURE,
    EQ_LINE_SEARCH_FAILURE, EQ_STAGNATION, EQ_MAX_ITERATIONS,
    EQ_PHASE_CYCLE, EQ_PHASE_DEGENERATE
} EquilibriumStatus;

typedef enum {
    PHASE_UNAVAILABLE=0, PHASE_ABSENT, PHASE_ACTIVE, PHASE_BOUNDARY
} EquilibriumPhaseStatus;

typedef struct {
    int max_iterations, max_backtracks;
    double tol_equilibrium, tol_balance, tol_total;
    double initial_fraction;
    double max_log_step;
    double armijo_c, min_step;
    int max_phase_iterations;
    double tol_phase, tol_phase_add, tol_phase_amount;
} EquilibriumOptions;

typedef struct {
    EquilibriumStatus status;
    int converged, iterations, backtracks;
    double n[BALANCE_MAX_PRODUCTS];
    double z[BALANCE_MAX_PRODUCTS];
    double lambda[CHEM_MAX_ELEMENTS];
    double n_g, z_g;
    double max_equilibrium_residual, max_balance_residual, total_residual;
    double merit;
    double Phi, g;
    int phase_iterations, phase_additions, phase_removals;
    size_t active_count;
    int active_condensed[BALANCE_MAX_PRODUCTS];
    EquilibriumPhaseStatus phase_status[BALANCE_MAX_PRODUCTS];
    double phase_stability[BALANCE_MAX_PRODUCTS];
    double max_condensed_residual, max_phase_violation;
} EquilibriumResult;

/* 功能：生成平衡迭代的默认容差及步长选项。
 * 输入：无。
 * 输出：返回 EquilibriumOptions。 */
EquilibriumOptions equilibrium_default_options(void);

/* 功能：将平衡求解状态转换为文字。
 * 输入：status。
 * 输出：返回状态名称字符串。 */
const char *equilibrium_status_name(EquilibriumStatus status);

/* 功能：在固定温度与绝对压强下求气相和可出现凝相的平衡组分。
 * 输入：balance、standard、p、initial_n、options、out、error、error_size。
 * 输出：返回 EquilibriumStatus，并写入全部 n_j 与相稳定性。 */
EquilibriumStatus equilibrium_solve(
    const MassBalance *balance, const GibbsStandard *standard, double p,
    const double *initial_n, const EquilibriumOptions *options,
    EquilibriumResult *out, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
