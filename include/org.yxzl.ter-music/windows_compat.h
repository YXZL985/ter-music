#ifndef WINDOWS_COMPAT_H
#define WINDOWS_COMPAT_H

/*
 * windows_compat.h — Windows 平台统一兼容层
 *
 * 本头文件在 WIN32 下被包含，将所有 Linux 系统 API 调用
 * 宏映射到 Windows 等效 API。所有源文件只需包含本头文件
 * 即可在 Windows + MSVC 下编译。
 *
 * 用法：
 *   所有 .c 文件 #include "windows_compat.h"
 *   （通常在公共头中统一包含，而非逐个修改源文件）
 */

#ifdef _WIN32

/* ── 防止 Win32 头文件过度包含 ────────────────────────── */
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <direct.h>      /* _getcwd, _chdir */
#include <io.h>          /* _read, _write, _access, _chsize */
#include <process.h>     /* _getpid */
#include <stdlib.h>

/* ── 确保 MSVC 不报安全警告 ────────────────────────────── */
#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4996)  /* POSIX 函数重名 */
#endif

/* ── unistd.h 替代 ─────────────────────────────────────── */
#define getcwd   _getcwd
#define chdir    _chdir
#define read(fd, buf, count)    _read((fd), (buf), (unsigned int)(count))
#define write(fd, buf, count)   _write((fd), (buf), (unsigned int)(count))
#define getpid   _getpid
#define access   _access
#define unlink   _unlink
#define rmdir    _rmdir

/* 预定义文件访问模式（与 Linux 兼容） */
#ifndef F_OK
#  define F_OK 0
#endif
#ifndef X_OK
#  define X_OK 1
#endif
#ifndef W_OK
#  define W_OK 2
#endif
#ifndef R_OK
#  define R_OK 4
#endif

/* sleep 替代 */
#define usleep(us)  Sleep((DWORD)((us) / 1000))
#define sleep(sec)  Sleep((DWORD)(sec) * 1000)

/* ssize_t 定义（MSVC 无 ssize_t） */
#ifdef _MSC_VER
#  ifndef _SSIZE_T_DEFINED
#    define _SSIZE_T_DEFINED
#    include <basetsd.h>
typedef SSIZE_T ssize_t;
#  endif
#endif

/* 确保 STDIN_FILENO / STDOUT_FILENO / STDERR_FILENO 已定义 */
#ifndef STDIN_FILENO
#  define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#  define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#  define STDERR_FILENO 2
#endif

/* ── sys/stat.h 替代 ──────────────────────────────────── */
#include <sys/stat.h>  /* MSVC 实际有 <sys/stat.h> */

#ifndef S_ISREG
#  define S_ISREG(m)  (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#  define S_ISDIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISLNK
#  define S_ISLNK(m)  (0)  /* Windows 无符号链接 */
#endif
#ifndef S_ISCHR
#  define S_ISCHR(m)  (((m) & _S_IFMT) == _S_IFCHR)
#endif
#ifndef S_ISBLK
#  define S_ISBLK(m)  (0)
#endif
#ifndef S_ISFIFO
#  define S_ISFIFO(m) (((m) & _S_IFMT) == _S_IFIFO)
#endif

#define stat  _stat
#define lstat _stat   /* Windows 无符号链接，lstat = stat */
#define fstat _fstat

/* 从 MSVC 的 _stat 结构体映射到 POSIX 字段名 */
#define st_atim   st_atime
#define st_mtim   st_mtime
#define st_ctim   st_ctime

/* ── dlfcn.h 替代 ──────────────────────────────────────── */
#define RTLD_LAZY   0
#define RTLD_NOW    0
#define RTLD_LOCAL  0
#define RTLD_GLOBAL 0

static inline void *dlopen(const char *lib, int flags) {
    (void)flags;
    return (void *)LoadLibraryA(lib);
}

static inline void *dlsym(void *handle, const char *name) {
    return (void *)GetProcAddress((HMODULE)handle, name);
}

static inline int dlclose(void *handle) {
    return FreeLibrary((HMODULE)handle) ? 0 : -1;
}

static inline const char *dlerror(void) {
    static char buf[256];
    DWORD err = GetLastError();
    if (err == 0) return NULL;
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err, 0, buf, sizeof(buf), NULL);
    return buf;
}

/* ── signal.h 补丁 ──────────────────────────────────────── */
/* 注意：sigaction() 在 Windows 上不完全等效，用 signal() 近似 */
#ifndef sigaction
struct sigaction {
    void (*sa_handler)(int);
    sigset_t sa_mask;
    int sa_flags;
};
#  define sigaction(sig, act, old)  \
    ((old ? (*(int *)(old) = 0) : 0), \
     signal((sig), (act) ? (act)->sa_handler : SIG_DFL) == SIG_ERR ? -1 : 0)
#endif

/* SIGHUP 在 Windows 上没有等效信号，映射到 SIGTERM 或忽略 */
#ifndef SIGHUP
#  define SIGHUP 1
#endif
#ifndef SIGKILL
#  define SIGKILL 9
#endif
#ifndef SIGPIPE
#  define SIGPIPE 13
#endif

/* ── getopt.h 替代 ──────────────────────────────────────── */
/* 需要单独包含 compat/getopt.h 获得完整 getopt_long 实现 */
/* 这里仅声明外部变量 */
#ifdef __cplusplus
extern "C" {
#endif
extern char *optarg;
extern int optind, opterr, optopt;
#ifdef __cplusplus
}
#endif

/* ── execinfo.h 替代（backtrace） ──────────────────────── */
/* 完整实现在 compat/execinfo.c，这里仅声明 */
#ifdef __cplusplus
extern "C" {
#endif
int backtrace(void **buffer, int size);
char **backtrace_symbols(void *const *buffer, int size);
void backtrace_symbols_fd(void *const *buffer, int size, int fd);
#ifdef __cplusplus
}
#endif

/* ── popen / pclose ─────────────────────────────────────── */
#define popen  _popen
#define pclose _pclose

/* ── mkstemp 替代 ───────────────────────────────────────── */
#include <io.h>
static inline int mkstemp(char *tmpl) {
    if (_mktemp_s(tmpl, strlen(tmpl) + 1) != 0) return -1;
    return _open(tmpl, _O_CREAT | _O_EXCL | _O_RDWR, _S_IREAD | _S_IWRITE);
}
/* mkstemps 是 mkstemp 带后缀长度参数 */
static inline int mkstemps(char *tmpl, int suffixlen) {
    /* 简单的实现：忽略 suffixlen，在 tmpl 中 XXXXXX 位置上操作 */
    size_t len = strlen(tmpl);
    if (len < 6) return -1;
    return mkstemp(tmpl);
}

/* ── iconv 替代 ─────────────────────────────────────────── */
/* 方案 A：使用 Win32 API MultiByteToWideChar / WideCharToMultiByte */
/* 若需要完整 iconv 接口，通过 vcpkg 安装 libiconv */
/* 这里提供简化的内联实现 */
#include <wchar.h>

typedef void *iconv_t;

static inline iconv_t iconv_open(const char *tocode, const char *fromcode) {
    (void)tocode;
    (void)fromcode;
    /* 简化：返回非 NULL 哨兵值 */
    return (iconv_t)1;
}

static inline int iconv_close(iconv_t cd) {
    (void)cd;
    return 0;
}

/* iconv 的简化 inline 实现 —— 仅处理 UTF-8 与本地编码间的转换 */
static inline size_t iconv(iconv_t cd,
                           char **inbuf, size_t *inbytesleft,
                           char **outbuf, size_t *outbytesleft) {
    (void)cd;
    if (!inbuf || !*inbuf || !outbuf || !*outbuf) return (size_t)-1;

    /* 尝试使用 WideCharToMultiByte/MultiByteToWideChar 进行 UTF-8 转换 */
    int len = MultiByteToWideChar(CP_UTF8, 0, *inbuf, (int)*inbytesleft, NULL, 0);
    if (len <= 0) return (size_t)-1;

    wchar_t *wide = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!wide) return (size_t)-1;
    MultiByteToWideChar(CP_UTF8, 0, *inbuf, (int)*inbytesleft, wide, len);
    wide[len] = L'\0';

    int out_len = WideCharToMultiByte(CP_ACP, 0, wide, len,
                                       *outbuf, (int)*outbytesleft, NULL, NULL);
    free(wide);
    if (out_len <= 0) return (size_t)-1;

    *inbuf += *inbytesleft;
    *inbytesleft = 0;
    *outbuf += out_len;
    *outbytesleft -= out_len;
    return 0;
}

/* ── 时间函数 ───────────────────────────────────────────── */
#include <time.h>
#include <sys/timeb.h>

/* clock_gettime(CLOCK_MONOTONIC, ...) */
static inline int clock_gettime_monotonic(struct timespec *ts) {
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER counter;
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    QueryPerformanceCounter(&counter);
    ts->tv_sec  = (time_t)(counter.QuadPart / freq.QuadPart);
    ts->tv_nsec = (long)((counter.QuadPart % freq.QuadPart) * 1000000000LL / freq.QuadPart);
    return 0;
}

#ifndef CLOCK_MONOTONIC
#  define CLOCK_MONOTONIC 1
#  define clock_gettime(clk_id, ts)                                 \
     ((clk_id) == CLOCK_MONOTONIC                                   \
      ? clock_gettime_monotonic(ts)                                 \
      : (clock_gettime_monotonic(ts), 0))  /* CLOCK_REALTIME 回退 */
#endif

/* 线程安全的 localtime */
#define localtime_r(t, tm)  localtime_s((tm), (t))

/* ── 字符串函数 ─────────────────────────────────────────── */
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp
#define strdup      _strdup

/* ── 路径分隔符 ─────────────────────────────────────────── */
static inline void path_normalize(char *path) {
    while (*path) {
        if (*path == '/') *path = '\\';
        path++;
    }
}

/* ── 最大路径长度 ───────────────────────────────────────── */
#ifndef PATH_MAX
#  define PATH_MAX MAX_PATH
#endif

/* ── 环境变量 ───────────────────────────────────────────── */
#define setenv(name, value, overwrite)  SetEnvironmentVariableA((name), (value))
#define unsetenv(name)                  SetEnvironmentVariableA((name), NULL)

/* ── IPv6 兼容 ──────────────────────────────────────────── */
#ifndef IN6_IS_ADDR_LOOPBACK
#  define IN6_IS_ADDR_LOOPBACK(a) \
     (((const uint32_t *)(a))[0] == 0  && ((const uint32_t *)(a))[1] == 0 && \
      ((const uint32_t *)(a))[2] == 0  && ((const uint32_t *)(a))[3] == htonl(1))
#endif

/* ── 恢复 MSVC 警告 ─────────────────────────────────────── */
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#endif /* _WIN32 */
#endif /* WINDOWS_COMPAT_H */
