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
/* Every open(2) below MUST carry this. The MSVCRT open() defaults to TEXT
 * mode, which turns \n into \r\n on write and back on read -- on the byte
 * offsets iox_read_at/iox_write_at exist to address, that silently shifts
 * every byte after the first newline in a page and hands back a structure
 * whose string slots decode as NULL. It corrupted the tycho-kv B+ tree store
 * (same file size, contents shifted one byte from the first \n onward) and
 * crashed the next open of that store in tycho_str_copy(s=0x0). The fopen
 * calls in this file already say "rb"/"wb"; open(2) had no such marker.
 * POSIX has no text mode, so the flag does not exist there and is 0. */
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifdef _WIN32
#include <windows.h>           /* GetFileAttributesExA -- the UTC file time */
#include <io.h>                 /* _lseeki64 -- the pread/pwrite stand-ins */
#include <direct.h>             /* _mkdir -- the one-arg Windows mkdir */
#include <sys/utime.h>          /* _utime + struct _utimbuf -- iox_set_mtime */
#include <fcntl.h>              /* O_RDWR/O_CREAT are the same, but keep _O_BINARY honest */
/* mingw-w64 has none of getline(3)/pread(2)/pwrite(2) and a one-arg mkdir.
 * The getline stand-in is fgets-free: read one byte at a time into a doubling
 * buffer, strip the trailing newline (and a preceding \r -- the CRLF
 * convention). pread/pwrite keep the contract "read/write at an absolute
 * offset without disturbing the file position" via _lseeki64 + save/restore. */
static ssize_t ty_getline(char **bufp, size_t *capp, FILE *f) {
    size_t n = 0;
    for (;;) {
        if (n + 2 > *capp) {
            size_t nc = *capp ? *capp * 2 : 256;
            char *nb = realloc(*bufp, nc);
            if (!nb) return -1;               /* fail closed: caller sees EOF */
            *bufp = nb; *capp = nc;
        }
        int c = fgetc(f);
        if (c == EOF) { if (n == 0) return -1; (*bufp)[n] = '\0'; return (ssize_t)n; }
        if (c == '\n') { if (n > 0 && (*bufp)[n-1] == '\r') n--; (*bufp)[n] = '\0'; return (ssize_t)n; }
        (*bufp)[n++] = (char)c;
    }
}
static ssize_t ty_pread(int fd, void *buf, size_t n, long long off) {
    long long pos = _lseeki64(fd, 0, SEEK_CUR);
    if (pos < 0) return -1;
    if (_lseeki64(fd, off, SEEK_SET) < 0) return -1;
    ssize_t r = read(fd, buf, n);
    _lseeki64(fd, pos, SEEK_SET);
    return r;
}
static ssize_t ty_pwrite(int fd, const void *buf, size_t n, long long off) {
    long long pos = _lseeki64(fd, 0, SEEK_CUR);
    if (pos < 0) return -1;
    if (_lseeki64(fd, off, SEEK_SET) < 0) return -1;
    ssize_t r = write(fd, buf, n);
    _lseeki64(fd, pos, SEEK_SET);
    return r;
}
#define getline(b, c, f)  ty_getline((b), (c), (f))
#define pread(fd, b, n, o)  ty_pread((fd), (b), (n), (o))
#define pwrite(fd, b, n, o) ty_pwrite((fd), (b), (n), (o))
#define mkdir(p, m) _mkdir(p)   /* mingw's mkdir takes one argument */
#endif
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

#define TY_RF_MISS 0
#define TY_RF_OK   1
#define TY_RF_DIR  2
#define TY_RF_ERR  3

/* Map a failed fopen/fread errno onto the status codes above. `path` lets a
 * failure that the platform refuses to classify be asked of the kernel: on
 * Windows, fopen/open of a DIRECTORY fails with EACCES (not POSIX's EISDIR),
 * so an EACCES is re-checked by stat -- a directory is TY_RF_DIR, anything
 * else stays a real permission/other error. */
static tycho_int ty_rf_errno(const char *path) {
    if (errno == ENOENT || errno == ENOTDIR) return TY_RF_MISS;
    if (errno == EISDIR) return TY_RF_DIR;
    if (errno == EACCES && path) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return TY_RF_DIR;
    }
    return TY_RF_ERR;
}

void iox_read_file(const char *path, tycho_int *status,
                   unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    *status = TY_RF_ERR;
    errno = 0;
    FILE *f = fopen(path, "rb");
    if (!f) { *status = ty_rf_errno(path); return; }      /* fail closed: empty result */
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
        *status = ty_rf_errno(path);
        free(buf);
        fclose(f);
        return;
    }
    fclose(f);
    *out = buf;
    *outlen = (tycho_int)len;
    *status = TY_RF_OK;
}

void iox_read_at(const char *path, tycho_int off, tycho_int n, tycho_int *status,
                 unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    *status = TY_RF_ERR;
    if (off < 0 || n < 0) return;                    /* fail closed, before open(2) */
    errno = 0;
    int fd = open(path, O_RDONLY | O_BINARY);
    if (fd < 0) { *status = ty_rf_errno(path); return; }
    struct stat st;
    if (fstat(fd, &st) != 0) { *status = ty_rf_errno(path); close(fd); return; }
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
            *status = ty_rf_errno(path);
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

tycho_int iox_stat_kind(const char *path) {
    struct stat st;
    errno = 0;
    if (stat(path, &st) != 0) return ty_rf_errno(path);   /* ENOENT/ENOTDIR -> MISS */
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
    if (stat(path, &st) != 0) return ty_rf_errno(path);   /* ENOENT/ENOTDIR -> MISS */
    *mtime = (tycho_int)st.st_mtime;
#ifdef _WIN32
    {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
            unsigned long long t =
                ((unsigned long long)fad.ftLastWriteTime.dwHighDateTime << 32)
                | fad.ftLastWriteTime.dwLowDateTime;
            *mtime = (tycho_int)((long long)(t / 10000000ULL) - 11644473600LL);
        }
    }
#endif
    return TY_RF_OK;
}

/* SET the modification time of `path` to `mtime` epoch seconds -- the write side
 * of iox_stat_mtime, and the same status code space as it.
 *
 * ATIME IS LEFT ALONE. utimensat's UTIME_OMIT says "do not touch this one", which
 * is why it is used here over utime(2): utime's struct carries both times, so
 * restoring an mtime through it would silently stamp the access time too. Windows
 * has no utimensat, so there the atime is read back and passed through unchanged.
 *
 * A DIRECTORY IS TY_RF_OK, matching iox_stat_mtime and not iox_stat_size: a
 * directory has a modification time, it is the one iox_stat_mtime handed out, and
 * an extractor restoring a tree must be able to give it back. */
tycho_int iox_set_mtime(const char *path, tycho_int mtime) {
    errno = 0;
#ifdef _WIN32
    struct stat st;
    struct _utimbuf ub;
    if (stat(path, &st) != 0) return ty_rf_errno(path);
    if (S_ISDIR(st.st_mode)) {
        HANDLE h = CreateFileA(path, FILE_WRITE_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (h == INVALID_HANDLE_VALUE) return TY_RF_ERR;
        unsigned long long t = (unsigned long long)mtime * 10000000ULL + 116444736000000000ULL;
        FILETIME ft;
        ft.dwLowDateTime  = (DWORD)(t & 0xFFFFFFFFULL);
        ft.dwHighDateTime = (DWORD)(t >> 32);
        BOOL ok = SetFileTime(h, NULL, NULL, &ft);
        CloseHandle(h);
        return ok ? TY_RF_OK : TY_RF_ERR;
    }
    ub.actime = (time_t)st.st_atime;
    ub.modtime = (time_t)mtime;
    if (_utime(path, &ub) != 0) return ty_rf_errno(path);
#else
    struct timespec ts[2];
    ts[0].tv_sec = 0;             ts[0].tv_nsec = UTIME_OMIT;
    ts[1].tv_sec = (time_t)mtime; ts[1].tv_nsec = 0;
    if (utimensat(AT_FDCWD, path, ts, 0) != 0) return ty_rf_errno(path);
#endif
    return TY_RF_OK;
}

tycho_int iox_stat_size(const char *path, tycho_int *size) {
    struct stat st;
    *size = 0;
    errno = 0;
    if (stat(path, &st) != 0) return ty_rf_errno(path);   /* ENOENT/ENOTDIR -> MISS */
    if (S_ISDIR(st.st_mode)) return TY_RF_DIR;
    if (!S_ISREG(st.st_mode)) return TY_RF_ERR;       /* fifo/socket/device: no length */
    *size = (tycho_int)st.st_size;
    return TY_RF_OK;
}

/* One code the read/stat space has no use for: the path is occupied by something
 * that is NOT a directory, so make_dir cannot have what it asked for. TY_RF_OK
 * cannot carry it -- OK already means "I did it" below. */
#define TY_FS_EXISTS 4

tycho_int iox_make_dir(const char *path) {
    errno = 0;
    if (mkdir(path, 0777) == 0) return TY_RF_OK;
    if (errno == EEXIST)                                  /* who is in the way? */
        return iox_stat_kind(path) == TY_RF_DIR ? TY_RF_DIR : TY_FS_EXISTS;
    return ty_rf_errno(path);
}

tycho_int iox_remove(const char *path) {
    errno = 0;
#ifdef _WIN32
    /* Windows remove() is unlink()-only: a directory needs _rmdir. stat once
     * to know which -- then the errno mapping above handles the rest
     * (ENOTEMPTY stays a real error, ENOENT is the already-gone answer). */
    struct stat st;
    if (stat(path, &st) != 0) return ty_rf_errno(path);
    if (S_ISDIR(st.st_mode)) {
        if (_rmdir(path) == 0) return TY_RF_OK;
        return ty_rf_errno(path);
    }
    if (remove(path) == 0) return TY_RF_OK;
    return ty_rf_errno(path);
#else
    if (remove(path) == 0) return TY_RF_OK;
    return ty_rf_errno(path);
#endif
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
void iox_write_at(const char *path, tycho_int off,
                  const unsigned char *data, tycho_int datalen, tycho_int *status) {
    *status = TY_RF_ERR;
    if (off < 0 || datalen < 0) return;            /* fail closed, before open(2) */
    errno = 0;
    int fd = open(path, O_RDWR | O_CREAT | O_BINARY, 0644);   /* create if absent, like write_bytes */
    if (fd < 0) { *status = ty_rf_errno(path); return; }
    if (datalen > 0 && data) {
        ssize_t w = pwrite(fd, data, (size_t)datalen, (off_t)off);
        if (w != (ssize_t)datalen) { *status = ty_rf_errno(path); close(fd); return; }  /* partial: error, fail closed */
    }
    if (close(fd) != 0) { *status = ty_rf_errno(path); return; }
    *status = TY_RF_OK;
}

void iox_write_bytes(const char *path, const unsigned char *data,
                     tycho_int datalen, tycho_int *status) {
    *status = TY_RF_ERR;
    errno = 0;
    FILE *f = fopen(path, "wb");
    if (!f) { *status = ty_rf_errno(path); return; }       /* fail closed: nothing written */
    if (datalen > 0 && data && fwrite(data, 1, (size_t)datalen, f) != (size_t)datalen) {
        *status = ty_rf_errno(path);                        /* partial write: an error */
        fclose(f);
        return;
    }
    if (fclose(f) != 0) { *status = ty_rf_errno(path); return; }
    *status = TY_RF_OK;
}

/* One more code the read/stat space has no use for: the OBJECT IS REAL AND THE
 * PLATFORM CANNOT SYNC IT. It is deliberately not TY_RF_ERR, because the two
 * demand opposite moves from a caller -- see iox_sync below. */
#define TY_FS_UNSUP 5

/* fsync(2) `path`: return only once the kernel says the bytes AND the metadata
 * are on the storage device, not merely in the page cache.
 *
 *      TY_RF_OK     (1)  the device acknowledged the flush
 *      TY_RF_MISS   (0)  no such path (ENOENT/ENOTDIR)
 *      TY_FS_UNSUP  (5)  it is there and this platform/filesystem has no flush
 *                        for it -- see WHY THIS IS NOT AN ERROR CODE below
 *      TY_RF_ERR    (3)  it is there and the flush FAILED: EIO, ENOSPC on a
 *                        delayed-allocation filesystem, a dead device
 *
 * Every other writer here returns once close(2) succeeded, which reaches the
 * page cache and no further: enough to survive the WRITING PROCESS dying, never
 * enough to survive the machine dying, because the page cache is RAM.
 *
 * A DIRECTORY IS A LEGAL ARGUMENT AND IS TY_RF_OK (matching iox_stat_mtime, not
 * iox_stat_size) because it is the half of durability everyone forgets: a
 * file's CONTENTS and its NAME live in different places, and fsync of the file
 * flushes only the first. A freshly created file synced without its parent can
 * come back from a power cut as a file that does not exist, data intact and
 * unreachable. O_RDONLY is also the only mode a directory can be opened in.
 *
 * TY_FS_UNSUP IS NOT TY_RF_ERR, on iox_make_dir's TY_FS_EXISTS test: one failed
 * syscall, two opposite next moves. TY_RF_ERR means the kernel tried to push the
 * data and the device refused -- the write is LOST. TY_FS_UNSUP means the data
 * is exactly where every other core:io writer leaves it and the platform has no
 * stronger promise available, which a caller may degrade to and say so. And it
 * is never a silent TY_RF_OK: claiming a flush that did not happen would rebuild
 * the false durability claim this call exists to remove, one layer lower. */
tycho_int iox_sync(const char *path) {
    errno = 0;
#ifdef _WIN32
    /* No Windows API flushes a DIRECTORY ENTRY: FlushFileBuffers (which _commit
     * wraps) needs a handle opened for writing, and a directory cannot be opened
     * that way. Flushing NTFS's metadata means a volume handle and Administrator.
     * So the directory half is honestly unavailable here rather than quietly
     * skipped -- a caller creating a file on Windows learns that the NAME is not
     * covered instead of being told it is. */
    struct stat st;
    if (stat(path, &st) != 0) return ty_rf_errno(path);
    if (S_ISDIR(st.st_mode)) return TY_FS_UNSUP;
    /* O_WRONLY, unlike the POSIX branch: _commit -> FlushFileBuffers requires
     * GENERIC_WRITE on the handle and fails ERROR_ACCESS_DENIED without it. */
    int fd = open(path, O_WRONLY | O_BINARY);
    if (fd < 0) return ty_rf_errno(path);
    if (_commit(fd) != 0) { int e = errno; close(fd); errno = e; return TY_RF_ERR; }
    if (close(fd) != 0) return ty_rf_errno(path);
    return TY_RF_OK;
#else
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_BINARY);
    if (fd < 0) return ty_rf_errno(path);
    int r;
    do { r = fsync(fd); } while (r != 0 && errno == EINTR);   /* a signal is not a failure */
    if (r != 0) {
        /* EINVAL is what Linux returns for an fd whose object has no flush (a
         * fifo, a socket, some pseudo-filesystems); ENOTSUP/EOPNOTSUPP is the
         * same answer elsewhere. Everything else means the flush was attempted
         * and did not land, which is data loss. */
        tycho_int st = TY_RF_ERR;
        if (errno == EINVAL || errno == ENOTSUP
#if defined(EOPNOTSUPP) && EOPNOTSUPP != ENOTSUP
            || errno == EOPNOTSUPP
#endif
           ) st = TY_FS_UNSUP;
        close(fd);
        return st;
    }
    /* close(2) can report a deferred write error; a sync that succeeded and then
     * failed to close has not proven what it claims. */
    if (close(fd) != 0) return ty_rf_errno(path);
    return TY_RF_OK;
#endif
}
