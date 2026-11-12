#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE          /* cfmakeraw under -std=c11 */
#endif
#ifndef _WIN32
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600        /* posix_openpt / ptsname */
#endif
#endif
#include <stdlib.h>
#include <stdint.h>
#include "../tycho.h"

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#endif

tycho_int termx_is_tty(tycho_int fd) {
#ifdef _WIN32
    return _isatty((int)fd) ? 1 : 0;
#else
    return isatty((int)fd) ? 1 : 0;
#endif
}

tycho_int termx_cols(tycho_int fd) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    HANDLE h = (HANDLE)_get_osfhandle((int)fd);
    if (h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &info))
        return info.srWindow.Right - info.srWindow.Left + 1;
    const char *e = getenv("COLUMNS");
    return e ? atoi(e) : 0;
#else
    struct winsize w;
    if (ioctl((int)fd, TIOCGWINSZ, &w) != 0) return 0;
    return w.ws_col;
#endif
}

tycho_int termx_rows(tycho_int fd) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    HANDLE h = (HANDLE)_get_osfhandle((int)fd);
    if (h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &info))
        return info.srWindow.Bottom - info.srWindow.Top + 1;
    const char *e = getenv("LINES");
    return e ? atoi(e) : 0;
#else
    struct winsize w;
    if (ioctl((int)fd, TIOCGWINSZ, &w) != 0) return 0;
    return w.ws_row;
#endif
}

tycho_int termx_ansi(tycho_int fd) {
#ifdef _WIN32
    HANDLE h = (HANDLE)_get_osfhandle((int)fd);
    DWORD mode;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        if (!(mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            if (!SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) return 0;
        }
        return 1;                           /* Windows 10+ console with VT enabled */
    }
    return _isatty((int)fd) ? 1 : 0;        /* MSYS pty / redirected terminal */
#else
    return isatty((int)fd) ? 1 : 0;
#endif
}

#ifdef _WIN32

typedef struct {
    HANDLE in;
    DWORD old_in;
    HANDLE out;
    DWORD old_out;
} TermRaw;

void *termx_raw_enter(tycho_int fd) {
    if (!_isatty((int)fd)) return NULL;
    TermRaw *r = (TermRaw *)malloc(sizeof *r);
    if (!r) return NULL;
    r->in = GetStdHandle(STD_INPUT_HANDLE);
    r->out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (r->in == INVALID_HANDLE_VALUE || !GetConsoleMode(r->in, &r->old_in)) {
        /* Non-console tty (MSYS pty): no console modes to change. */
        r->in = INVALID_HANDLE_VALUE;
        r->out = INVALID_HANDLE_VALUE;
        return r;
    }
    DWORD ni = r->old_in & ~(DWORD)(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    if (!SetConsoleMode(r->in, ni)) { free(r); return NULL; }
    if (r->out != INVALID_HANDLE_VALUE && GetConsoleMode(r->out, &r->old_out)) {
        SetConsoleMode(r->out, r->old_out | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    } else {
        r->out = INVALID_HANDLE_VALUE;
    }
    return r;
}

void termx_raw_leave(void *vp) {
    TermRaw *r = (TermRaw *)vp;
    if (!r) return;
    SetConsoleMode(r->in, r->old_in);
    if (r->out != INVALID_HANDLE_VALUE) SetConsoleMode(r->out, r->old_out);
    free(r);
}

tycho_int termx_read_byte(tycho_int fd) {
    unsigned char c;
    return _read((int)fd, &c, 1) == 1 ? (tycho_int)c : -1;
}

#else

typedef struct {
    int fd;
    struct termios old;
} TermRaw;

void *termx_raw_enter(tycho_int fd) {
    if (!isatty((int)fd)) return NULL;
    TermRaw *r = (TermRaw *)malloc(sizeof *r);
    if (!r) return NULL;
    r->fd = (int)fd;
    if (tcgetattr((int)fd, &r->old) != 0) { free(r); return NULL; }
    struct termios raw = r->old;
    cfmakeraw(&raw);
    if (tcsetattr((int)fd, TCSAFLUSH, &raw) != 0) { free(r); return NULL; }
    return r;
}

void termx_raw_leave(void *vp) {
    TermRaw *r = (TermRaw *)vp;
    if (!r) return;
    tcsetattr(r->fd, TCSAFLUSH, &r->old);
    free(r);
}

tycho_int termx_read_byte(tycho_int fd) {
    unsigned char c;
    return read((int)fd, &c, 1) == 1 ? (tycho_int)c : -1;
}

#endif

/* Parse one byte of a CSI row;col R response. Returns 1 when complete. */
static int termx_parse_cursor_byte(int *state, int *row, int *col, unsigned char ch) {
    switch (*state) {
    case 0:
        if (ch == 0x1b) *state = 1;
        return 0;
    case 1:
        if (ch == '[') *state = 2;
        else { *state = 0; *row = 0; *col = 0; }
        return 0;
    case 2:
        if (ch == '?') *state = 3;
        else if (ch >= '0' && ch <= '9') { *row = ch - '0'; *state = 4; }
        else { *state = 0; *row = 0; *col = 0; }
        return 0;
    case 3:
        if (ch >= '0' && ch <= '9') { *row = ch - '0'; *state = 4; }
        else { *state = 0; *row = 0; *col = 0; }
        return 0;
    case 4:
        if (ch >= '0' && ch <= '9') *row = *row * 10 + (ch - '0');
        else if (ch == ';') *state = 5;
        else { *state = 0; *row = 0; *col = 0; }
        return 0;
    case 5:
        if (ch >= '0' && ch <= '9') { *col = ch - '0'; *state = 6; }
        else { *state = 0; *row = 0; *col = 0; }
        return 0;
    case 6:
        if (ch >= '0' && ch <= '9') *col = *col * 10 + (ch - '0');
        else if (ch == 'R') return 1;
        else { *state = 0; *row = 0; *col = 0; }
        return 0;
    }
    return 0;
}

/* 0 ok, 1 not a tty, 2 timeout, 3 bad response, 4 failed/bad arg. */
tycho_int termx_cursor_query(tycho_int out_fd, tycho_int in_fd, tycho_int timeout_ms,
                             tycho_int *row, tycho_int *col) {
    *row = 0;
    *col = 0;
    if (out_fd < 0 || in_fd < 0 || timeout_ms < 0) return 4;
#ifdef _WIN32
    if (!_isatty((int)out_fd) || !_isatty((int)in_fd)) return 1;
    HANDLE hout = (HANDLE)_get_osfhandle((int)out_fd);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (hout != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(hout, &info)) {
        int r = (int)(info.dwCursorPosition.Y - info.srWindow.Top + 1);
        int c = (int)(info.dwCursorPosition.X - info.srWindow.Left + 1);
        if (r <= 0 || c <= 0) return 3;
        *row = r;
        *col = c;
        return 0;
    }
    /* Non-console tty (MSYS pty): fall back to the ANSI DSR query. */
    {
        const char q[] = "\x1b[6n";
        int off = 0;
        while (off < 4) {
            int n = _write((int)out_fd, q + off, 4 - off);
            if (n <= 0) return 4;
            off += n;
        }
        HANDLE hin = (HANDLE)_get_osfhandle((int)in_fd);
        if (hin == INVALID_HANDLE_VALUE) return 4;
        int state = 0, r = 0, c = 0;
        DWORD start = GetTickCount();
        for (;;) {
            DWORD elapsed = GetTickCount() - start;
            if (elapsed >= (DWORD)timeout_ms) return 2;
            DWORD wait = WaitForSingleObject(hin, (DWORD)timeout_ms - elapsed);
            if (wait == WAIT_TIMEOUT) return 2;
            if (wait != WAIT_OBJECT_0) return 4;
            unsigned char ch;
            if (_read((int)in_fd, &ch, 1) != 1) return 4;
            if (termx_parse_cursor_byte(&state, &r, &c, ch) == 1) {
                if (r <= 0 || c <= 0) return 3;
                *row = r;
                *col = c;
                return 0;
            }
        }
    }
#else
    if (!isatty((int)out_fd) || !isatty((int)in_fd)) return 1;
    struct termios old, raw;
    int have_term = tcgetattr((int)in_fd, &old) == 0;
    if (have_term) {
        raw = old;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr((int)in_fd, TCSANOW, &raw);
    }
    {
        const char q[] = "\x1b[6n";
        int off = 0;
        while (off < 4) {
            ssize_t n = write((int)out_fd, q + off, (size_t)(4 - off));
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                if (have_term) tcsetattr((int)in_fd, TCSANOW, &old);
                return 4;
            }
            off += (int)n;
        }
        int state = 0, r = 0, c = 0;
        int result = 4;
        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (;;) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long long elapsed = (long long)(now.tv_sec - start.tv_sec) * 1000
                              + (now.tv_nsec - start.tv_nsec) / 1000000;
            int remaining = (int)(timeout_ms - elapsed);
            if (remaining < 0) remaining = 0;
            struct pollfd pfd;
            pfd.fd = (int)in_fd;
            pfd.events = POLLIN;
            pfd.revents = 0;
            int pr = poll(&pfd, 1, remaining);
            if (pr == 0) { result = 2; break; }
            if (pr < 0) {
                if (errno == EINTR) continue;
                result = 4;
                break;
            }
            unsigned char ch;
            ssize_t n = read((int)in_fd, &ch, 1);
            if (n != 1) {
                if (n < 0 && errno == EINTR) continue;
                result = 4;
                break;
            }
            if (termx_parse_cursor_byte(&state, &r, &c, ch) == 1) {
                if (r <= 0 || c <= 0) { result = 3; break; }
                *row = r;
                *col = c;
                result = 0;
                break;
            }
        }
        if (have_term) tcsetattr((int)in_fd, TCSANOW, &old);
        return result;
    }
#endif
}

/* Test-only pseudo-terminal pair. NULL on Windows or when the pty cannot open. */
#ifndef _WIN32
typedef struct { int master; int slave; } TermPty;

void *termx_pty_open(void) {
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0) return NULL;
    if (grantpt(m) != 0 || unlockpt(m) != 0) { close(m); return NULL; }
    char *name = ptsname(m);
    if (!name) { close(m); return NULL; }
    int s = open(name, O_RDWR | O_NOCTTY);
    if (s < 0) { close(m); return NULL; }
    TermPty *p = (TermPty *)malloc(sizeof *p);
    if (!p) { close(m); close(s); return NULL; }
    p->master = m;
    p->slave = s;
    return p;
}

tycho_int termx_pty_master(void *vp) { return vp ? ((TermPty *)vp)->master : -1; }
tycho_int termx_pty_slave(void *vp) { return vp ? ((TermPty *)vp)->slave : -1; }

void termx_pty_close(void *vp) {
    TermPty *p = (TermPty *)vp;
    if (!p) return;
    close(p->master);
    close(p->slave);
    free(p);
}
#else
void *termx_pty_open(void) { return NULL; }
tycho_int termx_pty_master(void *vp) { (void)vp; return -1; }
tycho_int termx_pty_slave(void *vp) { (void)vp; return -1; }
void termx_pty_close(void *vp) { (void)vp; }
#endif
