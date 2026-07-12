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

/* typed handle (FFI R2): a refcounted "resource". ffi_res_open bumps a live
 * count and returns an opaque handle; the compiler frees it via ffi_res_close
 * (the handle's `free:`) at scope exit -- so res_live returns to 0 iff the
 * destructor ran exactly once. */
static long g_res_live = 0;
void *ffi_res_open(long id) { (void)id; g_res_live++; return (void *)1; }
long ffi_res_close(void *h) { (void)h; g_res_live--; return 0; }
long ffi_res_use(void *h) { return h ? 7 : -1; }
long ffi_res_live(void) { return g_res_live; }

/* FFI R3a: nullable string return. A hit returns a string; a miss returns NULL,
 * which `extern ... -> Option(string)` surfaces as None (where `-> string` would
 * silently map the NULL to ""). */
const char *ffi_get(long key) { return key > 0 ? "hit" : 0; }

/* FFI R4: out-parameter constructors (the sqlite3_open(path, &db) -> rc shape).
 * ffi_mk writes an opaque handle through a `ptr` out-param (read back with ffi_read)
 * and returns a status; ffi_dbl writes through a scalar `int` out-param. The compiler
 * passes &local for each `inout` arg, so no hand-written shim is needed. */
long ffi_mk(long id, void **out) { if (id <= 0) { *out = 0; return -1; } slot = (int)id; *out = &slot; return 0; }
void ffi_dbl(long a, long *out) { *out = a * 2; }

/* to_i32 / to_ptr coverage. ffi_neg returns a NEGATIVE 32-bit C int -- declared
 * `-> int` it is read as 64-bit (a huge positive); to_i32 recovers the sign.
 * ffi_ptrval echoes a pointer's integer value, so to_ptr(-1) (a SQLITE_TRANSIENT
 * -style sentinel) round-trips. */
int  ffi_neg(void)        { return -7; }
long ffi_ptrval(void *p)  { return (long)p; }

/* arrays cross as (const T*, long): ffi_isum sums an [int], ffi_favg averages a
 * [float]. Proves [int]/[float] params reach C as a flat (pointer, length) pair. */
long   ffi_isum(const long *xs, long n)   { long s = 0; for (long i = 0; i < n; i++) s += xs[i]; return s; }
double ffi_favg(const double *xs, long n) { if (n == 0) return 0.0; double s = 0; for (long i = 0; i < n; i++) s += xs[i]; return s / (double)n; }

/* array RETURN via the out-param shim: ffi_iota returns [0..n) as an [int];
 * ffi_dscale takes a [float] and returns a scaled [float] (param AND return). The
 * shim malloc's *out; tycho_arr_*_from_c copies into the arena and frees it. */
void ffi_iota(long n, long **out, long *outlen) {
    if (n < 0) n = 0;
    long *p = (long *)malloc(n ? (size_t)n * sizeof(long) : 1);
    for (long i = 0; i < n; i++) p[i] = i;
    *out = p; *outlen = n;
}
void ffi_dscale(const double *xs, long n, double k, double **out, long *outlen) {
    if (n < 0) n = 0;
    double *p = (double *)malloc(n ? (size_t)n * sizeof(double) : 1);
    for (long i = 0; i < n; i++) p[i] = xs[i] * k;
    *out = p; *outlen = n;
}
