/* du.c -- the C side of tycho-du.
 *
 * One foreign resource: an open POSIX directory stream (DIR *), handed to Tycho
 * as a typed `handle Dir` whose destructor is dw_close. Everything else is a
 * scalar or a string, so nothing else needs owning.
 */
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

/* Bookkeeping that makes the affine claim testable rather than asserted: a
 * destructor that did not run shows up as g_live climbing. Single-threaded by
 * construction -- the FFI's race-freedom stops at this boundary (spec 26). */
static int64_t g_live = 0;    /* open streams right now: 0 balanced, >0 leak */
static int64_t g_opens = 0;   /* successful opendirs, ever */
static int64_t g_closes = 0;  /* dw_close calls, ever, null-guarded ones included */

/* The opener. NULL on failure; Tycho's scope-exit free is null-guarded. */
void *dw_open(const char *path) {
    DIR *d = opendir(path);
    if (d) { g_live++; g_opens++; }
    return (void *)d;
}

/* The destructor named by `free:` in the handle declaration.
 *
 * A NULL DOES NOT ARRIVE. The null guard is on the TYCHO side -- the compiler
 * emits `if (h_d) dw_close(h_d);` and nulls the variable on an early close -- so
 * a failed open gives opens=0 closes=0, not a close of NULL. tools/tycho-fh/fh.c
 * carried the opposite claim in a comment for a month and the 2026-09-21 probe
 * believed it over the one sentence of documentation that was right
 * (FRICTION 125). The `if (!d)` below is belt and braces, not the contract. */
int64_t dw_close(void *d) {
    g_closes++;
    if (!d) return 0;
    g_live--;
    return (int64_t)closedir((DIR *)d);
}

int64_t dw_live(void)   { return g_live; }
int64_t dw_opens(void)  { return g_opens; }
int64_t dw_closes(void) { return g_closes; }

/* Next entry name, skipping "." and ".."; NULL when the stream is exhausted.
 *
 * d_name lives inside the DIR's own buffer and stays valid until the next
 * readdir/closedir on this stream. Tycho copies a returned string into the
 * caller's arena immediately after this returns, and the stream is still open
 * at that moment, so the copy is sound. (docs/reference/ffi.md warns about the
 * closedir-then-return-d_name shape; this is deliberately not it.) */
const char *dw_next(void *d) {
    struct dirent *e;
    if (!d) return NULL;
    for (;;) {
        errno = 0;
        e = readdir((DIR *)d);
        if (!e) return NULL;
        if (e->d_name[0] == '.' &&
            (e->d_name[1] == '\0' || (e->d_name[1] == '.' && e->d_name[2] == '\0')))
            continue;
        return e->d_name;
    }
}

/* spec 24.1.1, the mirror arrangement: the classification is the return value
 * and the payload rides an `inout` out-param, because a size can take any value
 * a status code could. Codes: 0 error, 1 regular file, 2 directory, 3 other.
 * The payload is written on every path, first, so an early return fails closed. */
int64_t dw_stat(const char *path, int64_t *size) {
    struct stat sb;
    *size = 0;
    if (!path) { errno = EINVAL; return 0; }
    if (lstat(path, &sb) != 0) return 0;
    if (S_ISREG(sb.st_mode)) { *size = (int64_t)sb.st_size; return 1; }
    if (S_ISDIR(sb.st_mode)) return 2;
    return 3;
}

/* The reason for the last failure. A __thread static, not malloc: the FFI copies
 * a returned string but never frees it, so a malloc'd return would leak. */
const char *dw_errstr(void) {
    static __thread char buf[256];
    const char *m = strerror(errno);
    buf[0] = '\0';
    if (m) { strncpy(buf, m, sizeof buf - 1); buf[sizeof buf - 1] = '\0'; }
    return buf;
}
