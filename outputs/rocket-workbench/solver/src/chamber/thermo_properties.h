/* 文件功能：解析 NASA 九系数数据库并计算单组分标准物性。
 * 输入：数据库路径、物种名称和温度 K。
 * 输出：标准摩尔焓、熵、定压比热和摩尔质量。 */ 
#ifndef THERMO_PROPERTIES_H
#define THERMO_PROPERTIES_H

#ifdef __cplusplus
extern "C" {
#endif

#define THERMO_R0 8.314462618
#define THERMO_NASA_GAS_P0 100000.0
#define THERMO_GAS_P0 101325.0

typedef struct {
    double H;
    double Cp;
    double S;
    double MW;
} ThermoResult;

/* 功能：加载并解析指定的 NASA 九系数数据库。
 * 输入：path。
 * 输出：成功返回 0；失败记录错误信息。 */
int thermo_load(const char *path);

/* 功能：释放已加载的 NASA 数据库。
 * 输入：无。
 * 输出：清空全局数据库状态。 */
void thermo_free(void);

/* 功能：按物种名称和温度求标准摩尔焓、熵及定压比热。
 * 输入：formula、T_K。
 * 输出：返回 ThermoResult。 */
ThermoResult thermo_h(const char *formula, double T_K);

/* 功能：取得最近一次物性错误信息。
 * 输入：无。
 * 输出：返回错误字符串。 */
const char *thermo_last_error(void);

/* 功能：取得默认数据库文件名。
 * 输入：无。
 * 输出：返回路径字符串。 */
const char *thermo_default_file(void);

#ifdef __cplusplus
}
#endif
#endif
