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
#include <stdint.h>
/* int64-migration (Phase 3): Tycho `int` lowers to tycho_int (int64_t) in the
 * emitted program; this shim is a separate translation unit, so it defines the
 * same type to match the FFI ABI on ILP32/LLP64, not just LP64. The libc-facing
 * `int st` wait-status stays int; the decoded exit code is a Tycho int. */
#ifndef TYCHO_INT_T
#define TYCHO_INT_T
typedef int64_t tycho_int;
#endif

#ifdef _WIN32
#include <windows.h>            /* CreateProcessA / CreatePipe -- the argv path */
#define TY_POPEN  _popen
#define TY_PCLOSE _pclose
static tycho_int ty_os_decode(int st) { return (tycho_int)st; }   /* Windows: system/_pclose return the code directly */
#else
#include <sys/wait.h>
#include <spawn.h>              /* posix_spawnp + file actions -- the argv path */
#include <unistd.h>             /* pipe, close, read */
#include <errno.h>
extern char **environ;          /* posix_spawnp inherits it, as execvp does */
#define TY_POPEN  popen
#define TY_PCLOSE pclose
static tycho_int ty_os_decode(int st) {                       /* POSIX wait-status -> a plain code */
    if (st == -1)        return -1;
    if (WIFEXITED(st))   return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return -1;
}
#endif

tycho_int osx_system(const char *cmd) {
    return ty_os_decode(system(cmd));
}

typedef struct { tycho_int code; char *out; } OsRun;

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

tycho_int   osx_run_code(void *p) { return p ? ((OsRun *)p)->code : -1; }
const char *osx_run_out (void *p) { return p ? ((OsRun *)p)->out  : ""; }
void        osx_run_free(void *p) { if (p) { OsRun *r = p; free(r->out); free(r); } }

/* ---- the argv path: run a program with NO shell between caller and kernel ---
 *
 * WHY A BUILDER AND NOT `osx_exec(const char **argv, tycho_int n)`. A Tycho
 * `[string]` cannot cross the FFI at all: `docs/spec/14-ffi.md` §24.1 crosses
 * only the SCALAR arrays `[int]` and `[float]` as a `(const T*, long)` pair and
 * rejects "an array of any other element type". The NUL-joined-blob alternative
 * is worse than it looks -- a Tycho `string` returned to or from C truncates at
 * the first NUL, which is the defect `core:http`'s `body_bytes` was added to
 * work around. So argv is accumulated one `string` at a time behind an opaque
 * handle, the same shape core:http's Resp and core:sqlite's stmt already use.
 *
 * OWNERSHIP. Every pushed string is strdup'd here: the caller's `string` is
 * arena memory whose lifetime is not ours, and the vector must stay valid
 * across the spawn. The handle is freed by exactly one paired osx_argv_free.
 *
 * FAIL CLOSED. A push that cannot allocate marks the handle `broken` and every
 * later call refuses -- a half-built argv must never be executed, because a
 * DROPPED argument is not a smaller command, it is a different one (consider
 * `rm -- somefile` losing its `--`). An empty argv is refused for the same
 * reason. Refusal is -1 / NULL, never a fallback to the shell. */
#define OSX_ARGV_MAX 4096       /* a bound, so a runaway loop fails instead of swapping */

typedef struct { char **v; size_t n, cap; int broken; } OsArgv;

void *osx_argv_new(void) {
    OsArgv *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->cap = 8;
    a->v = calloc(a->cap, sizeof *a->v);       /* calloc: v[n] is already the NULL terminator */
    if (!a->v) { free(a); return NULL; }
    return a;
}

tycho_int osx_argv_push(void *h, const char *s) {
    OsArgv *a = h;
    if (!a || a->broken || !s) return 0;
    if (a->n >= OSX_ARGV_MAX) { a->broken = 1; return 0; }
    if (a->n + 2 > a->cap) {                   /* +2: the new entry and the NULL terminator */
        size_t ncap = a->cap * 2;
        char **nv = realloc(a->v, ncap * sizeof *nv);
        if (!nv) { a->broken = 1; return 0; }
        memset(nv + a->cap, 0, (ncap - a->cap) * sizeof *nv);
        a->v = nv; a->cap = ncap;
    }
    char *copy = strdup(s);
    if (!copy) { a->broken = 1; return 0; }
    a->v[a->n++] = copy;
    a->v[a->n] = NULL;
    return 1;
}

void osx_argv_free(void *h) {
    OsArgv *a = h;
    if (!a) return;
    for (size_t i = 0; i < a->n; i++) free(a->v[i]);
    free(a->v);
    free(a);
}

#ifndef _WIN32

/* Spawn a->v with no shell. posix_spawnp and not fork+execvp on purpose: a
 * Tycho program is threaded (the scheduler owns worker threads), and between
 * fork() and exec() in a threaded process only async-signal-safe calls are
 * legal -- a rule the arena allocator does not satisfy. posix_spawnp is the
 * interface that does the dance correctly, and it keeps execvp's PATH search.
 *
 * `out` NULL -> stdout is inherited (the osx_system shape); non-NULL -> stdout
 * is captured into a fresh buffer (the osx_run shape). stderr is inherited in
 * both, matching the two shell functions above. */
static tycho_int osx_spawn(OsArgv *a, char **out) {
    if (out) *out = NULL;
    if (!a || a->broken || a->n == 0) return -1;      /* fail closed */

    int fds[2] = { -1, -1 };
    posix_spawn_file_actions_t fa;
    int have_fa = 0;
    if (out) {
        if (pipe(fds) != 0) return -1;
        if (posix_spawn_file_actions_init(&fa) != 0) { close(fds[0]); close(fds[1]); return -1; }
        have_fa = 1;
        /* the child writes the pipe as its stdout and holds neither raw end */
        if (posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO) != 0 ||
            posix_spawn_file_actions_addclose(&fa, fds[0]) != 0 ||
            posix_spawn_file_actions_addclose(&fa, fds[1]) != 0) {
            posix_spawn_file_actions_destroy(&fa); close(fds[0]); close(fds[1]); return -1;
        }
    }

    pid_t pid;
    int rc = posix_spawnp(&pid, a->v[0], have_fa ? &fa : NULL, NULL, a->v, environ);
    if (have_fa) posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) {                                    /* no such program, or no memory */
        if (fds[0] >= 0) { close(fds[0]); close(fds[1]); }
        return -1;
    }

    char *buf = NULL;
    if (out) {
        close(fds[1]);                                /* parent must drop the write end or read never ends */
        size_t cap = 4096, len = 0;
        buf = malloc(cap);
        if (!buf) { close(fds[0]); waitpid(pid, NULL, 0); return -1; }
        char tmp[4096];
        ssize_t n;
        while ((n = read(fds[0], tmp, sizeof tmp)) != 0) {
            if (n < 0) { if (errno == EINTR) continue; free(buf); close(fds[0]); waitpid(pid, NULL, 0); return -1; }
            if (len + (size_t)n + 1 > cap) {
                size_t ncap = cap;
                while (len + (size_t)n + 1 > ncap) ncap *= 2;
                char *nb = realloc(buf, ncap);
                if (!nb) { free(buf); close(fds[0]); waitpid(pid, NULL, 0); return -1; }
                buf = nb; cap = ncap;
            }
            memcpy(buf + len, tmp, (size_t)n);
            len += (size_t)n;
        }
        buf[len] = '\0';
        close(fds[0]);
    }

    int st;
    while (waitpid(pid, &st, 0) < 0) {
        if (errno != EINTR) { free(buf); return -1; }  /* free(NULL) is a no-op */
    }
    if (out) *out = buf;
    return ty_os_decode(st);
}

tycho_int osx_exec(void *h) { return osx_spawn(h, NULL); }

void *osx_exec_out(void *h) {
    char *out = NULL;
    tycho_int code = osx_spawn(h, &out);
    if (code < 0 && !out) return NULL;                /* spawn failed: no handle, no output */
    OsRun *r = malloc(sizeof *r);
    if (!r) { free(out); return NULL; }
    r->code = code;
    r->out  = out;
    return r;                                          /* freed by osx_run_free, like osx_run's */
}

#else

/* ---- Windows: CreateProcess, and NO cmd.exe --------------------------------
 *
 * There is no execve here: every spawn ultimately takes a single command LINE,
 * and the callee splits it again with CommandLineToArgvW. So "no shell" is
 * achievable -- CreateProcess with no cmd.exe anywhere -- but "no re-parsing"
 * is not, and the only way an argv survives the round trip intact is to quote
 * it by the rules the splitter actually implements. That is what osx_win_quote
 * below does, and it is why the CRT's `_spawnvp` is NOT used: its own joiner
 * has historically mishandled trailing backslashes, which is the exact case
 * that turns `C:\dir\` into an escaped quote and shifts every later argument.
 *
 * WHAT THIS DOES AND DOES NOT BUY. A metacharacter (`;`, `&`, `|`, `>`) is
 * inert, because no shell ever sees the line -- that is the injection class
 * closed. What remains is a property of the CALLEE: a program that parses its
 * command line by its own rules rather than CommandLineToArgvW's -- cmd.exe
 * itself, a .bat/.cmd file, or anything using a hand-rolled splitter -- can
 * still read the line differently. Do not hand a .bat file untrusted argv.
 * gap: batch files are not refused here; refusing them by extension was
 * considered and left out because the callee is a PATH-searched name, not
 * necessarily a path we can classify before CreateProcess resolves it. */

/* Append one argv element, quoted per the CommandLineToArgvW rules:
 *   - a run of N backslashes followed by `"` becomes 2N+1 backslashes + \"
 *   - a run of N backslashes at the END of a quoted arg becomes 2N
 *   - backslashes not before a quote are literal
 * An empty argument MUST still be emitted as "" or it vanishes from the line.
 * Returns 0 on allocation failure (the caller fails closed). */
static int osx_win_quote(const char *s, char **buf, size_t *cap, size_t *len) {
    size_t need = strlen(s) * 2 + 3;                  /* worst case: every char escaped, + "" + space */
    if (*len + need + 1 > *cap) {
        size_t ncap = *cap ? *cap : 128;
        while (*len + need + 1 > ncap) ncap *= 2;
        char *nb = realloc(*buf, ncap);
        if (!nb) return 0;
        *buf = nb; *cap = ncap;
    }
    char *o = *buf + *len;
    int simple = *s != '\0';
    for (const char *p = s; *p; p++)
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\v' || *p == '"') { simple = 0; break; }
    if (simple) {
        size_t n = strlen(s);
        memcpy(o, s, n); o += n;
    } else {
        *o++ = '"';
        for (const char *p = s; ; p++) {
            size_t nsl = 0;
            while (*p == '\\') { nsl++; p++; }
            if (*p == '\0') {                          /* trailing run: double it, then close */
                for (size_t i = 0; i < nsl * 2; i++) *o++ = '\\';
                break;
            }
            if (*p == '"') {                           /* run before a quote: double it, escape the quote */
                for (size_t i = 0; i < nsl * 2 + 1; i++) *o++ = '\\';
                *o++ = '"';
            } else {
                for (size_t i = 0; i < nsl; i++) *o++ = '\\';
                *o++ = *p;
            }
        }
        *o++ = '"';
    }
    *len = (size_t)(o - *buf);
    (*buf)[*len] = '\0';
    return 1;
}

/* Join the vector into one writable command line. CreateProcess may MODIFY
 * lpCommandLine in place when lpApplicationName is NULL, so it must not be a
 * literal or a shared buffer. NULL on allocation failure. */
static char *osx_win_cmdline(OsArgv *a) {
    char *buf = NULL; size_t cap = 0, len = 0;
    for (size_t i = 0; i < a->n; i++) {
        if (i) {                                       /* separator, before the element */
            if (len + 2 > cap) {
                size_t ncap = cap ? cap * 2 : 128;
                char *nb = realloc(buf, ncap);
                if (!nb) { free(buf); return NULL; }
                buf = nb; cap = ncap;
            }
            buf[len++] = ' ';
            buf[len] = '\0';
        }
        if (!osx_win_quote(a->v[i], &buf, &cap, &len)) { free(buf); return NULL; }
    }
    if (!buf) {                                        /* n == 0 is refused by the caller, but never return NULL-as-ok */
        buf = calloc(1, 1);
    }
    return buf;
}

/* lpApplicationName is NULL so the PATH is searched, matching execvp/
 * posix_spawnp. `out` NULL -> stdout inherited; non-NULL -> captured. */
static tycho_int osx_spawn_win(OsArgv *a, char **out) {
    if (out) *out = NULL;
    if (!a || a->broken || a->n == 0) return -1;       /* fail closed */

    char *cmdline = osx_win_cmdline(a);
    if (!cmdline) return -1;

    HANDLE rd = NULL, wr = NULL;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    if (out) {
        if (!CreatePipe(&rd, &wr, &sa, 0)) { free(cmdline); return -1; }
        /* the child must not inherit the READ end or the pipe never sees EOF */
        if (!SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(rd); CloseHandle(wr); free(cmdline); return -1;
        }
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    if (out) {
        si.dwFlags    = STARTF_USESTDHANDLES;
        si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = wr;
        si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);   /* stderr stays the parent's, as popen does */
    }

    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, out ? TRUE : FALSE,
                             0, NULL, NULL, &si, &pi);
    free(cmdline);
    if (!ok) {                                         /* no such program, or refused */
        if (rd) { CloseHandle(rd); CloseHandle(wr); }
        return -1;
    }

    char *buf = NULL;
    if (out) {
        CloseHandle(wr);                               /* parent drops the write end or read never ends */
        size_t cap = 4096, len = 0;
        buf = malloc(cap);
        if (!buf) {
            CloseHandle(rd); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            return -1;
        }
        char tmp[4096];
        DWORD n;
        while (ReadFile(rd, tmp, sizeof tmp, &n, NULL) && n > 0) {
            if (len + n + 1 > cap) {
                size_t ncap = cap;
                while (len + n + 1 > ncap) ncap *= 2;
                char *nb = realloc(buf, ncap);
                if (!nb) {
                    free(buf); CloseHandle(rd);
                    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
                    return -1;
                }
                buf = nb; cap = ncap;
            }
            memcpy(buf + len, tmp, n);
            len += n;
        }
        buf[len] = '\0';
        CloseHandle(rd);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    if (!GetExitCodeProcess(pi.hProcess, &code)) code = (DWORD)-1;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (out) *out = buf;
    return (tycho_int)(int)code;                       /* Windows hands back the code directly */
}

tycho_int osx_exec(void *h) { return osx_spawn_win(h, NULL); }

void *osx_exec_out(void *h) {
    char *out = NULL;
    tycho_int code = osx_spawn_win(h, &out);
    if (code < 0 && !out) return NULL;
    OsRun *r = malloc(sizeof *r);
    if (!r) { free(out); return NULL; }
    r->code = code;
    r->out  = out;
    return r;
}

#endif
