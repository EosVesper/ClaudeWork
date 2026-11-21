/* 文件功能：在给定区间内用 Brent 法求连续标量方程的根。
 * 输入：函数回调、异号区间端点及容差。
 * 输出：根的位置、残差和求解状态。 */ 
#ifndef BRENT_H
#define BRENT_H
typedef int (*RootFunction)(double x, void *context, double *f);
typedef enum { ROOT_OK=0, ROOT_INVALID, ROOT_CALLBACK, ROOT_NOT_BRACKETED,
               ROOT_STAGNATION, ROOT_LIMIT } RootStatus;
typedef struct { double x,f,low,high; int evaluations,iterations,bisections,interpolations; } RootResult;

/* 功能：用 Brent 法在异号区间中寻找零点。
 * 输入：f、context、low、high、x_tol、f_tol、limit、out。
 * 输出：返回 RootStatus，并写入 RootResult。 */
RootStatus brent_solve(RootFunction f, void *context, double low, double high,
                       double x_tol, double f_tol, int limit, RootResult *out);
#endif
