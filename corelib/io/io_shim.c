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
#include <fcntl.h>               /* open(2) + O_RDONLY -- iox_read_at */
#include <unistd.h>              /* pread(2) + close(2) -- iox_read_at */
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

/* Read AT MOST `n` bytes of `path` starting at absolute byte offset `off`, in the
 * same *status code space and the same out-param shape iox_read_file uses above:
 *
 *      TY_RF_OK   (1)  the read was performed; *out holds *outlen bytes, and
 *                      *outlen may be LESS than n (see BOUNDS below) or 0
 *      TY_RF_MISS (0)  no such path
 *      TY_RF_DIR  (2)  the path names a directory
 *      TY_RF_ERR  (3)  it is there and could not be read -- permissions, a
 *                      non-regular file, or a REJECTED ARGUMENT (see NEGATIVE)
 *
 * pread(2), NOT lseek+read. pread reads at an absolute offset without touching
 * the file description's own offset, so this needs no open-file handle threaded
 * through Tycho and no seek/read pair that a second caller could interleave with.
 * The fd is opened and closed inside one call; nothing is shared, so nothing races.
 *
 * BOUNDS -- the three cases are three different answers, on purpose:
 *
 *   OFFSET PAST EOF -> TY_RF_OK with *outlen == 0. This is pread(2)'s own answer
 *     and it is not an error: there genuinely are no bytes there. It is also not
 *     the ambiguous empty that iox_read_file was fixed for -- there, empty could
 *     mean an empty file OR a missing one OR a directory, and the caller had no
 *     way to tell. Here the caller CHOSE the offset, so "0 bytes at offset 900 of
 *     a 26-byte file" is a complete answer to the question it asked.
 *
 *   LENGTH RUNNING PAST EOF -> a SHORT read: TY_RF_OK with *outlen < n. The buffer
 *     is not padded and the shortfall is not an error. *outlen is how the caller
 *     learns how much it got, which is why no third out-param is needed.
 *
 *   NEGATIVE off OR n -> TY_RF_ERR, rejected HERE, before open(2) and before any
 *     cast. This is the one that has to be a guard rather than a consequence: a
 *     negative tycho_int cast to a 32-bit off_t is implementation-defined, and on
 *     any width the value must never reach pread(2) where a sign-extended offset
 *     would be a wild read. Fail closed, ahead of the syscall.
 *
 * THE ALLOCATION IS BOUNDED BY THE FILE, NOT BY n. fstat gives the size, so the
 * buffer is min(n, size - off) -- never n. This is the boundary where untrusted
 * input meets malloc: a `Range:` header naming a terabyte allocates only what the
 * file actually holds beyond `off`. No arbitrary cap is imposed on top, and that
 * is deliberate -- a cap is a policy number with no principled value, and clamping
 * to the file already removes the attack, since an attacker cannot make the
 * allocation exceed the file they could have fetched whole anyway.
 *
 * The clamp also makes the off_t cast safe: after it, off < size, and size came
 * from an off_t, so `off` provably fits back into one.
 *
 * REGULAR FILES ONLY. A directory is TY_RF_DIR (matching iox_read_file); a fifo,
 * socket or device is TY_RF_ERR, because st_size is meaningless for them and
 * pread(2) fails ESPIPE on anything unseekable -- "the byte at offset N" is not a
 * question those objects have an answer to. */
void iox_read_at(const char *path, tycho_int off, tycho_int n, tycho_int *status,
                 unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    *status = TY_RF_ERR;
    if (off < 0 || n < 0) return;                    /* fail closed, before open(2) */
    errno = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) { *status = ty_rf_errno(); return; }
    struct stat st;
    if (fstat(fd, &st) != 0) { *status = ty_rf_errno(); close(fd); return; }
    if (S_ISDIR(st.st_mode)) { *status = TY_RF_DIR; close(fd); return; }
    if (!S_ISREG(st.st_mode)) { close(fd); return; }  /* fifo/socket/device: TY_RF_ERR */
    tycho_int size = (tycho_int)st.st_size;
    if (off >= size) { *status = TY_RF_OK; close(fd); return; }   /* past EOF: 0 bytes, Ok */
    tycho_int avail = size - off;
    tycho_int want = n < avail ? n : avail;           /* bounded by the FILE, not by n */
    if (want == 0) { *status = TY_RF_OK; close(fd); return; }     /* n == 0: Ok, empty */
    unsigned char *buf = malloc((size_t)want);
    if (!buf) { close(fd); return; }                  /* fail closed: *out stays NULL */
    tycho_int got = 0;
    while (got < want) {
        ssize_t r = pread(fd, buf + got, (size_t)(want - got), (off_t)(off + got));
        if (r < 0) {
            if (errno == EINTR) continue;             /* a signal, not a failure */
            *status = ty_rf_errno();
            free(buf);
            close(fd);
            return;
        }
        if (r == 0) break;      /* the file shrank under us -- a short read, still Ok */
        got += (tycho_int)r;
    }
    close(fd);
    *out = buf;
    *outlen = got;
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
 * (FRICTION.md, phase 7; the option-result plan). No Result and no error enum can
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

/* The MODIFICATION TIME of `path`, in whole seconds since the UNIX epoch,
 * written through *mtime. The return is the same status code space as
 * iox_stat_kind, minus the kind distinction:
 *
 *      TY_RF_OK   (1)  stat succeeded; *mtime holds the epoch seconds
 *      TY_RF_MISS (0)  no such path (ENOENT/ENOTDIR)
 *      TY_RF_ERR  (3)  it is there and cannot be stat'ed (EACCES, ELOOP, ...)
 *
 * WHY AN OUT-PARAM, AND WHY NOT JUST EXTEND iox_stat_kind. iox_stat_kind above
 * returns a plain scalar because the kind and the failure share one code space.
 * An mtime cannot join that space: it is an ordinary integer, so a file last
 * modified at epoch second 2 would be indistinguishable from TY_RF_DIR. So the
 * payload and the status have to occupy different slots -- the same separation
 * iox_read_file makes, with each half in whichever slot can hold it. There the
 * payload is `bytes`, which must be the return, so the status took the `inout`;
 * here the payload is a scalar, so it takes the `inout` and the status keeps the
 * return and its shared code space intact.
 *
 * A SIBLING, NOT AN EXTRA OUT-PARAM ON iox_stat_kind. Folding mtime into
 * iox_stat_kind would fetch both from one stat(2), but its three callers --
 * io.is_dir, io.exists and iox_make_dir below, which calls it from C -- would
 * each have to pass an mtime they discard, to spare a syscall for a caller that
 * did not ask. The cost of the split is one extra stat(2) for a caller that
 * wants both; a conditional GET that answers 304 wants only this one, and never
 * opens the file at all.
 *
 * WHOLE SECONDS ON PURPOSE. st_mtime is st_mtim.tv_sec on any POSIX.1-2008
 * system and the classic field everywhere else, so this needs no feature macro
 * and no macOS st_mtimespec spelling. The sub-second half (st_mtim.tv_nsec) is
 * deliberately dropped: HTTP-date is whole seconds, datetime.from_unix takes
 * whole seconds, and a second out-param nothing can consume is precision spent
 * on no one. A caller that needs nanoseconds adds a second `inout` here. */
tycho_int iox_stat_mtime(const char *path, tycho_int *mtime) {
    struct stat st;
    *mtime = 0;
    errno = 0;
    if (stat(path, &st) != 0) return ty_rf_errno();   /* ENOENT/ENOTDIR -> MISS */
    *mtime = (tycho_int)st.st_mtime;
    return TY_RF_OK;
}

/* The SIZE IN BYTES of the regular file `path`, written through *size. Same
 * status code space as iox_stat_mtime above:
 *
 *      TY_RF_OK   (1)  stat succeeded and `path` is a regular file; *size holds
 *                      its length, and 0 is a SUCCESS (an empty file has a length)
 *      TY_RF_MISS (0)  no such path (ENOENT/ENOTDIR)
 *      TY_RF_DIR  (2)  the path names a directory -- see below, this is NOT the
 *                      same call as iox_stat_mtime's
 *      TY_RF_ERR  (3)  it is there and has no byte length: unstattable (EACCES,
 *                      ELOOP), or a fifo/socket/device
 *
 * The out-param/return split is iox_stat_mtime's exactly, for its reason: the
 * payload is a scalar, so it cannot share a code space with 0..3 (a 2-byte file
 * would be indistinguishable from TY_RF_DIR), and a scalar CAN be an `inout`,
 * so the payload takes the `inout` and the status keeps the return.
 *
 * A THIRD stat(2) SIBLING, not a second out-param on iox_stat_mtime. Phase 1
 * refused to fold mtime into iox_stat_kind by counting the callers who would
 * pass a value they discard, and the same count decides this -- it just lands
 * differently. iox_stat_mtime has ONE caller (io.mtime), so folding a size into
 * it costs one discard, not three. But it costs a second one immediately: io.size
 * would then call iox_stat_mtime and discard the mtime, so BOTH public calls
 * would be discarding half of a merged fetch, and the merged function's name
 * would have to stop being iox_stat_mtime. Two discards and a rename, to save a
 * syscall for a caller that wants both -- and no such caller exists: a Range
 * request wants a size and a slice, not a size and a date.
 *
 * NOR IS IT SHARED WITH iox_read_at, which already fstats. That fstat happens on
 * an fd that is opened, read and closed inside one call, and the three callers
 * that need a size need it BEFORE they know what to read: a 416 emits
 * `Content-Range: bytes *\/LEN` and reads nothing at all, a suffix range
 * `bytes=-N` needs LEN to compute its start, and `bytes=A-` needs LEN to name its
 * end. Returning a size out of read_at would hand it back after the decision that
 * needed it. The cost of the split is one extra stat(2) on the path that then
 * also reads; the 416 path pays one stat and opens nothing.
 *
 * A DIRECTORY IS TY_RF_DIR HERE, AND THAT DIFFERS FROM iox_stat_mtime ON PURPOSE.
 * Phase 1 made a directory's mtime TY_RF_OK because refusing it would discard a
 * field the kernel had already filled in. That argument does not carry here,
 * because st_size on a directory is not an answer to this question: it is the
 * size of the directory's own on-disk entry structure (4096 on ext4, something
 * else on btrfs, 0 on some filesystems), not a count of bytes anyone can read.
 * A directory's mtime IS the modification time being asked for; a directory's
 * st_size is not the byte length being asked for. So this is not discarding an
 * answer, it is declining to report a number that would be one -- and the caller
 * that would be misled is exactly the one this exists for, which would answer a
 * 416 with `bytes *\/4096` for a path holding no bytes at all.
 *
 * The rule that keeps the two calls honest with each other: iox_stat_size
 * succeeds on exactly the paths iox_read_at can read. Regular file -> both work;
 * directory -> both TY_RF_DIR; fifo/socket/device -> both TY_RF_ERR, since
 * st_size is meaningless for them and pread(2) fails ESPIPE on anything
 * unseekable. A size no read could ever produce is worse than no size. */
tycho_int iox_stat_size(const char *path, tycho_int *size) {
    struct stat st;
    *size = 0;
    errno = 0;
    if (stat(path, &st) != 0) return ty_rf_errno();   /* ENOENT/ENOTDIR -> MISS */
    if (S_ISDIR(st.st_mode)) return TY_RF_DIR;
    if (!S_ISREG(st.st_mode)) return TY_RF_ERR;       /* fifo/socket/device: no length */
    *size = (tycho_int)st.st_size;
    return TY_RF_OK;
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
 * syscall test (the option-result plan). */
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

/* Write ALL of `data` (datalen bytes) to `path`, truncating it first. Same
 * status code space as iox_read_file:
 *
 *      TY_RF_OK   (1)  every byte was written (datalen == 0 truncates and is Ok)
 *      TY_RF_MISS (0)  a path component does not exist (ENOENT/ENOTDIR)
 *      TY_RF_DIR  (2)  the path names a directory
 *      TY_RF_ERR  (3)  could not write -- EACCES, ENOSPC, EROFS, EISDIR on
 *                      some platforms, or a PARTIAL write (disk full mid-file)
 *
 * All-or-nothing: a partial fwrite is an error, not a short success. A file
 * that half-wrote is worse than one that did not write -- the caller can
 * remove() the residue -- and the length is known, so a short write is never
 * ambiguous. `data` arrives as `const unsigned char *` plus an implicit length
 * (the compiler's bytes-input FFI shape); NULL with datalen 0 is the empty
 * bytes value and must not be written. */
void iox_write_bytes(const char *path, const unsigned char *data,
                     tycho_int datalen, tycho_int *status) {
    *status = TY_RF_ERR;
    errno = 0;
    FILE *f = fopen(path, "wb");
    if (!f) { *status = ty_rf_errno(); return; }       /* fail closed: nothing written */
    if (datalen > 0 && data && fwrite(data, 1, (size_t)datalen, f) != (size_t)datalen) {
        *status = ty_rf_errno();                        /* partial write: an error */
        fclose(f);
        return;
    }
    if (fclose(f) != 0) { *status = ty_rf_errno(); return; }
    *status = TY_RF_OK;
}
