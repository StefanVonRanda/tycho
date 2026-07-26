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
#include <sys/stat.h>            /* stat(2) + S_ISDIR -- iox_stat_kind */
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

/* What KIND of thing `path` is, in the same status codes iox_read_file uses:
 *
 *      TY_RF_DIR  (2)  it is a directory
 *      TY_RF_OK   (1)  it exists and is NOT a directory (file, symlink target,
 *                      fifo, device -- anything a server would try to send)
 *      TY_RF_MISS (0)  no such path (ENOENT), or a non-directory used as one
 *                      (ENOTDIR, e.g. "/etc/hostname/x")
 *      TY_RF_ERR  (3)  it is there but cannot be stat'ed (EACCES on a parent
 *                      directory, ELOOP, ENAMETOOLONG, ...)
 *
 * This is the syscall that was missing, and its absence was a documented wrong
 * answer for a whole plan: with only list_dir, "is this a directory" had to be
 * asked as `len(io.list(p)) > 0`, which calls an EMPTY directory a file
 * (FRICTION.md, phase 7; plan.md phase 4). No Result and no error enum can
 * express a question the OS was never asked -- so we ask it.
 *
 * stat(2), not lstat(2), on purpose: a symlink to a directory IS a directory for
 * the purpose of "does this URL need a trailing slash". Returns a plain scalar --
 * no `inout` status is needed because the kind and the failure share one code
 * space, unlike iox_read_file where a `bytes` payload occupies the return. */
tycho_int iox_stat_kind(const char *path) {
    struct stat st;
    errno = 0;
    if (stat(path, &st) != 0) return ty_rf_errno();   /* ENOENT/ENOTDIR -> MISS */
    return S_ISDIR(st.st_mode) ? TY_RF_DIR : TY_RF_OK;
}

/* One code the read/stat space has no use for: the path is occupied by something
 * that is NOT a directory, so make_dir cannot have what it asked for. TY_RF_OK
 * cannot carry it -- OK already means "I did it" below. */
#define TY_FS_EXISTS 4

/* mkdir(2) the SINGLE directory `path` (0777 & ~umask). Parents are not created:
 * this is mkdir, not `mkdir -p`, and a missing parent is TY_RF_MISS.
 *
 *      TY_RF_OK     (1)  created it
 *      TY_RF_DIR    (2)  it was ALREADY a directory -- EEXIST, and the caller's
 *                        goal already holds, so core:io reports it as Ok(false)
 *      TY_FS_EXISTS (4)  EEXIST on something that is not a directory
 *      TY_RF_MISS   (0)  a path component does not exist (ENOENT/ENOTDIR)
 *      TY_RF_ERR    (3)  anything else (EACCES, EROFS, ENOSPC, ELOOP, ...)
 *
 * EEXIST is the reason this returns a code and not a bool: "already a directory"
 * and "a file is in the way" are the same failed mkdir but opposite answers to
 * the only question a caller has. stat(2) separates them, so ask it -- the same
 * move iox_stat_kind is, one layer up. Until this function existed there was no
 * way to make a directory from Tycho at all, so corelib/test/io built one with
 * os.system("mkdir -p"), i.e. a corelib test shelling out to /bin/sh to set up a
 * syscall test (plan.md phase 5). */
tycho_int iox_make_dir(const char *path) {
    errno = 0;
    if (mkdir(path, 0777) == 0) return TY_RF_OK;
    if (errno == EEXIST)                                  /* who is in the way? */
        return iox_stat_kind(path) == TY_RF_DIR ? TY_RF_DIR : TY_FS_EXISTS;
    return ty_rf_errno();
}

/* remove(3) ONE entry: unlink(2) for a non-directory, rmdir(2) for a directory.
 *
 *      TY_RF_OK   (1)  it was there and is gone now
 *      TY_RF_MISS (0)  nothing was there (ENOENT/ENOTDIR) -- core:io reports this
 *                      as Ok(false), because "make it not exist" already holds
 *      TY_RF_ERR  (3)  it is there and could not be removed: a NON-EMPTY
 *                      directory (ENOTEMPTY, or EEXIST on some systems), EACCES,
 *                      EBUSY, EPERM on a sticky parent, ...
 *
 * Deliberately NOT recursive. A tree walk is a different function with a much
 * worse failure mode, and the one caller that needs cleanup (corelib/test/io)
 * removes what it made, in order. So ENOTEMPTY is a real error here, and that is
 * the property that keeps this from being `rm -rf` in a shim. */
tycho_int iox_remove(const char *path) {
    errno = 0;
    if (remove(path) == 0) return TY_RF_OK;
    return ty_rf_errno();
}
