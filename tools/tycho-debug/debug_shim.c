#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE          /* glibc: expose setpgid without -std noise */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>   /* CreateProcess, pipes, SetConsoleCtrlHandler */
#include <process.h>   /* _getpid */
#include <io.h>        /* _open_osfhandle -- HANDLE -> the fd the Line reader uses */
#include <fcntl.h>     /* _O_RDONLY/_O_WRONLY for _open_osfhandle */
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

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
#ifdef _WIN32
    HANDLE proc;             /* the gdb process, for WaitForSingleObject */
#else
    pid_t pid;
#endif
} Dbg;

static void dbgx_install_sigint(void) {
#ifdef _WIN32
    SetConsoleCtrlHandler(
        (PHANDLER_ROUTINE)(void (*)(void))on_sigint, TRUE);
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                         /* no SA_RESTART: reads must EINTR */
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
#endif
}

#ifdef _WIN32
void *dbgx_spawn(const char *cmd) {
    if (!cmd) return NULL;
    HANDLE in_r, in_w, out_r, out_w;
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&in_r, &in_w, &sa, 0)) return NULL;
    if (!CreatePipe(&out_r, &out_w, &sa, 0)) { CloseHandle(in_r); CloseHandle(in_w); return NULL; }
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);    /* parent end only */
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    char *cmdline = malloc(strlen(cmd) + 16);
    if (!cmdline) { CloseHandle(in_r); CloseHandle(in_w); CloseHandle(out_r); CloseHandle(out_w); return NULL; }
    sprintf(cmdline, "cmd.exe /c %s", cmd);
    STARTUPINFOA si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_r;
    si.hStdOutput = out_w;
    si.hStdError = out_w;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof pi);
    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                             CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi);
    free(cmdline);
    CloseHandle(in_r);                        /* the child has its copies */
    CloseHandle(out_w);
    if (!ok) { CloseHandle(in_w); CloseHandle(out_r); return NULL; }
    CloseHandle(pi.hThread);
    Dbg *d = calloc(1, sizeof *d);
    if (!d) { CloseHandle(in_w); CloseHandle(out_r); CloseHandle(pi.hProcess); return NULL; }
    int rfd = _open_osfhandle((intptr_t)out_r, _O_RDONLY);
    int wfd = _open_osfhandle((intptr_t)in_w, _O_WRONLY);
    if (rfd < 0 || wfd < 0 || !line_init_into(&d->rd, rfd)) {
        if (rfd >= 0) _close(rfd);
        if (wfd >= 0) _close(wfd);
        CloseHandle(pi.hProcess);
        free(d);
        return NULL;
    }
    d->wfd = wfd;
    d->proc = pi.hProcess;
    dbgx_install_sigint();
    return d;
}
#else
/* POSIX spawn: /bin/sh -c, own process group, SIGINT handler without
 * SA_RESTART (the blocked read EINTRs and surfaces the flag). */
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
    dbgx_install_sigint();
    return d;
}
#endif

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
#ifdef _WIN32
    WaitForSingleObject(d->proc, INFINITE);
    CloseHandle(d->proc);
#else
    waitpid(d->pid, NULL, 0);
#endif
    free(d);
}

/* Absolute path of `p` (realpath(3)) into a static buffer -- the FFI copies
 * it. NULL (-> None) when the path does not exist; the tool fails closed. */
const char *dbgx_abs(const char *p) {
    if (!p) return NULL;
    static char buf[4096];
#ifdef _WIN32
    /* _fullpath resolves the STRING and succeeds for a path that does not
     * exist, where realpath fails -- so on Windows the tool sailed past its
     * "no such file" refusal and reported a compile failure instead, for any
     * missing program. Check existence to keep realpath's contract. */
    if (GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES) return NULL;
    return _fullpath(buf, p, sizeof buf) ? buf : NULL;
#else
    return realpath(p, buf) ? buf : NULL;
#endif
}

tycho_int dbgx_pid(void) {
#ifdef _WIN32
    return (tycho_int)_getpid();
#else
    return (tycho_int)getpid();
#endif
}

/* 1 under _WIN32, else 0 -- picks the null device, the temp root, the .exe
 * suffix and the argument-quoting style in main.ty. */
tycho_int dbgx_is_windows(void) {
#ifdef _WIN32
    return 1;
#else
    return 0;
#endif
}

tycho_int dbgx_run(const char *cmd) {
    if (!cmd) return -1;
#ifdef _WIN32
    {
        char *copy = _strdup(cmd);
        if (copy) {
            for (char *q = copy; *q && *q != ' ' && *q != '\t'; q++)
                if (*q == '/') *q = '\\';
            tycho_int r = (tycho_int)system(copy);
            free(copy);
            return r;
        }
    }
#endif
    return (tycho_int)system(cmd);
}

/* Delete one file (the temp debuggee binary at quit). Was `rm -f`, which
 * cmd.exe does not have. 0 on success, -1 otherwise -- the caller is a cleanup
 * path and ignores it, exactly as `rm -f` swallowed a missing file. */
tycho_int dbgx_remove(const char *path) {
    if (!path) return -1;
    return remove(path) == 0 ? 0 : -1;
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
#ifdef _WIN32
    (void)pid; (void)sig;
    return -1;
#else
    if (pid <= 0) return -1;
    return kill((pid_t)pid, (int)sig) == 0 ? 0 : -1;
#endif
}
