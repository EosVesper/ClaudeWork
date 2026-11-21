/* 文件功能：把 NASA 九系数数据库规范化为"一个组分一条记录、多条温度段"。
 * 输入：原始数据库路径。
 * 输出：规范化后的数据库文件路径、合并统计与命名冲突清单。 */
#include "nozzle_database.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DB_MAX_LINES   40000
#define DB_MAX_RECORDS 4096
#define DB_MAX_RANGES  16
#define DB_LINE        256
#define DB_NAME        18

/* 一条原始记录。块行用行号引用，避免拷贝文本。 */
typedef struct {
    char name[DB_NAME + 1];
    int name_line;
    int meta_line;
    int block_start;   /* 第一个温度段的首行行号 */
    int range_count;
    int phase;
    double molar_mass;
} DbRecord;

/* 合并后的组分：一串原始记录的索引。 */
typedef struct {
    char name[DB_NAME + 1];
    int order;                  /* 首次出现的记录序号 */
    int count;
    int record[DB_MAX_RANGES];  /* 组成该组分的原始记录 */
} DbGroup;

/* 文件整体内容，按行切分。 */
typedef struct {
    char *buffer;
    char *line[DB_MAX_LINES];
    size_t count;
} DbFile;

/* 功能：把错误写入缓冲区。
 * 输入：error、size、message。
 * 输出：返回失败值 0。 */
static int fail(char *error, size_t size, const char *message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

/* 功能：按行切分文件内容，原地把换行替换为结束符。
 * 输入：file。
 * 输出：成功返回 1；行数过多返回 0。 */
static int split_lines(DbFile *file)
{
    size_t i = 0;
    char *p = file->buffer;
    file->count = 0;
    /* 跳过可能的 UTF-8 BOM。 */
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF)
        p += 3;
    while (*p) {
        char *eol;
        if (file->count >= DB_MAX_LINES) return 0;
        file->line[file->count++] = p;
        eol = strchr(p, '\n');
        if (!eol) break;
        *eol = '\0';
        p = eol + 1;
        /* 去掉 Windows 行尾的回车。 */
        if (file->count > 0) {
            size_t len = strlen(file->line[file->count - 1]);
            if (len > 0 && file->line[file->count - 1][len - 1] == '\r')
                file->line[file->count - 1][len - 1] = '\0';
        }
        (void)i;
    }
    return 1;
}

/* 功能：从固定列清取整数。
 * 输入：line、pos、width。
 * 输出：返回解析结果，失败返回 0。 */
static int field_int(const char *line, size_t pos, size_t width)
{
    char buffer[32];
    size_t len = strlen(line);
    if (pos >= len || width >= sizeof(buffer)) return 0;
    if (pos + width > len) width = len - pos;
    memcpy(buffer, line + pos, width);
    buffer[width] = '\0';
    return atoi(buffer);
}

/* 功能：从固定列清取浮点数。
 * 输入：line、pos、width。
 * 输出：返回解析结果，失败返回 0。 */
static double field_double(const char *line, size_t pos, size_t width)
{
    char buffer[32];
    size_t len = strlen(line);
    if (pos >= len || width >= sizeof(buffer)) return 0.0;
    if (pos + width > len) width = len - pos;
    memcpy(buffer, line + pos, width);
    buffer[width] = '\0';
    return atof(buffer);
}

/* 功能：取出行首的名称（到空白为止，最多 18 字符）。
 * 输入：line、out。
 * 输出：无。 */
static void field_name(const char *line, char *out)
{
    size_t i = 0;
    while (i < DB_NAME && line[i] && !isspace((unsigned char)line[i])) {
        out[i] = line[i];
        ++i;
    }
    out[i] = '\0';
}

/* 功能：判断两个名称忽略大小写是否相同。
 * 输入：a、b。
 * 输出：相同返回 1。 */
static int same_ignoring_case(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

/* 功能：读取并解析全部记录。
 * 输入：file、records、capacity、header_end、end_line、error、size。
 * 输出：成功返回记录数；失败返回 0。 */
static size_t parse_records(const DbFile *file, DbRecord *records, size_t capacity,
                            size_t *header_end, size_t *end_line,
                            char *error, size_t size)
{
    size_t i = 0, n = 0;
    int started = 0;
    while (i < file->count) {
        const char *line = file->line[i];
        if (!started) {
            if (strncmp(line, "thermo", 6) == 0) {
                started = 1;
                /* thermo 之后紧跟一行全局温度网格，属于头部。 */
                i += 2;
                *header_end = i;
            } else {
                ++i;
            }
            continue;
        }
        if (strncmp(line, "END PRODUCTS", 12) == 0) {
            *end_line = i;
            return n;
        }
        if (line[0] == '!' || line[0] == '\0') { ++i; continue; }
        if (!line[0] || isspace((unsigned char)line[0]))
            return (size_t)fail(error, size, "数据库格式异常：记录行以空白开头。");
        if (n >= capacity)
            return (size_t)fail(error, size, "数据库记录过多，超出解析上限。");
        {
            DbRecord *r = &records[n];
            field_name(line, r->name);
            r->name_line = (int)i;
            r->meta_line = (int)(i + 1);
            r->range_count = field_int(file->line[i + 1], 0, 2);
            r->phase = field_int(file->line[i + 1], 51, 1);
            r->molar_mass = field_double(file->line[i + 1], 52, 13);
            r->block_start = (int)(i + 2);
            if (r->range_count <= 0 || r->range_count > DB_MAX_RANGES)
                return (size_t)fail(error, size, "数据库格式异常：温度段数不合法。");
            ++n;
            i += 2 + (size_t)r->range_count * 3;
        }
    }
    return (size_t)fail(error, size, "数据库缺少 END PRODUCTS 标记。");
}

/* 功能：按精确名称合并记录。
 * 输入：records、count、groups、group_capacity。
 * 输出：返回合并后的组数。 */
static size_t merge_groups(const DbRecord *records, size_t count,
                           DbGroup *groups, size_t group_capacity)
{
    size_t n = 0, i;
    for (i = 0; i < count; ++i) {
        size_t g;
        for (g = 0; g < n; ++g)
            if (strcmp(groups[g].name, records[i].name) == 0) break;
        if (g == n) {
            if (n >= group_capacity) continue;
            memset(&groups[n], 0, sizeof(groups[n]));
            memcpy(groups[n].name, records[i].name, sizeof(groups[n].name) - 1);
            groups[n].order = (int)i;
            ++n;
        }
        if (groups[g].count < DB_MAX_RANGES)
            groups[g].record[groups[g].count++] = (int)i;
    }
    return n;
}

/* 功能：统计仅大小写不同的名称组，并记下被遮蔽的一方。
 * 输入：groups、n、report。
 * 输出：无。 */
static void find_case_collisions(const DbGroup *groups, size_t n, NozzleDatabase *report)
{
    size_t i, j;
    for (i = 0; i < n; ++i) {
        for (j = i + 1; j < n; ++j) {
            if (same_ignoring_case(groups[i].name, groups[j].name)) {
                ++report->case_collisions;
                /* 文件里靠后的一条会被 strcasecmp 式的查找遮蔽。 */
                if (report->shadowed_count < 8) {
                    char *slot = report->shadowed[report->shadowed_count++];
                    memset(slot, 0, sizeof(report->shadowed[0]));
                    memcpy(slot, groups[j].name, sizeof(report->shadowed[0]) - 1);
                }
            }
        }
    }
}

/* 功能：把规范化内容写入文件。
 * 输入：file、records、groups、group_count、header_end、end_line、path。
 * 输出：成功返回 1；失败返回 0。truncated 返回被截断的温度段数。 */
static int write_canonical(const DbFile *file, const DbRecord *records,
                           const DbGroup *groups, size_t group_count,
                           size_t header_end, size_t end_line, const char *path,
                           NozzleDatabase *report)
{
    FILE *out = fopen(path, "w");
    size_t h, g, t;
    size_t truncated = 0;
    if (!out) return 0;
    /* 首行写入统计信息，供下次命中缓存时直接读回，不必重新解析。 */
    fprintf(out, "! nozzle-canonical records=%zu species=%zu merged=%zu collisions=%zu\n",
            report->records, report->species, report->merged_groups,
            report->case_collisions);
    for (h = 0; h < header_end; ++h) fprintf(out, "%s\n", file->line[h]);
    for (g = 0; g < group_count; ++g) {
        const DbGroup *group = &groups[g];
        const DbRecord *first = &records[group->order];
        int total = 0, written = 0, k;
        char meta[DB_LINE];
        for (k = 0; k < group->count; ++k)
            total += records[group->record[k]].range_count;
        /* 物性解析器要求记录行首字符为空格，因此段数必须按两位右对齐书写；
         * 上限取 9，超过时截断并计数（实际数据最多 6 段，不会触发）。 */
        if (total > 9) {
            truncated += (size_t)(total - 9);
            total = 9;
        }
        fprintf(out, "%s\n", file->line[first->name_line]);
        snprintf(meta, sizeof(meta), "%s", file->line[first->meta_line]);
        meta[0] = ' ';
        meta[1] = (char)('0' + total % 10);
        fprintf(out, "%s\n", meta);
        /* 温度段按记录出现顺序依次写出，与合并前逐段一致。 */
        for (k = 0; k < group->count && written < total; ++k) {
            const DbRecord *r = &records[group->record[k]];
            int s;
            for (s = 0; s < r->range_count && written < total; ++s) {
                int base = r->block_start + s * 3;
                fprintf(out, "%s\n", file->line[base]);
                fprintf(out, "%s\n", file->line[base + 1]);
                fprintf(out, "%s\n", file->line[base + 2]);
                ++written;
            }
        }
    }
    for (t = end_line; t < file->count; ++t) fprintf(out, "%s\n", file->line[t]);
    fclose(out);
    report->truncated_ranges = truncated;
    return 1;
}

/* 功能：判断缓存文件是否可直接复用。
 * 输入：target、source。
 * 输出：可复用返回 1。 */
static int cache_valid(const char *target, const char *source)
{
    struct stat a, b;
    if (stat(target, &a) != 0 || a.st_size <= 0) return 0;
    if (stat(source, &b) != 0) return 0;
    return a.st_mtime >= b.st_mtime;
}

/* 功能：生成（或复用）规范化数据库。
 * 输入：source。
 * 输出：成功返回 1 并写入 report。 */
int nozzle_database_prepare(const char *source, NozzleDatabase *report,
                            char *error, size_t error_size)
{
    static DbFile file;
    static DbRecord records[DB_MAX_RECORDS];
    static DbGroup groups[DB_MAX_RECORDS];
    static char target[512];
    size_t header_end = 0, end_line = 0, record_count, group_count;
    FILE *in;
    long size;

    if (!report) return fail(error, error_size, "输出参数为空。");
    memset(report, 0, sizeof(*report));
    if (!source || !source[0])
        return fail(error, error_size, "数据库路径为空。");

    /* 目标文件：优先当前工作目录，失败时退到临时目录。 */
    {
        const char *tmp = getenv("TEMP");
        snprintf(target, sizeof(target), "thermo_canonical.inp");
        if (cache_valid(target, source)) {
            FILE *cached = fopen(target, "r");
            report->valid = 1;
            report->cached = 1;
            snprintf(report->path, sizeof(report->path), "%s", target);
            if (cached) {
                char head[256] = {0};
                if (fgets(head, sizeof(head), cached))
                    sscanf(head, "! nozzle-canonical records=%zu species=%zu merged=%zu collisions=%zu",
                           &report->records, &report->species,
                           &report->merged_groups, &report->case_collisions);
                fclose(cached);
            }
            return 1;
        }
        if (tmp && tmp[0]) {
            /* 仅记录备用位置，真正写入失败时再使用。 */
            (void)tmp;
        }
    }

    in = fopen(source, "rb");
    if (!in) return fail(error, error_size, "无法打开 NASA 数据库。");
    if (fseek(in, 0, SEEK_END) != 0 || (size = ftell(in)) <= 0) {
        fclose(in);
        return fail(error, error_size, "无法确定数据库长度。");
    }
    rewind(in);
    file.buffer = (char *)malloc((size_t)size + 1);
    if (!file.buffer) {
        fclose(in);
        return fail(error, error_size, "数据库内容内存不足。");
    }
    if (fread(file.buffer, 1, (size_t)size, in) != (size_t)size) {
        free(file.buffer);
        file.buffer = NULL;
        fclose(in);
        return fail(error, error_size, "读取数据库内容失败。");
    }
    file.buffer[size] = '\0';
    fclose(in);

    if (!split_lines(&file)) {
        free(file.buffer);
        file.buffer = NULL;
        return fail(error, error_size, "数据库行数超出解析上限。");
    }
    record_count = parse_records(&file, records, DB_MAX_RECORDS, &header_end, &end_line,
                                 error, error_size);
    if (record_count == 0) {
        free(file.buffer);
        file.buffer = NULL;
        return 0;
    }
    group_count = merge_groups(records, record_count, groups, DB_MAX_RECORDS);

    report->records = record_count;
    report->species = group_count;
    report->merged_groups = 0;
    {
        size_t g;
        for (g = 0; g < group_count; ++g)
            if (groups[g].count > 1) ++report->merged_groups;
    }
    find_case_collisions(groups, group_count, report);

    if (!write_canonical(&file, records, groups, group_count, header_end, end_line,
                         target, report)) {
        const char *tmp = getenv("TEMP");
        if (tmp && tmp[0]) {
            snprintf(target, sizeof(target), "%s/nozzle_thermo_canonical.inp", tmp);
            if (!write_canonical(&file, records, groups, group_count, header_end,
                                 end_line, target, report)) {
                free(file.buffer);
                file.buffer = NULL;
                return fail(error, error_size, "无法写出规范化数据库文件。");
            }
        } else {
            free(file.buffer);
            file.buffer = NULL;
            return fail(error, error_size, "无法写出规范化数据库文件。");
        }
    }
    free(file.buffer);
    file.buffer = NULL;

    report->valid = 1;
    snprintf(report->path, sizeof(report->path), "%s", target);
    if (error && error_size) error[0] = '\0';
    return 1;
}
