/**
 * @file compat/dirent.c
 * @brief opendir / readdir / closedir for Windows (MSVC)
 *
 * Implements the <dirent.h> API used by playlist, library, and config
 * modules to enumerate directories.  Wraps FindFirstFile / FindNextFile.
 *
 * Compile only under WIN32:
 *   #ifdef _WIN32  →  compile this file
 *
 * @note d_type is approximated from dwFileAttributes:
 *       FILE_ATTRIBUTE_DIRECTORY → DT_DIR, otherwise DT_REG.
 *       Reparse points are not reported as DT_LNK.
 */

#ifdef _WIN32

#include "compat/dirent.h"
#include <errno.h>

/* ── helpers ───────────────────────────────────────────────────────── */

/**
 * Build search pattern "path\\*" so FindFirstFile enumerates all entries.
 * Assumes path is NUL-terminated.  Returns malloc'd string or NULL on
 * overflow / allocation failure (sets errno).
 */
static char *build_pattern(const char *path)
{
    size_t len;

    if (!path || !*path) {
        errno = ENOENT;
        return NULL;
    }

    len = strlen(path);
    /* Need len + "\\*" + NUL */
    if (len > MAX_PATH - 3) {
        errno = ENOMEM;
        return NULL;
    }

    char *pattern = (char *)malloc(len + 4);
    if (!pattern) {
        errno = ENOMEM;
        return NULL;
    }

    /* Strip trailing backslash / slash (preserve "C:\" root though) */
    while (len > 1 && (path[len - 1] == '\\' || path[len - 1] == '/'))
        len--;

    memcpy(pattern, path, len);
    pattern[len]     = '\\';
    pattern[len + 1] = '*';
    pattern[len + 2] = '\0';
    return pattern;
}

/* ── opendir ───────────────────────────────────────────────────────── */

DIR *opendir(const char *path)
{
    DIR *dirp;

    if (!path || !*path) {
        errno = ENOENT;
        return NULL;
    }

    dirp = (DIR *)calloc(1, sizeof(DIR));
    if (!dirp) {
        errno = ENOMEM;
        return NULL;
    }

    dirp->handle   = INVALID_HANDLE_VALUE;
    dirp->first    = 1;
    dirp->finished = 0;

    dirp->pattern = build_pattern(path);
    if (!dirp->pattern) {
        /* errno already set by build_pattern */
        free(dirp);
        return NULL;
    }

    return dirp;
}

/* ── readdir ───────────────────────────────────────────────────────── */

struct dirent *readdir(DIR *dirp)
{
    BOOL ok;

    if (!dirp || dirp->finished)
        return NULL;

    if (dirp->first) {
        /* First call — start enumeration */
        dirp->first = 0;
        dirp->handle = FindFirstFileA(dirp->pattern, &dirp->find_data);
        if (dirp->handle == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
                dirp->finished = 1;
                errno = 0;
            } else {
                errno = EIO;
            }
            return NULL;
        }
        /* Populate entry */
        strncpy(dirp->entry.d_name, dirp->find_data.cFileName,
                sizeof(dirp->entry.d_name) - 1);
        dirp->entry.d_name[sizeof(dirp->entry.d_name) - 1] = '\0';
        dirp->entry.d_type = (dirp->find_data.dwFileAttributes
                              & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
        return &dirp->entry;
    }

    /* Subsequent calls */
    ok = FindNextFileA(dirp->handle, &dirp->find_data);
    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_NO_MORE_FILES) {
            dirp->finished = 1;
            errno = 0;
        } else {
            errno = EIO;
        }
        return NULL;
    }

    strncpy(dirp->entry.d_name, dirp->find_data.cFileName,
            sizeof(dirp->entry.d_name) - 1);
    dirp->entry.d_name[sizeof(dirp->entry.d_name) - 1] = '\0';
    dirp->entry.d_type = (dirp->find_data.dwFileAttributes
                          & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
    return &dirp->entry;
}

/* ── closedir ──────────────────────────────────────────────────────── */

int closedir(DIR *dirp)
{
    if (!dirp) {
        errno = EBADF;
        return -1;
    }

    if (dirp->handle != INVALID_HANDLE_VALUE)
        FindClose(dirp->handle);

    free(dirp->pattern);
    free(dirp);
    return 0;
}

/* ── rewinddir ─────────────────────────────────────────────────────── */

void rewinddir(DIR *dirp)
{
    if (!dirp) return;

    /* Close current search handle */
    if (dirp->handle != INVALID_HANDLE_VALUE) {
        FindClose(dirp->handle);
        dirp->handle = INVALID_HANDLE_VALUE;
    }

    /* Reset state so next readdir calls FindFirstFile again */
    dirp->first    = 1;
    dirp->finished = 0;
    memset(&dirp->find_data, 0, sizeof(dirp->find_data));
    memset(dirp->entry.d_name, 0, sizeof(dirp->entry.d_name));
    dirp->entry.d_type = DT_UNKNOWN;
}

#endif /* _WIN32 */
