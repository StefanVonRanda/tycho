/* core:io shim -- a bounded-memory streaming line reader over libc getline(3).
 *
 * read_lines() slurps a whole file into an array; this reads ONE line at a time
 * into a single reused buffer, so peak memory is O(longest line), not O(file).
 * The getline buffer is Tycho's to copy at the FFI boundary (arena-copied before
 * the next call overwrites it). Fail-closed: an unopenable path yields a null
 * handle; a read past EOF or on error yields NULL (-> None), never a partial or
 * stale line. Pure libc -- no external dependency, no `deps` file.
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE          /* glibc: expose getline + ssize_t */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
/* int64-migration (Phase 3): Tycho `int` lowers to tycho_int (int64_t) in the
 * emitted program; this shim is a separate translation unit, so it defines the
 * same type to match the FFI ABI on ILP32/LLP64, not just LP64. */
#ifndef TYCHO_INT_T
#define TYCHO_INT_T
typedef int64_t tycho_int;
#endif

typedef struct { FILE *f; char *buf; size_t cap; } IoLines;

void *iox_open_lines(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;                             /* fail closed: cannot open */
    IoLines *r = malloc(sizeof *r);
    if (!r) { fclose(f); return NULL; }
    r->f = f; r->buf = NULL; r->cap = 0;
    return r;
}

/* The next line, WITHOUT its trailing '\n' (and a preceding '\r' if present), or
 * NULL at end of file / error. The pointer is valid only until the next call or
 * close_lines -- Tycho arena-copies it immediately at the FFI boundary. An empty
 * line returns "" (a non-NULL empty string), distinct from EOF's NULL. */
const char *iox_read_line(void *p) {
    if (!p) return NULL;
    IoLines *r = p;
    ssize_t n = getline(&r->buf, &r->cap, r->f);
    if (n < 0) return NULL;                          /* EOF or read error */
    while (n > 0 && (r->buf[n - 1] == '\n' || r->buf[n - 1] == '\r'))
        r->buf[--n] = '\0';
    return r->buf;
}

void iox_close_lines(void *p) {
    if (!p) return;
    IoLines *r = p;
    if (r->f) fclose(r->f);                          /* fclose exactly once */
    free(r->buf);
    free(r);
}

/* Read the whole file at `path` as raw bytes -- binary-safe, so interior NUL
 * bytes survive (unlike the read_file builtin's string) -- CLASSIFYING the
 * outcome in *status:
 *
 *      TY_RF_OK   (1)  the file was read; *out holds *outlen bytes (0 is legal:
 *                      an EMPTY FILE is a success, not a failure)
 *      TY_RF_MISS (0)  no such path
 *      TY_RF_DIR  (2)  the path names a directory, not a file
 *      TY_RF_ERR  (3)  the path exists but could not be read (permissions, ...)
 *
 * The classification is the point of this signature. Until 2026-07-26 this
 * function returned only `bytes`, so an empty file, a missing file and a
 * directory were the SAME empty result -- the collision that makes a static file
 * server answer a 0-byte 200 for a path it cannot actually serve (FRICTION.md,
 * phase 7). The kernel distinguishes all three; only the FFI shape was throwing
 * the distinction away, so core:io now hands it up as Result(bytes, io.IoErr).
 *
 * A directory is detected the portable way, without stat(2): glibc lets
 * fopen(dir, "rb") succeed and fails the first read with EISDIR, while other
 * platforms fail the open with EISDIR -- both are checked.
 *
 * `status` is an `inout int` on the Tycho side, which lowers to a leading
 * tycho_int* out-param ahead of the two `bytes` return out-params (same shape as
 * net_shim's netx_read). On any failure *out stays NULL -- tycho_bytes_from_c
 * allocates an empty buffer and does not free NULL. */
#define TY_RF_MISS 0
#define TY_RF_OK   1
#define TY_RF_DIR  2
#define TY_RF_ERR  3

/* Map a failed fopen/fread errno onto the status codes above. */
static tycho_int ty_rf_errno(void) {
    if (errno == ENOENT || errno == ENOTDIR) return TY_RF_MISS;
    if (errno == EISDIR) return TY_RF_DIR;
    return TY_RF_ERR;
}

void iox_read_file(const char *path, tycho_int *status,
                   unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    *status = TY_RF_ERR;
    errno = 0;
    FILE *f = fopen(path, "rb");
    if (!f) { *status = ty_rf_errno(); return; }      /* fail closed: empty result */
    size_t cap = 4096, len = 0, n;
    unsigned char *buf = malloc(cap);
    if (!buf) { fclose(f); return; }
    errno = 0;
    while ((n = fread(buf + len, 1, cap - len, f)) > 0) {
        len += n;
        if (len == cap) {
            size_t nc = cap * 2;
            unsigned char *nb = realloc(buf, nc);
            if (!nb) { free(buf); fclose(f); return; }   /* fail closed */
            buf = nb; cap = nc;
        }
    }
    if (ferror(f)) {                                 /* a directory reaches here on glibc */
        *status = ty_rf_errno();
        free(buf);
        fclose(f);
        return;
    }
    fclose(f);
    *out = buf;
    *outlen = (tycho_int)len;
    *status = TY_RF_OK;
}
