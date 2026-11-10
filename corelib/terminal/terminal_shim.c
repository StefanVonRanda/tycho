#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE          /* cfmakeraw under -std=c11 */
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
    if (h == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(h, &info)) return 0;
    return info.srWindow.Right - info.srWindow.Left + 1;
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
    if (h == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(h, &info)) return 0;
    return info.srWindow.Bottom - info.srWindow.Top + 1;
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
    if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode)) return 0;
    return 1;                               /* Windows 10+ consoles understand VT */
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
        free(r); return NULL;
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
