/* FFI test fixture — a tiny C library linked via `extern "ffidemo"`.
 * Exercises every Stage-1 boundary shape: scalar in/out (int + float) and
 * string in/out, plus a NULL string return (must surface as "" in tycho). */
#include <stdio.h>

static char buf[256];

const char *ffi_echo(const char *s) {        /* string -> string */
    snprintf(buf, sizeof buf, "[%s]", s ? s : "(null)");
    return buf;
}

long ffi_sum3(long a, long b, long c) {       /* ints -> int */
    return a + b + c;
}

double ffi_scale(double x, long k) {          /* float + int -> float */
    return x * (double)k;
}

const char *ffi_maybe(const char *s) {        /* NULL-return path: empty -> NULL */
    return (!s || !*s) ? 0 : s;
}

/* Stage 2: opaque handles (ptr = void*). */
static int slot;
void *ffi_open(long id) {                     /* int -> ptr; NULL handle for id <= 0 */
    if (id <= 0) return 0;
    slot = (int)id;
    return &slot;
}
long ffi_read(void *h) {                       /* ptr -> int; -1 for a NULL handle */
    return h ? *(int *)h : -1;
}
