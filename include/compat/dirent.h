#ifndef COMPAT_DIRENT_H
#define COMPAT_DIRENT_H

/*
 * compat/dirent.h — dirent (opendir/readdir/closedir) 声明（MSVC 兼容）
 *
 * Windows（MSVC）不提供 <dirent.h>，此头文件提供等效接口。
 * 仅当 _WIN32 定义时生效；Linux 下直接使用系统 <dirent.h>。
 *
 * 实现见 src/compat/dirent.c，使用 FindFirstFile / FindNextFile 封装。
 *
 * d_type 支持有限：
 *   DT_DIR  — 由 FindFirstFile 的 dwFileAttributes 判断
 *   DT_REG  — 非目录文件
 *   DT_LNK  — Windows 上不做特殊处理（Reparse Point 不单独映射为链接）
 *   DT_UNKNOWN — 无法判断时使用
 */

#ifdef _WIN32

#ifdef __cplusplus
extern "C" {
#endif

#include <windows.h>
#include <stdlib.h>
#include <string.h>

/* ── 目录项类型常量 ───────────────────────────────── */
#ifndef DT_UNKNOWN
#  define DT_UNKNOWN     0
#  define DT_DIR         4
#  define DT_REG         8
#  define DT_LNK        10
#endif

/* ── dirent 结构体 ──────────────────────────────────── */
struct dirent {
    char   d_name[MAX_PATH];   /* 文件名（不含路径） */
    unsigned char d_type;      /* 文件类型 */
};

/* ── DIR 结构体（不透明句柄） ───────────────────────── */
typedef struct _DIR {
    HANDLE          handle;       /* FindFirstFile handle, INVALID_HANDLE_VALUE 表示未开始 */
    WIN32_FIND_DATAA find_data;   /* 当前查找到的文件信息 */
    struct dirent   entry;        /* 当前条目，readdir 返回指向此 */
    char           *pattern;      /* 搜索模式（path\*），由 opendir 分配 */
    int             first;       /* 1 = 首次调用 readdir 时需先调用 FindFirstFile */
    int             finished;     /* 1 = 已无更多文件 */
} DIR;

/* ── API ─────────────────────────────────────────────── */
DIR           *opendir(const char *path);
struct dirent *readdir(DIR *dirp);
int            closedir(DIR *dirp);
void           rewinddir(DIR *dirp);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
#endif /* COMPAT_DIRENT_H */
