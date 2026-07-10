/* core:os shim -- run external commands via libc system(3) / popen(3).
 *
 * The stdout-capture read loop lives here, in C, where buffer growth is explicit
 * and every allocation is checked. Tycho only ever receives the finished,
 * NUL-terminated string (arena-copied at the FFI boundary). Fail-closed: any
 * allocation or spawn failure yields a null handle / -1 exit, never a partial or
 * uninitialized buffer.
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE          /* glibc: expose popen/pclose + wait-status macros */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define TY_POPEN  _popen
#define TY_PCLOSE _pclose
static long ty_os_decode(int st) { return (long)st; }   /* Windows: system/_pclose return the code directly */
#else
#include <sys/wait.h>
#define TY_POPEN  popen
#define TY_PCLOSE pclose
static long ty_os_decode(int st) {                       /* POSIX wait-status -> a plain code */
    if (st == -1)        return -1;
    if (WIFEXITED(st))   return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return -1;
}
#endif

long osx_system(const char *cmd) {
    return ty_os_decode(system(cmd));
}

typedef struct { long code; char *out; } OsRun;

void *osx_run(const char *cmd) {
    FILE *f = TY_POPEN(cmd, "r");
    if (!f) return NULL;                              /* fail closed: shell could not start */
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) { TY_PCLOSE(f); return NULL; }
    char tmp[4096];
    size_t n;
    while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) {
        if (len + n + 1 > cap) {                     /* grow, keeping room for the NUL */
            size_t ncap = cap;
            while (len + n + 1 > ncap) ncap *= 2;
            char *nb = realloc(buf, ncap);
            if (!nb) { free(buf); TY_PCLOSE(f); return NULL; }
            buf = nb; cap = ncap;
        }
        memcpy(buf + len, tmp, n);
        len += n;
    }
    buf[len] = '\0';
    OsRun *r = malloc(sizeof *r);
    if (!r) { free(buf); TY_PCLOSE(f); return NULL; }
    r->out  = buf;
    r->code = ty_os_decode(TY_PCLOSE(f));            /* pclose exactly once, on every path */
    return r;
}

long        osx_run_code(void *p) { return p ? ((OsRun *)p)->code : -1; }
const char *osx_run_out (void *p) { return p ? ((OsRun *)p)->out  : ""; }
void        osx_run_free(void *p) { if (p) { OsRun *r = p; free(r->out); free(r); } }
