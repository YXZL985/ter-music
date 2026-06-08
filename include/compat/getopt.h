#ifndef COMPAT_GETOPT_H
#define COMPAT_GETOPT_H

/*
 * compat/getopt.h — getopt / getopt_long 声明（MSVC 兼容）
 *
 * Windows（MSVC）不提供 <getopt.h>，此头文件提供等效接口。
 * 仅当 _WIN32 定义时生效；Linux 下直接使用系统 <getopt.h>。
 *
 * 实现见 src/compat/getopt.c，compile-once 集成。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ── getopt 基础变量 ──────────────────────────────── */
extern char *optarg;
extern int  optind;
extern int  opterr;
extern int  optopt;
extern int  optreset;     /* 允许重复扫描 */

/* ── getopt 选项结构（用于 getopt_long） ──────────── */
struct option {
    const char *name;
    int         has_arg;
    int        *flag;
    int         val;
};

/* has_arg 常量 */
#define no_argument        0
#define required_argument  1
#define optional_argument  2

/* ── API ───────────────────────────────────────────── */
int getopt(int argc, char * const argv[], const char *optstring);
int getopt_long(int argc, char * const argv[],
                const char *optstring,
                const struct option *longopts, int *longindex);

#ifdef __cplusplus
}
#endif

#endif /* COMPAT_GETOPT_H */
