#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../tycho.h"

static __thread char *g_stack = NULL;

#ifdef _WIN32
#include <windows.h>
char *dbgx_stack(void) {
    void *frames[64];
    USHORT n = CaptureStackBackTrace(0, 64, frames, NULL);
    size_t cap = (size_t)n * 24 + 1;
    char *p = realloc(g_stack, cap);
    if (!p) return "";
    g_stack = p;
    g_stack[0] = 0;
    size_t at = 0;
    for (USHORT i = 0; i < n; i++) {
        at += (size_t)snprintf(g_stack + at, cap - at, "%p\n", frames[i]);
        if (at >= cap) break;
    }
    return g_stack;
}
#else
#include <execinfo.h>
char *dbgx_stack(void) {
    void *frames[64];
    int n = backtrace(frames, 64);
    char **syms = backtrace_symbols(frames, n);
    if (!syms) return "";
    size_t cap = 1;
    for (int i = 0; i < n; i++) cap += strlen(syms[i]) + 1;
    char *p = realloc(g_stack, cap);
    if (!p) { free(syms); return ""; }
    g_stack = p;
    size_t at = 0;
    for (int i = 0; i < n; i++) {
        size_t len = strlen(syms[i]);
        memcpy(g_stack + at, syms[i], len);
        at += len;
        g_stack[at++] = '\n';
    }
    g_stack[at < cap ? at : cap - 1] = 0;
    free(syms);
    return g_stack;
}
#endif
