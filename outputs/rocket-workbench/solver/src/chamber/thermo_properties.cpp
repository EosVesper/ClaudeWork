/* 文件功能：解析 NASA 九系数数据库并计算单组分标准物性。
 * 输入：数据库路径、物种名称和温度 K。
 * 输出：标准摩尔焓、熵、定压比热和摩尔质量。 */ 
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include "thermo_properties.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#define R_GAS       THERMO_R0
#define NAME_MAX    18
#define MAX_RANGES  8
#define MSG_LEN     512

typedef struct {
    double T_low;
    double T_high;
    double a[9];
} TempRange;

typedef struct {
    char     name[NAME_MAX + 1];
    double   molecular_weight;
    int      range_count;
    TempRange ranges[MAX_RANGES];
} Species;

typedef struct {
    Species *species;
    int      count;
} ThermoDB;

typedef enum {
    THERMO_OK          = 0,
    THERMO_ERR_ARG     = -1,
    THERMO_ERR_FILE    = -2,
    THERMO_ERR_MEM     = -3,
    THERMO_ERR_PARSE   = -4,
    THERMO_ERR_SPECIES = -5,
    THERMO_ERR_NO_DATA = -6,
    THERMO_ERR_TEMP    = -7
} ThermoStatus;

static ThermoDB g_db = { NULL, 0 };
static int      g_ready = 0;
static char     g_error[MSG_LEN] = "";

/* 功能：删除字符串首尾的空白字符。
 * 输入：s。
 * 输出：就地更新字符串。 */
static void str_trim(char *s)
{
    char *p = s;
    size_t len;

    while (*p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }
    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

/* 功能：不区分大小写地比较两个字符串。
 * 输入：a、b。
 * 输出：返回布尔判断值。 */
static int str_iequals(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

/* 功能：不区分大小写地查找子串。
 * 输入：text、part。
 * 输出：返回布尔判断值。 */
static int str_icontains(const char *text, const char *part)
{
    size_t n = strlen(part);
    const char *p;

    if (n == 0) {
        return 1;
    }
    for (p = text; *p != '\0'; ++p) {
        size_t i;
        for (i = 0; i < n; ++i) {
            if (p[i] == '\0' ||
                tolower((unsigned char)p[i]) != tolower((unsigned char)part[i])) {
                break;
            }
        }
        if (i == n) {
            return 1;
        }
    }
    return 0;
}

/* 功能：提取 NASA 记录中的物种名称。
 * 输入：line、out。
 * 输出：写入名称缓冲区。 */
static void extract_name(const char *line, char *out)
{
    int i = 0;

    while (i < NAME_MAX && line[i] != '\0' && !isspace((unsigned char)line[i])) {
        out[i] = line[i];
        ++i;
    }
    out[i] = '\0';
}

/* 功能：格式化并保存最近一次物性错误。
 * 输入：fmt、...。
 * 输出：更新全局错误字符串。 */
static void set_error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(g_error, sizeof(g_error), fmt, ap);
    va_end(ap);
}

/* 功能：解析 NASA 固定列宽字段中的浮点数。
 * 输入：line、pos、len、out。
 * 输出：成功返回 1 并写入数值。 */
static int parse_fixed_double(const char *line, size_t pos, size_t len, double *out)
{
    char field[32];
    char *end = NULL;
    size_t i;

    if (strlen(line) < pos + 1) {
        return 0;
    }
    if (len > sizeof(field) - 1) {
        len = sizeof(field) - 1;
    }
    for (i = 0; i < len && line[pos + i] != '\0'; ++i) {
        field[i] = line[pos + i];
    }
    field[i] = '\0';

    *out = strtod(field, &end);
    return end != field;
}

/* 功能：从连续记录行读取九系数多项式参数。
 * 输入：line1、line2、a。
 * 输出：成功返回 1 并写入九个系数。 */
static int parse_coeffs(const char *line1, const char *line2, double a[9])
{
    char buf[512];
    double v[10];
    int n = 0;
    char *p;

    snprintf(buf, sizeof(buf), "%s %s", line1, line2);
    for (p = buf; *p != '\0'; ++p) {
        if (*p == 'D' || *p == 'd') {
            *p = 'E';
        }
    }

    p = buf;
    while (n < 10) {
        char *end = NULL;
        double x = strtod(p, &end);
        if (end == p) {
            break;
        }
        v[n++] = x;
        p = end;
    }

    if (n == 9) {
        memcpy(a, v, 7 * sizeof(double));
        a[7] = v[7];
        a[8] = v[8];
        return 1;
    }
    if (n == 10) {

        memcpy(a, v, 7 * sizeof(double));
        a[7] = v[8];
        a[8] = v[9];
        return 1;
    }
    return 0;
}

/* 功能：识别 NASA 物种记录的名称行。
 * 输入：line。
 * 输出：返回布尔判断值。 */
static int is_name_line(const char *line)
{
    unsigned char c;

    if (line[0] == '\0') {
        return 0;
    }
    c = (unsigned char)line[0];
    if (isspace(c) || c == '!' || c == '-' || c == '+' || c == '.') {
        return 0;
    }
    if (isdigit(c)) {
        return 0;
    }
    return 1;
}

/* 功能：解析一个物种的名称、摩尔质量和温度区间。
 * 输入：lines、line_count、start、sp、next。
 * 输出：成功返回 1 并写入 Species 和下一行位置；失败返回 0。 */
static int parse_species(char **lines, int line_count, int start,
                         Species *sp, int *next)
{
    const char *comp;
    int nrange;
    int k;
    int j;

    if (start + 1 >= line_count) {
        return 0;
    }

    comp = lines[start + 1];
    if (comp[0] != ' ') {
        return 0;
    }

    char comp_copy[512];
    strncpy(comp_copy, comp, sizeof(comp_copy) - 1);
    comp_copy[sizeof(comp_copy) - 1] = '\0';

    char* token = strtok(comp_copy, " \t");
    char* last_token = NULL;
    char* prev_token = NULL;
    while (token != NULL) {
        prev_token = last_token;
        last_token = token;
        token = strtok(NULL, " \t");
    }
    double mw = 0.0;
    if (prev_token != NULL) {

        mw = atof(prev_token);
    }

    nrange = atoi(comp);
    if (nrange < 0 || nrange > MAX_RANGES) {
        return 0;
    }

    memset(sp, 0, sizeof(*sp));
    extract_name(lines[start], sp->name);
    if (sp->name[0] == '\0') {
        return 0;
    }

    sp->molecular_weight = mw;

    j = start + 2;
    for (k = 0; k < nrange; ++k) {
        TempRange *r = &sp->ranges[k];

        if (j + 2 >= line_count) {
            return 0;
        }
        if (!parse_fixed_double(lines[j], 0, 11, &r->T_low)) {
            return 0;
        }
        if (!parse_fixed_double(lines[j], 11, 11, &r->T_high)) {
            return 0;
        }
        if (!(r->T_low > 0.0) || !(r->T_high > r->T_low)) {
            return 0;
        }
        if (!parse_coeffs(lines[j + 1], lines[j + 2], r->a)) {
            return 0;
        }
        j += 3;
    }

    if (nrange == 0) {

        if (j >= line_count || lines[j][0] != ' ') {
            return 0;
        }
        ++j;
    }

    sp->range_count = nrange;
    *next = j;
    return 1;
}

/* 功能：读取完整 NASA 数据库文件。
 * 输入：path、text_out、size_out。
 * 输出：成功返回 THERMO_OK 并写入文本及长度；失败返回错误码。 */
static int read_file(const char *path, char **text_out, long *size_out)
{
    FILE *fp;
    long file_size;
    char *text;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return THERMO_ERR_FILE;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return THERMO_ERR_FILE;
    }
    file_size = ftell(fp);
    if (file_size <= 0) {
        fclose(fp);
        return THERMO_ERR_FILE;
    }
    rewind(fp);

    text = (char *)malloc((size_t)file_size + 1);
    if (text == NULL) {
        fclose(fp);
        return THERMO_ERR_MEM;
    }
    if (fread(text, 1, (size_t)file_size, fp) != (size_t)file_size) {
        free(text);
        fclose(fp);
        return THERMO_ERR_FILE;
    }
    text[file_size] = '\0';
    fclose(fp);

    *text_out = text;
    *size_out = file_size;
    return THERMO_OK;
}

/* 功能：把数据库文本分割为记录行。
 * 输入：text、size、lines_out、count_out。
 * 输出：成功返回 THERMO_OK 并写入行指针数组及行数；失败返回错误码。 */
static int split_lines(char *text, long size, char ***lines_out, int *count_out)
{
    int max_lines = (int)(size / 2) + 2;
    char **lines;
    int count = 0;
    int i;

    lines = (char **)malloc(sizeof(char *) * (size_t)max_lines);
    if (lines == NULL) {
        return THERMO_ERR_MEM;
    }

    lines[count++] = text;
    for (i = 0; i < (int)size; ++i) {
        if (text[i] == '\n') {
            text[i] = '\0';
            if (text[i + 1] != '\0' && count < max_lines) {
                lines[count++] = &text[i + 1];
            }
        }
    }
    for (i = 0; i < count; ++i) {
        size_t len = strlen(lines[i]);
        if (len > 0 && lines[i][len - 1] == '\r') {
            lines[i][len - 1] = '\0';
        }
    }

    *lines_out = lines;
    *count_out = count;
    return THERMO_OK;
}

/* 功能：遍历数据库记录并建立物种索引。
 * 输入：lines、line_count、list_out、count_out。
 * 输出：成功返回 THERMO_OK 并写入物种数组及数量；失败返回错误码。 */
static int parse_database(char **lines, int line_count,
                          Species **list_out, int *count_out)
{
    Species *list = NULL;
    int list_count = 0;
    int list_cap = 0;
    int i;

    *list_out = NULL;
    *count_out = 0;

    i = 0;
    while (i < line_count) {
        char name[NAME_MAX + 1];
        Species tmp;
        int next = 0;

        if (strncmp(lines[i], "END", 3) == 0) {
            ++i;
            continue;
        }
        if (!is_name_line(lines[i])) {
            ++i;
            continue;
        }
        extract_name(lines[i], name);
        if (str_iequals(name, "thermo")) {
            ++i;
            continue;
        }
        if (!parse_species(lines, line_count, i, &tmp, &next)) {
            ++i;
            continue;
        }

        if (list_count == list_cap) {
            int new_cap = list_cap ? list_cap * 2 : 1024;
            Species *tmp_list = (Species *)realloc(
                list, sizeof(Species) * (size_t)new_cap);
            if (tmp_list == NULL) {
                free(list);
                return THERMO_ERR_MEM;
            }
            list = tmp_list;
            list_cap = new_cap;
        }
        list[list_count++] = tmp;
        i = next;
    }

    *list_out = list;
    *count_out = list_count;
    return THERMO_OK;
}

/* 功能：按 NASA 九系数计算指定温区的标准摩尔焓。
 * 输入：r、T。
 * 输出：返回 J/mol。 */
static double enthalpy_J_per_mol(const TempRange *r, double T)
{
    const double *a = r->a;
    double h_over_rt;

    h_over_rt = -a[0] / (T * T)
              + a[1] * log(T) / T
              + a[2]
              + a[3] * T / 2.0
              + a[4] * T * T / 3.0
              + a[5] * T * T * T / 4.0
              + a[6] * T * T * T * T / 5.0
              + a[7] / T;

    return R_GAS * T * h_over_rt;
}

/* 功能：按 NASA 九系数计算指定温区的标准摩尔熵。
 * 输入：r、T。
 * 输出：返回 J/(mol K)。 */
static double enthalpy_S_per_mol(const TempRange* r, double T)
{
    const double* a = r->a;
    double s_over_rt;

    s_over_rt = -a[0] / (2.0*T * T)
        - a[1] / T
        + a[2] * log(T)
        + a[3] * T
        + a[4] * T * T / 2.0
        + a[5] * T * T * T / 3.0
        + a[6] * T * T * T * T / 4.0
        + a[8];

    return R_GAS * s_over_rt;
}

/* 功能：按 NASA 九系数计算指定温区的标准摩尔定压比热。
 * 输入：r、T。
 * 输出：返回 J/(mol K)。 */
static double enthalpy_Cp_per_mol(const TempRange* r, double T)
{
    const double* a = r->a;
    double C_over_rt;

    C_over_rt = a[0] / (T * T)
        + a[1] / T
        + a[2]
        + a[3] * T
        + a[4] * T * T
        + a[5] * T * T * T
        + a[6] * T * T * T * T;

    return R_GAS * C_over_rt;
}

/* 功能：取得默认数据库文件名。
 * 输入：无。
 * 输出：返回路径字符串。 */
const char *thermo_default_file(void);

/* 功能：加载并解析指定的 NASA 九系数数据库。
 * 输入：path。
 * 输出：成功返回 0；失败记录错误信息。 */
int         thermo_load(const char *path);

/* 功能：释放已加载的 NASA 数据库。
 * 输入：无。
 * 输出：清空全局数据库状态。 */
void        thermo_free(void);

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
const char *thermo_default_file(void)
{
    static const char *paths[] = {
        "thermo.inp"
    };
    size_t k;

    for (k = 0; k < sizeof(paths) / sizeof(paths[0]); ++k) {
        FILE *probe = fopen(paths[k], "rb");
        if (probe != NULL) {
            fclose(probe);
            return paths[k];
        }
    }
    return NULL;
}

/* 功能：加载并解析指定的 NASA 九系数数据库。
 * 输入：path。
 * 输出：成功返回 0；失败记录错误信息。 */
int thermo_load(const char *path)
{
    char *text = NULL;
    char **lines = NULL;
    Species *list = NULL;
    int line_count = 0;
    int count = 0;
    long size = 0;
    int rc;

    if (path == NULL) {
        set_error("数据文件路径不能为空。");
        return THERMO_ERR_ARG;
    }

    rc = read_file(path, &text, &size);
    if (rc != THERMO_OK) {
        set_error("无法打开或读取数据文件: %s", path);
        return rc;
    }
    rc = split_lines(text, size, &lines, &line_count);
    if (rc != THERMO_OK) {
        free(text);
        set_error("内存不足。");
        return rc;
    }
    rc = parse_database(lines, line_count, &list, &count);
    free(lines);
    free(text);
    if (rc != THERMO_OK) {
        set_error("内存不足。");
        return rc;
    }
    if (count == 0) {
        free(list);
        set_error("数据文件里没有解析到任何物质: %s", path);
        return THERMO_ERR_PARSE;
    }

    thermo_free();
    g_db.species = list;
    g_db.count = count;
    g_ready = 1;
    g_error[0] = '\0';
    return THERMO_OK;
}

/* 功能：释放已加载的 NASA 数据库。
 * 输入：无。
 * 输出：清空全局数据库状态。 */
void thermo_free(void)
{
    free(g_db.species);
    g_db.species = NULL;
    g_db.count = 0;
    g_ready = 0;
}

/* 功能：取得最近一次物性错误信息。
 * 输入：无。
 * 输出：返回错误字符串。 */
const char *thermo_last_error(void)
{
    return g_error;
}

/* 功能：按物种名称和温度求标准摩尔焓、熵及定压比热。
 * 输入：formula、T_K。
 * 输出：返回 ThermoResult。 */
ThermoResult thermo_h(const char *formula, double T_K)
{
    char query[64];
    const Species *sp = NULL;
    const TempRange *range = NULL;
    int i;
    ThermoResult result;
    result.H = NAN;
    result.Cp = NAN;
    result.S = NAN;
    result.MW = NAN;

    g_error[0] = '\0';

    if (formula == NULL) {
        set_error("化学式不能为空。");
        return result;
    }
    snprintf(query, sizeof(query), "%s", formula);
    str_trim(query);
    if (query[0] == '\0') {
        set_error("化学式不能为空。");
        return result;
    }
    if (!(T_K > 0.0)) {
        set_error("温度必须是大于 0 的数（单位 K）。");
        return result;
    }

    if (!g_ready) {
        const char *path = thermo_default_file();
        if (path == NULL) {
            set_error("找不到 thermo.inp，请把数据文件和程序放在一起，"
                      "或先用 thermo_load() 指定路径。");
            return result;
        }
        if (thermo_load(path) != THERMO_OK) {
            if (g_error[0] == '\0') {
                set_error("读取数据文件失败: %s", path);
            }
            return result;
        }
    }

    for (i = 0; i < g_db.count; ++i) {
        if (str_iequals(g_db.species[i].name, query)) {
            sp = &g_db.species[i];
            break;
        }
    }
    if (sp == NULL) {
        char sug[MSG_LEN];
        size_t used = 0;
        int shown = 0;

        sug[0] = '\0';
        for (i = 0; i < g_db.count && shown < 12; ++i) {
            int w;
            if (!str_icontains(g_db.species[i].name, query)) {
                continue;
            }
            w = snprintf(sug + used, sizeof(sug) - used, "%s%s",
                         shown ? ", " : "", g_db.species[i].name);
            if (w <= 0 || (size_t)w >= sizeof(sug) - used) {
                break;
            }
            used += (size_t)w;
            ++shown;
        }
        if (shown > 0) {
            set_error("未找到物质 \"%s\"。相近的物质: %s", query, sug);
        } else {
            set_error("未找到物质 \"%s\"。", query);
        }
        return result;
    }

    if (sp->range_count == 0) {
        set_error("物质 \"%s\" 在文件中只是反应物占位条目，没有温度区间和系数。",
                  sp->name);
        return result;
    }

    for (i = 0; i < sp->range_count; ++i) {
        if (T_K >= sp->ranges[i].T_low && T_K <= sp->ranges[i].T_high) {
            range = &sp->ranges[i];
            break;
        }
    }
    if (range == NULL) {
        char list[256];
        size_t used = 0;

        list[0] = '\0';
        for (i = 0; i < sp->range_count; ++i) {
            int w = snprintf(list + used, sizeof(list) - used, "[%.3f, %.3f] ",
                             sp->ranges[i].T_low, sp->ranges[i].T_high);
            if (w <= 0 || (size_t)w >= sizeof(list) - used) {
                break;
            }
            used += (size_t)w;
        }
        set_error("温度 %.2f K 不在物质 \"%s\" 的数据范围内。可用区间: %sK",
                  T_K, sp->name, list);
        return result;
    }

    result.H = enthalpy_J_per_mol(range, T_K);
    result.S = enthalpy_S_per_mol(range, T_K);
    result.Cp = enthalpy_Cp_per_mol(range, T_K);
    result.MW = sp->molecular_weight;
    return result;
}

#ifndef THERMO_NO_MAIN

/* 功能：运行单组分 NASA 物性示例。
 * 输入：无。
 * 输出：输出物性值并返回退出码。 */
int main(void)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    ThermoResult CO = thermo_h("AL-", 500);
    printf("标准焓值 %lf J/mol 标准熵值 %lf J/(mol*K) 定压比热容 %f J/(mol*K）摩尔质量 %f g/mol", CO.H, CO.S,CO.Cp,CO.MW);
    thermo_free();
    return 0;
}
#endif
