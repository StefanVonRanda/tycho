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
