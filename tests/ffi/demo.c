/* FFI test fixture — a tiny C library linked via `extern "ffidemo"`.
 * Exercises every Stage-1 boundary shape: scalar in/out (int + float) and
 * string in/out, plus a NULL string return (must surface as "" in tycho). */
#include <stdio.h>
#include <stdlib.h>

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

/* bytes: crosses as (ptr,len). ffi_bsum takes a bytes param (two C args);
 * ffi_brev RETURNS bytes via the out-param shim — it malloc's *out (the tycho
 * runtime copies it into an arena and frees it). Length-carried, so interior
 * 0x00 round-trips (a NUL-terminated string API could not do this). */
long ffi_bsum(const unsigned char *p, long n) {
    long s = 0;
    for (long i = 0; i < n; i++) s += p[i];
    return s;
}
void ffi_brev(const unsigned char *p, long n, unsigned char **out, long *outlen) {
    unsigned char *r = (unsigned char *)malloc(n > 0 ? (size_t)n : 1);
    for (long i = 0; i < n; i++) r[i] = p[n - 1 - i];
    *out = r;
    *outlen = n;
}
