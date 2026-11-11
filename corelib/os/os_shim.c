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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE              /* glibc: pipe2 -- the race-free CLOEXEC pipe */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../tycho.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#ifdef _WIN32
#include <windows.h>            /* CreateProcessA / CreatePipe -- the argv path */
#include <process.h>            /* _beginthreadex -- concurrent stdout/stderr drains */
#include <errno.h>
#include <sys/stat.h>           /* _chmod */
#define TY_POPEN  _popen
#define TY_PCLOSE _pclose
static tycho_int ty_os_decode(int st) { return (tycho_int)st; }   /* Windows: system/_pclose return the code directly */
#else
#include <sys/wait.h>
#include <spawn.h>              /* posix_spawnp + file actions -- the argv path */
#include <unistd.h>             /* pipe, close, read */
#include <fcntl.h>              /* O_CLOEXEC / F_SETFD -- the capture pipe */
#include <poll.h>               /* drain stdout and stderr together */
#include <errno.h>
#include <sys/stat.h>           /* chmod */
extern char **environ;          /* posix_spawnp inherits it, as execvp does */

/* pipe2 is Linux/glibc + the BSDs; it is the only race-free spelling, since a
 * pipe() followed by fcntl() leaves a window in which a concurrent spawn on
 * another thread inherits the raw ends. Fall back to the window where absent. */
static int ty_pipe_cloexec(int fds[2]) {
#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    return pipe2(fds, O_CLOEXEC);
#else
    if (pipe(fds) != 0) return -1;
    if (fcntl(fds[0], F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(fds[1], F_SETFD, FD_CLOEXEC) != 0) {
        close(fds[0]); close(fds[1]);
        return -1;
    }
    return 0;
#endif
}
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

typedef struct { tycho_int code; char *out; size_t out_len; char *err; size_t err_len; } OsRun;

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
    r->out_len = len;
    r->err = NULL;
    r->err_len = 0;
    r->code = ty_os_decode(TY_PCLOSE(f));            /* pclose exactly once, on every path */
    return r;
}

tycho_int osx_run_code(void *p) { return p ? ((OsRun *)p)->code : -1; }
void osx_run_out(void *p, const unsigned char **out, tycho_int *outlen) {
    *out = NULL; *outlen = 0;
    if (!p) return;
    OsRun *r = p;
    *out = (const unsigned char *)r->out;             /* transfer to the bytes FFI copier */
    *outlen = (tycho_int)r->out_len;
    r->out = NULL; r->out_len = 0;
}
void osx_run_err(void *p, const unsigned char **out, tycho_int *outlen) {
    *out = NULL; *outlen = 0;
    if (!p) return;
    OsRun *r = p;
    *out = (const unsigned char *)r->err;
    *outlen = (tycho_int)r->err_len;
    r->err = NULL; r->err_len = 0;
}
void osx_run_free(void *p) { if (p) { OsRun *r = p; free(r->out); free(r->err); free(r); } }

/* Append n bytes and keep the buffer NUL-terminated. Grows by doubling; returns
 * 0 only when the allocation fails, so every caller fails closed. */
static int ty_buf_append(char **buf, size_t *cap, size_t *len, const char *src, size_t n) {
    if (*len + n + 1 > *cap) {
        size_t ncap = *cap ? *cap : 4096;
        while (*len + n + 1 > ncap) ncap *= 2;
        char *nb = realloc(*buf, ncap);
        if (!nb) return 0;
        *buf = nb; *cap = ncap;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 1;
}

#define OSX_ARGV_MAX 4096       /* a bound, so a runaway loop fails instead of swapping */

static int osx_argv_ok(const char *const *v, tycho_int n) {
    if (!v || n <= 0 || n > OSX_ARGV_MAX) return 0;
    for (tycho_int i = 0; i < n; i++) if (!v[i]) return 0;
    return 1;
}

/* Case-insensitive suffix check: does `name` end with `.bat` or `.cmd`?
 * Used to refuse batch files before CreateProcess, since cmd.exe re-parses
 * the command line and defeats the quoting we built.
 *
 * Deliberately OUTSIDE the _WIN32 split, though only the Windows spawn calls
 * it. The logic is pure C, and while it sat inside the #ifdef no POSIX host
 * could compile it -- so its gate scored a hand-copy instead, which passes
 * whatever the shipped function does and drifts from it silently. Here,
 * corelib/os/os_batch_check.c `#include`s this file and scores THIS function,
 * supplying its own main(); scripts/shim_check.sh builds and runs it.
 *
 * MARKED `unused` RATHER THAN REFERENCED. On a POSIX build outside that gate
 * nothing calls it, and the previous silencer took its address at file scope:
 * that quiets gcc's -Wunused-function, but only by creating an unused
 * VARIABLE, which clang warns about instead. The suppression became the thing
 * that fired, and since scripts/shim.warn locks this file at zero warnings it
 * stopped `make ci` under clang outright (FRICTION 90). The attribute says the
 * same thing to both compilers and creates nothing to warn about. It is
 * harmless where the function IS used -- the Windows spawn path, and the gate
 * above. */
__attribute__((unused))
static int osx_is_batch(const char *name) {
    size_t len = strlen(name);
    if (len < 4) return 0;
    const char *ext = name + len - 4;
    if (ext[0] != '.') return 0;
    /* .bat */
    if ((ext[1] == 'b' || ext[1] == 'B') &&
        (ext[2] == 'a' || ext[2] == 'A') &&
        (ext[3] == 't' || ext[3] == 'T'))
        return 1;
    /* .cmd */
    if ((ext[1] == 'c' || ext[1] == 'C') &&
        (ext[2] == 'm' || ext[2] == 'M') &&
        (ext[3] == 'd' || ext[3] == 'D'))
        return 1;
    return 0;
}

#ifndef _WIN32

/* execv shape from (ptr,len): a NULL-terminated vector this function owns. Only
 * the POINTERS are copied -- the strings stay the caller's, borrowed. NULL if
 * the argv is refused or the copy cannot be allocated. */
static char **osx_argv_dup(const char *const *v, tycho_int n) {
    if (!osx_argv_ok(v, n)) return NULL;
    char **a = calloc((size_t)n + 1, sizeof *a);   /* calloc: a[n] is the NULL terminator */
    if (!a) return NULL;
    for (tycho_int i = 0; i < n; i++) a[i] = (char *)v[i];
    return a;
}

/* Spawn v with no shell. posix_spawnp and not fork+execvp on purpose: a
 * Tycho program is threaded (the scheduler owns worker threads), and between
 * fork() and exec() in a threaded process only async-signal-safe calls are
 * legal -- a rule the arena allocator does not satisfy. posix_spawnp is the
 * interface that does the dance correctly, and it keeps execvp's PATH search.
 *
 * `out` NULL -> stdout is inherited (the osx_system shape); non-NULL -> stdout
 * is captured into a fresh buffer (the osx_run shape). stderr is inherited in
 * both, matching the two shell functions above. */
static tycho_int osx_spawn(const char *const *v, tycho_int n, char **out, size_t *outlen) {
    if (out) *out = NULL;
    if (outlen) *outlen = 0;
    size_t captured = 0;
    char **argv = osx_argv_dup(v, n);
    if (!argv) return -1;                             /* fail closed */

    int fds[2] = { -1, -1 };
    posix_spawn_file_actions_t fa;
    int have_fa = 0;
    if (out) {
        /* CLOEXEC on BOTH ends. A Tycho program is threaded, so a sibling task
         * spawning its own child would otherwise have it inherit this write end
         * and the read loop below would block until that unrelated child exits.
         * The dup2 file action makes a FRESH descriptor for the child's stdout,
         * and dup2 clears FD_CLOEXEC, so the capture path still works. */
        if (ty_pipe_cloexec(fds) != 0) { free(argv); return -1; }
        if (posix_spawn_file_actions_init(&fa) != 0) { free(argv); close(fds[0]); close(fds[1]); return -1; }
        have_fa = 1;
        /* the child writes the pipe as its stdout and holds neither raw end */
        if (posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO) != 0 ||
            posix_spawn_file_actions_addclose(&fa, fds[0]) != 0 ||
            posix_spawn_file_actions_addclose(&fa, fds[1]) != 0) {
            posix_spawn_file_actions_destroy(&fa); free(argv); close(fds[0]); close(fds[1]); return -1;
        }
    }

    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], have_fa ? &fa : NULL, NULL, argv, environ);
    free(argv);                                       /* the spawn is done with it; the strings were never ours */
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
        captured = len;
        close(fds[0]);
    }

    int st;
    while (waitpid(pid, &st, 0) < 0) {
        if (errno != EINTR) { free(buf); return -1; }  /* free(NULL) is a no-op */
    }
    if (out) *out = buf;
    if (outlen) *outlen = captured;
    return ty_os_decode(st);
}

tycho_int osx_exec(const char *const *v, tycho_int n) { return osx_spawn(v, n, NULL, NULL); }

void *osx_exec_out(const char *const *v, tycho_int n) {
    char *out = NULL;
    size_t out_len = 0;
    tycho_int code = osx_spawn(v, n, &out, &out_len);
    if (code < 0 && !out) return NULL;                /* spawn failed: no handle, no output */
    OsRun *r = malloc(sizeof *r);
    if (!r) { free(out); return NULL; }
    r->code = code;
    r->out  = out;
    r->out_len = out_len;
    r->err = NULL;
    r->err_len = 0;
    return r;                                          /* freed by osx_run_free, like osx_run's */
}

/* Capture stdout and stderr into separate buffers. Both pipes are drained in
 * one poll loop: reading them one after the other deadlocks as soon as the child
 * fills the pipe that is not being read. */
void *osx_exec_capture(const char *const *v, tycho_int n) {
    char **argv = osx_argv_dup(v, n);
    if (!argv) return NULL;

    int outp[2] = { -1, -1 }, errp[2] = { -1, -1 };
    posix_spawn_file_actions_t fa;
    if (ty_pipe_cloexec(outp) != 0) { free(argv); return NULL; }
    if (ty_pipe_cloexec(errp) != 0) { close(outp[0]); close(outp[1]); free(argv); return NULL; }
    if (posix_spawn_file_actions_init(&fa) != 0) {
        close(outp[0]); close(outp[1]); close(errp[0]); close(errp[1]); free(argv); return NULL;
    }
    if (posix_spawn_file_actions_adddup2(&fa, outp[1], STDOUT_FILENO) != 0 ||
        posix_spawn_file_actions_adddup2(&fa, errp[1], STDERR_FILENO) != 0 ||
        posix_spawn_file_actions_addclose(&fa, outp[0]) != 0 ||
        posix_spawn_file_actions_addclose(&fa, outp[1]) != 0 ||
        posix_spawn_file_actions_addclose(&fa, errp[0]) != 0 ||
        posix_spawn_file_actions_addclose(&fa, errp[1]) != 0) {
        posix_spawn_file_actions_destroy(&fa);
        close(outp[0]); close(outp[1]); close(errp[0]); close(errp[1]); free(argv); return NULL;
    }

    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], &fa, NULL, argv, environ);
    free(argv);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) {
        close(outp[0]); close(outp[1]); close(errp[0]); close(errp[1]); return NULL;
    }
    close(outp[1]); close(errp[1]);                   /* parent keeps only read ends */

    struct pollfd pf[2];
    pf[0].fd = outp[0]; pf[0].events = POLLIN; pf[0].revents = 0;
    pf[1].fd = errp[0]; pf[1].events = POLLIN; pf[1].revents = 0;
    char *out = NULL, *err = NULL;
    size_t ocap = 0, olen = 0, ecap = 0, elen = 0;
    int open_fds = 2;
    int ok = 1;
    char tmp[4096];
    while (open_fds > 0 && ok) {
        pf[0].revents = 0; pf[1].revents = 0;
        int pr = poll(pf, 2, -1);
        if (pr < 0) {
            if (errno == EINTR) continue;
            ok = 0;
            break;
        }
        for (int i = 0; i < 2 && ok; i++) {
            if (pf[i].fd < 0 || pf[i].revents == 0) continue;
            ssize_t got = read(pf[i].fd, tmp, sizeof tmp);
            if (got > 0) {
                ok = i == 0 ? ty_buf_append(&out, &ocap, &olen, tmp, (size_t)got)
                            : ty_buf_append(&err, &ecap, &elen, tmp, (size_t)got);
            } else if (got == 0) {
                close(pf[i].fd);
                pf[i].fd = -1;
                open_fds -= 1;
            } else if (errno != EINTR) {
                ok = 0;
            }
        }
    }
    if (pf[0].fd >= 0) close(pf[0].fd);
    if (pf[1].fd >= 0) close(pf[1].fd);
    int st;
    while (waitpid(pid, &st, 0) < 0) {
        if (errno != EINTR) { ok = 0; break; }
    }
    if (!ok) { free(out); free(err); return NULL; }
    if (!out) { out = calloc(1, 1); if (!out) { free(err); return NULL; } }
    if (!err) { err = calloc(1, 1); if (!err) { free(out); return NULL; } }
    OsRun *r = malloc(sizeof *r);
    if (!r) { free(out); free(err); return NULL; }
    r->code = ty_os_decode(st);
    r->out = out;
    r->out_len = olen;
    r->err = err;
    r->err_len = elen;
    return r;
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
 * gap: batch files (.bat/.cmd) are now refused by extension in osx_spawn_win.
 * A PATH-resolved name like "cmd" (no extension) is not caught -- that is the
 * documented limitation of an extension-based check. */

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
static char *osx_win_cmdline(const char *const *v, tycho_int n) {
    char *buf = NULL; size_t cap = 0, len = 0;
    for (tycho_int i = 0; i < n; i++) {
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
        if (!osx_win_quote(v[i], &buf, &cap, &len)) { free(buf); return NULL; }
    }
    if (!buf) {                                        /* n == 0 is refused by the caller, but never return NULL-as-ok */
        buf = calloc(1, 1);
    }
    return buf;
}

/* lpApplicationName is NULL so the PATH is searched, matching execvp/
 * posix_spawnp. `out` NULL -> stdout inherited; non-NULL -> captured. */
static tycho_int osx_spawn_win(const char *const *v, tycho_int n, char **out, size_t *outlen) {
    if (out) *out = NULL;
    if (outlen) *outlen = 0;
    size_t captured = 0;
    if (!osx_argv_ok(v, n)) return -1;                 /* fail closed */

    /* Refuse .bat/.cmd: they run through cmd.exe which re-parses the command
     * line, defeating the quoting we built.  Only catches explicit extensions
     * -- a PATH-resolved name like "cmd" is not caught. */
    if (osx_is_batch(v[0])) return -1;

    char *cmdline = osx_win_cmdline(v, n);
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
        captured = len;
        CloseHandle(rd);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    if (!GetExitCodeProcess(pi.hProcess, &code)) code = (DWORD)-1;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (out) *out = buf;
    if (outlen) *outlen = captured;
    return (tycho_int)(int)code;                       /* Windows hands back the code directly */
}

/* Windows spawn path: compiled only under _WIN32.  The quoting round-trip is
 * tested by os_argv_quotecheck.c (run by shim-check on MSYS/MINGW/CYGWIN).
 * The spawn+capture path itself has no Linux-host test -- loud-skip: the
 * os_argv_quotecheck.c leg in shim-check is the only Windows gate. */
tycho_int osx_exec(const char *const *v, tycho_int n) { return osx_spawn_win(v, n, NULL, NULL); }

void *osx_exec_out(const char *const *v, tycho_int n) {
    char *out = NULL;
    size_t out_len = 0;
    tycho_int code = osx_spawn_win(v, n, &out, &out_len);
    if (code < 0 && !out) return NULL;
    OsRun *r = malloc(sizeof *r);
    if (!r) { free(out); return NULL; }
    r->code = code;
    r->out  = out;
    r->out_len = out_len;
    r->err = NULL;
    r->err_len = 0;
    return r;
}

typedef struct { HANDLE h; char *buf; size_t len, cap; int ok; } OsxDrain;

static unsigned __stdcall osx_win_drain(void *arg) {
    OsxDrain *d = arg;
    char tmp[4096];
    DWORD n;
    d->ok = 1;
    while (ReadFile(d->h, tmp, sizeof tmp, &n, NULL) && n > 0) {
        if (!ty_buf_append(&d->buf, &d->cap, &d->len, tmp, (size_t)n)) { d->ok = 0; break; }
    }
    CloseHandle(d->h);
    return 0;
}

/* Two reader threads, one per pipe: a single-threaded read would deadlock once
 * the child fills the pipe that is not currently being drained. */
void *osx_exec_capture(const char *const *v, tycho_int n) {
    if (!osx_argv_ok(v, n)) return NULL;               /* fail closed */
    if (osx_is_batch(v[0])) return NULL;
    char *cmdline = osx_win_cmdline(v, n);
    if (!cmdline) return NULL;

    HANDLE ord = NULL, owr = NULL, erd = NULL, ewr = NULL;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&ord, &owr, &sa, 0)) { free(cmdline); return NULL; }
    if (!CreatePipe(&erd, &ewr, &sa, 0)) {
        CloseHandle(ord); CloseHandle(owr); free(cmdline); return NULL;
    }
    if (!SetHandleInformation(ord, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(erd, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(ord); CloseHandle(owr); CloseHandle(erd); CloseHandle(ewr);
        free(cmdline); return NULL;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = owr;
    si.hStdError = ewr;
    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    free(cmdline);
    if (!ok) {
        CloseHandle(ord); CloseHandle(owr); CloseHandle(erd); CloseHandle(ewr);
        return NULL;
    }
    CloseHandle(owr);
    CloseHandle(ewr);

    OsxDrain od = { ord, NULL, 0, 0, 1 };
    OsxDrain ed = { erd, NULL, 0, 0, 1 };
    uintptr_t t1 = _beginthreadex(NULL, 0, osx_win_drain, &od, 0, NULL);
    if (!t1) {
        CloseHandle(ord); CloseHandle(erd);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return NULL;
    }
    uintptr_t t2 = _beginthreadex(NULL, 0, osx_win_drain, &ed, 0, NULL);
    if (!t2) {
        CloseHandle(erd);
        WaitForSingleObject((HANDLE)t1, INFINITE);
        CloseHandle((HANDLE)t1);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        free(od.buf); free(ed.buf);
        return NULL;
    }
    WaitForSingleObject((HANDLE)t1, INFINITE);
    WaitForSingleObject((HANDLE)t2, INFINITE);
    CloseHandle((HANDLE)t1);
    CloseHandle((HANDLE)t2);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD wcode = 1;
    if (!GetExitCodeProcess(pi.hProcess, &wcode)) wcode = (DWORD)-1;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (!od.ok || !ed.ok) { free(od.buf); free(ed.buf); return NULL; }
    if (!od.buf) { od.buf = calloc(1, 1); if (!od.buf) { free(ed.buf); return NULL; } }
    if (!ed.buf) { ed.buf = calloc(1, 1); if (!ed.buf) { free(od.buf); return NULL; } }
    OsRun *r = malloc(sizeof *r);
    if (!r) { free(od.buf); free(ed.buf); return NULL; }
    r->code = (tycho_int)(int)wcode;
    r->out = od.buf;
    r->out_len = od.len;
    r->err = ed.buf;
    r->err_len = ed.len;
    return r;
}

#endif

/* Host predicate. The preprocessor's answer, exported as a runtime one: a Tycho
 * program cannot spell _WIN32, and `compiler/types/load.ty@dir_of` needs exactly
 * the condition `src/tychoc.c@path_dir` gets from it. */
tycho_int osx_is_windows(void) {
#ifdef _WIN32
    return 1;
#else
    return 0;
#endif
}

char *osx_env_get(const char *name) {
    return getenv(name);
}

tycho_int osx_env_set(const char *name, const char *value) {
#ifdef _WIN32
    return _putenv_s(name, value) == 0 ? 0 : -1;
#else
    return setenv(name, value, 1) == 0 ? 0 : -1;
#endif
}

tycho_int osx_env_unset(const char *name) {
#ifdef _WIN32
    return _putenv_s(name, "") == 0 ? 0 : -1;
#else
    return unsetenv(name) == 0 ? 0 : -1;
#endif
}

/* Thread-local so concurrent cwd/hostname calls cannot overwrite each other.
 * The Tycho boundary copies the answer immediately. */
static _Thread_local char ty_os_cwd[8192];
static _Thread_local char ty_os_host[1024];

char *osx_cwd(void) {
#ifdef _WIN32
    DWORD n = GetCurrentDirectoryA((DWORD)sizeof ty_os_cwd, ty_os_cwd);
    if (n == 0 || n >= sizeof ty_os_cwd) return NULL;
#else
    if (!getcwd(ty_os_cwd, sizeof ty_os_cwd)) return NULL;
#endif
    return ty_os_cwd;
}

tycho_int osx_chdir(const char *p) {
#ifdef _WIN32
    if (SetCurrentDirectoryA(p)) return 0;
    switch (GetLastError()) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND: return 1;
        case ERROR_ACCESS_DENIED:  return 2;
        case ERROR_DIRECTORY:      return 3;
        default:                   return 4;
    }
#else
    if (chdir(p) == 0) return 0;
    switch (errno) {
        case ENOENT:  return 1;
        case EACCES:
        case EPERM:   return 2;
        case ENOTDIR: return 3;
        default:      return 4;
    }
#endif
}

tycho_int osx_pid(void) {
#ifdef _WIN32
    return (tycho_int)_getpid();
#else
    return (tycho_int)getpid();
#endif
}

char *osx_hostname(void) {
#ifdef _WIN32
    DWORD n = (DWORD)sizeof ty_os_host;
    if (!GetComputerNameA(ty_os_host, &n)) return NULL;
    ty_os_host[n < sizeof ty_os_host ? n : sizeof ty_os_host - 1] = 0;
#else
    if (gethostname(ty_os_host, sizeof ty_os_host - 1) != 0) return NULL;
    ty_os_host[sizeof ty_os_host - 1] = 0;
#endif
    return ty_os_host;
}

/* ---- permissions and identity ---- */
tycho_int osx_chmod(const char *p, tycho_int mode) {
#ifdef _WIN32
    if (_chmod(p, (int)mode) == 0) return 0;
    switch (errno) {
        case ENOENT: return 1;
        case EACCES: return 2;
        default:     return 4;
    }
#else
    if (chmod(p, (mode_t)mode) == 0) return 0;
    switch (errno) {
        case ENOENT: return 1;
        case EACCES:
        case EPERM:  return 2;
        default:     return 4;
    }
#endif
}

tycho_int osx_umask(tycho_int mask) {
#ifdef _WIN32
    return (tycho_int)_umask((int)mask);
#else
    return (tycho_int)umask((mode_t)mask);
#endif
}

tycho_int osx_getuid(void) {
#ifdef _WIN32
    return -1;
#else
    return (tycho_int)getuid();
#endif
}

tycho_int osx_geteuid(void) {
#ifdef _WIN32
    return -1;
#else
    return (tycho_int)geteuid();
#endif
}

tycho_int osx_getgid(void) {
#ifdef _WIN32
    return -1;
#else
    return (tycho_int)getgid();
#endif
}

tycho_int osx_getegid(void) {
#ifdef _WIN32
    return -1;
#else
    return (tycho_int)getegid();
#endif
}

tycho_int osx_getppid(void) {
#ifdef _WIN32
    return -1;
#else
    return (tycho_int)getppid();
#endif
}

tycho_int osx_platform(void) {
#if defined(__ANDROID__)
    return 3;
#elif defined(_WIN32)
    return 1;
#elif defined(__APPLE__)
#if TARGET_OS_IPHONE
    return 4;
#else
    return 2;
#endif
#elif defined(__linux__)
    return 0;
#else
    return 5;
#endif
}
