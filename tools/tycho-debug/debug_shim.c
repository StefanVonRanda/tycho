/* Companion C for tools/tycho-debug/main.ty (the `tycho debug` gdb adapter),
 * linked via `tychoc --shim tools/tycho-debug/debug_shim.c`.
 *
 * The debugger drives a gdb MI session over two pipes: it writes MI commands
 * to gdb's stdin and reads gdb's stdout until the `(gdb)` prompt. core:os
 * cannot express that (system/popen are one-way), so fork/pipe/dup2/exec and
 * a line reader live here. Pure libc/POSIX -- nothing external to install.
 *
 * SIGNALS, AND WHY THEY LOOK THE WAY THEY DO.
 *
 *   gdb is spawned into its OWN process group (setpgid in the child), so a
 *   terminal Ctrl-C reaches the tool and never gdb. The tool's SIGINT handler
 *   (installed here) does the only async-signal-safe thing there is: store to
 *   a sig_atomic_t. A blocked read then returns EINTR, and dbgx_readline
 *   reports that as an interrupt (NULL + the dbgx_interrupted flag) rather
 *   than as EOF -- so a Ctrl-C mid-`next` does not look like gdb dying. The
 *   tool answers the flag by SIGINT-ing the INFERIOR's pid (dbgx_kill): MI
 *   stays in its default SYNC mode, where gdb does not read commands while the
 *   inferior runs, so `-exec-interrupt` written to the pipe would sit
 *   unprocessed -- but gdb is the tracer, so a signal delivered to the
 *   inferior is intercepted and reported as *stopped at the current source
 *   line. (The mi-async route was probed on gdb 17.2 and rejected: with
 *   `-gdb-set mi-async on` the FIRST -exec-run misses every breakpoint and
 *   swallows the inferior's output.)
 *
 *   The handler is installed WITHOUT SA_RESTART so the interrupted read
 *   actually surfaces; SIGPIPE is ignored so a dead gdb cannot kill the tool
 *   in the middle of a write (the write then returns -1 and the tool reports
 *   it).
 *
 * FFI SHAPES (match the emitted C): tycho `int` == tycho_int (int64_t, defined
 * below -- this shim is a separate translation unit), tycho `string` ==
 * char*, `ptr` == void*, `-> Option(string)` == nullable char* (NULL -> None,
 * copied into the caller's arena immediately, so the line buffer here is only
 * valid until the next dbgx_readline call -- the same contract core:io's
 * iox_read_line documents). The first field of the handle struct is the Line
 * reader, so dbgx_readline treats a gdb handle and the stdin handle alike.
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE          /* glibc: expose setpgid without -std noise */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#ifndef TYCHO_INT_T
#define TYCHO_INT_T
typedef int64_t tycho_int;
#endif

/* One line reader over a raw fd. */
typedef struct {
    int fd;
    char *buf;
    size_t cap;
} Line;

static volatile sig_atomic_t g_interrupted = 0;

static void on_sigint(int sig) {
    (void)sig;
    g_interrupted = 1;
}

static Line *line_init(int fd) {
    Line *l = calloc(1, sizeof *l);
    if (!l) return NULL;
    l->fd = fd;
    l->cap = 512;
    l->buf = malloc(l->cap);
    if (!l->buf) { free(l); return NULL; }
    return l;
}

static int line_init_into(Line *l, int fd) {
    l->fd = fd;
    l->cap = 512;
    l->buf = malloc(l->cap);
    return l->buf != NULL;
}

/* double the line buffer; 0 on allocation failure (the read then returns the
 * partial line -- a truncated MI record mis-parses, but the session survives) */
static int line_grow(Line *L) {
    size_t nc = L->cap * 2;
    char *nb = realloc(L->buf, nc);
    if (!nb) return 0;
    L->buf = nb;
    L->cap = nc;
    return 1;
}

/* 1 = a full line read into L->buf (NUL-terminated, trailing newline/CRLF
 * stripped), 0 = EOF, -1 = interrupted (a SIGINT arrived; see on_sigint). */
static long line_read(Line *L) {
    long n = 0;
    for (;;) {
        char c;
        ssize_t r = read(L->fd, &c, 1);
        if (r == 1) {
            if (c == '\n') {
                if (n > 0 && L->buf[n - 1] == '\r') n--;
                L->buf[n] = '\0';
                return 1;
            }
            if (n + 1 >= (long)L->cap && !line_grow(L))
                break;                    /* allocation failed: return what we have */
            L->buf[n++] = c;
            continue;
        }
        if (r < 0 && errno == EINTR) return -1;
        return 0;
    }
    /* only reached when the buffer could not grow: return the partial line */
    L->buf[n] = '\0';
    return n > 0 ? 1 : 0;
}

void *dbgx_stdin(void) {
    return line_init(0);
}

/* The next line from the handle, or NULL at EOF / on an interrupt. The tool
 * disambiguates the two NULL cases with dbgx_interrupted. The returned
 * pointer is valid until the next call or close -- Tycho arena-copies it. */
const char *dbgx_readline(void *p) {
    if (!p) return NULL;
    Line *L = p;
    return line_read(L) == 1 ? L->buf : NULL;
}

/* Test-and-clear the SIGINT flag. */
tycho_int dbgx_interrupted(void *p) {
    (void)p;
    sig_atomic_t v = g_interrupted;
    g_interrupted = 0;
    return (tycho_int)v;
}

/* The gdb handle: the Line reader is its FIRST field, so dbgx_readline's
 * `void *` cast in the FFI preamble is the same pointer either way. */
typedef struct {
    Line rd;
    int wfd;
    pid_t pid;
} Dbg;

/* Spawn `cmd` via /bin/sh -c with stdin/stdout/stderr as pipes, in its OWN
 * process group, and install the tool's SIGINT handler. Returns NULL on any
 * failure before the exec -- fail closed, nothing half-spawned. */
void *dbgx_spawn(const char *cmd) {
    if (!cmd) return NULL;
    int in[2], out[2];
    if (pipe(in) != 0) return NULL;
    if (pipe(out) != 0) { close(in[0]); close(in[1]); return NULL; }
    pid_t pid = fork();
    if (pid < 0) {
        close(in[0]); close(in[1]); close(out[0]); close(out[1]);
        return NULL;
    }
    if (pid == 0) {
        setpgid(0, 0);                       /* terminal Ctrl-C stays away from gdb */
        dup2(in[0], 0);
        dup2(out[1], 1);
        dup2(out[1], 2);                     /* gdb stderr joins the MI stream */
        close(in[0]); close(in[1]); close(out[0]); close(out[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    Dbg *d = calloc(1, sizeof *d);
    if (!d) { close(in[1]); close(out[0]); return NULL; }
    if (!line_init_into(&d->rd, out[0])) { close(in[1]); close(out[0]); free(d); return NULL; }
    d->wfd = in[1];
    d->pid = pid;
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                         /* no SA_RESTART: reads must EINTR */
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
    return d;
}

tycho_int dbgx_write(void *p, const char *s) {
    if (!p || !s) return -1;
    Dbg *d = p;
    size_t n = strlen(s), off = 0;
    while (off < n) {
        ssize_t w = write(d->wfd, s + off, n - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)w;
    }
    return (tycho_int)off;
}

void dbgx_close(void *p) {
    if (!p) return;
    Dbg *d = p;
    if (d->wfd >= 0) close(d->wfd);
    if (d->rd.fd >= 0) close(d->rd.fd);
    free(d->rd.buf);
    waitpid(d->pid, NULL, 0);
    free(d);
}

/* Absolute path of `p` (realpath(3)) into a static buffer -- the FFI copies
 * it. NULL (-> None) when the path does not exist; the tool fails closed. */
const char *dbgx_abs(const char *p) {
    if (!p) return NULL;
    static char buf[4096];
    return realpath(p, buf) ? buf : NULL;
}

tycho_int dbgx_pid(void) {
    return (tycho_int)getpid();
}

/* Deliver a signal to a PID. The debugger's Ctrl-C path uses this on the
 * INFERIOR's pid (learned from gdb's `=thread-group-started` record): in MI
 * sync mode gdb does not read commands while the inferior runs, so
 * `-exec-interrupt` written to the pipe sits unprocessed -- but SIGINT sent
 * straight to the inferior is intercepted by gdb (it is the tracer), which
 * stops it and reports `*stopped,reason="signal-received"` at the CURRENT
 * source line. Returns 0 on success, -1 when the pid is unknown or the
 * signal could not be delivered (the inferior already exited -- harmless). */
tycho_int dbgx_kill(tycho_int pid, tycho_int sig) {
    if (pid <= 0) return -1;
    return kill((pid_t)pid, (int)sig) == 0 ? 0 : -1;
}
