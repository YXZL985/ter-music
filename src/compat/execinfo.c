/**
 * @file compat/execinfo.c
 * @brief backtrace / backtrace_symbols for Windows (MSVC)
 *
 * Implements the <execinfo.h> API used by crash_handler in main.c:
 *   - backtrace()             : capture call stack frames via CaptureStackBackTrace
 *   - backtrace_symbols()     : resolve to printable strings via dbghelp
 *   - backtrace_symbols_fd()  : write resolved symbols to fd
 *
 * Compile only under WIN32:
 *   #ifdef _WIN32  →  compile this file
 *
 * @author ter-music team
 */

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>

/* Maximum depth for backtrace */
#define BT_MAX_FRAMES 128

/* ── backtrace ───────────────────────────────────────────────── */

int backtrace(void **buffer, int size)
{
    if (!buffer || size <= 0) return 0;

    /* CaptureStackBackTrace is available on Windows XP SP3+ / Vista+ */
    typedef USHORT (WINAPI *CaptureStackBackTrace_fn)(ULONG, ULONG, PVOID *, PULONG);
    static CaptureStackBackTrace_fn pCapture = NULL;
    static int initialized = 0;

    if (!initialized) {
        HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
        if (hKernel)
            pCapture = (CaptureStackBackTrace_fn)
                GetProcAddress(hKernel, "RtlCaptureStackBackTrace");
        initialized = 1;
    }

    if (pCapture) {
        int count = (int)pCapture(0, (ULONG)size, buffer, NULL);
        return count;
    }

    /* Fallback: use _AddressOfReturnAddress intrinsic (MSVC) */
    void *stack[BT_MAX_FRAMES];
    int i = 0;
    void **frame = (void **)_AddressOfReturnAddress();
    while (i < size && i < BT_MAX_FRAMES) {
        if (!frame || !*frame) break;
        stack[i++] = *frame;
        frame = (void **)((char *)frame + sizeof(void *));
    }
    if (i > 0) memcpy(buffer, stack, (size_t)i * sizeof(void *));
    return i;
}

/* ── backtrace_symbols ───────────────────────────────────────── */

char **backtrace_symbols(void *const *buffer, int size)
{
    char **result;
    int i;

    if (!buffer || size <= 0) return NULL;

    result = (char **)malloc((size_t)size * sizeof(char *));
    if (!result) return NULL;

    /* Try to initialise DbgHelp once */
    static int dbghelp_loaded = -1;  /* -1 = uninitialised, 0 = failed, 1 = ok */
    static HANDLE process = NULL;

    if (dbghelp_loaded == -1) {
        process = GetCurrentProcess();
        SymInitialize(process, NULL, TRUE);
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        dbghelp_loaded = 1;
    }

    for (i = 0; i < size; i++) {
        char sym_buf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO *sym = (SYMBOL_INFO *)sym_buf;
        char line_buf[128] = "";

        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen   = 255;

        /* Attempt symbol resolution */
        if (dbghelp_loaded == 1 &&
            SymFromAddr(process, (DWORD64)(uintptr_t)buffer[i], NULL, sym)) {
            /* Try to get line number */
            IMAGEHLP_LINE64 line;
            DWORD displacement;
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            if (SymGetLineFromAddr64(process, (DWORD64)(uintptr_t)buffer[i],
                                     &displacement, &line)) {
                snprintf(line_buf, sizeof(line_buf), " (%s:%lu)",
                         line.FileName, line.LineNumber);
            }

            size_t len = strlen(sym->Name) + strlen(line_buf) + 32;
            result[i] = (char *)malloc(len);
            if (result[i]) {
                snprintf(result[i], len, "%p: %s%s",
                         (void *)buffer[i], sym->Name, line_buf);
            }
        } else if (dbghelp_loaded == 1) {
            /* Address found but symbol not resolved — show as hex */
            size_t len = 32;
            result[i] = (char *)malloc(len);
            if (result[i])
                snprintf(result[i], len, "%p: <unknown>", (void *)buffer[i]);
        } else {
            /* DbgHelp not available — minimal hex dump */
            size_t len = 32;
            result[i] = (char *)malloc(len);
            if (result[i])
                snprintf(result[i], len, "%p", (void *)buffer[i]);
        }

        /* If malloc failed, leave NULL entries */
    }

    return result;
}

/* ── backtrace_symbols_fd ────────────────────────────────────── */

void backtrace_symbols_fd(void *const *buffer, int size, int fd)
{
    char **symbols;
    int    i;

    if (!buffer || size <= 0 || fd < 0) return;

    symbols = backtrace_symbols(buffer, size);
    if (!symbols) {
        /* Ultimate fallback: write hex addresses */
        for (i = 0; i < size; i++) {
            char line[64];
            int len = snprintf(line, sizeof(line), "%p\n", (void *)buffer[i]);
            if (len > 0)
                _write(fd, line, (unsigned int)(len < (int)sizeof(line) ? len : (int)sizeof(line) - 1));
        }
        return;
    }

    for (i = 0; i < size; i++) {
        if (symbols[i]) {
            size_t slen = strlen(symbols[i]);
            _write(fd, symbols[i], (unsigned int)slen);
            _write(fd, "\n", 1);
        } else {
            const char *msg = "<malloc failed>\n";
            _write(fd, msg, (unsigned int)strlen(msg));
        }
    }

    /* Free everything */
    for (i = 0; i < size; i++) free(symbols[i]);
    free(symbols);
}

#endif /* _WIN32 */
