#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#endif

long tycho_run(char *cmd) {
#ifdef _WIN32
    char *copy = strdup(cmd);
    if (copy) {
        for (char *q = copy; *q && *q != ' ' && *q != '\t'; q++)
            if (*q == '/') *q = '\\';
        long r = (long)system(copy);
        free(copy);
        return r;
    }
#endif
    return (long)system(cmd);
}

/* Sleep for `ms` milliseconds (for `tycho watch`'s poll loop). */
long tycho_sleep_ms(long ms) {
    if (ms > 0) usleep((useconds_t)(ms * 1000));
    return 0;
}

long tycho_is_windows(void) {
#ifdef _WIN32
    return 1;
#else
    return 0;
#endif
}

/* Delete one file. 0 on success, -1 if it could not be removed (including
 * "was not there", which every caller here treats as success anyway -- they
 * are all cleanup paths that used `rm -f`). */
long tycho_remove(char *path) {
#ifdef _WIN32
    for (int tries = 0; tries < 20; tries++) {
        if (remove(path) == 0) return 0;
        if (errno != EACCES) return -1;
        Sleep(10);
    }
    return -1;
#else
    return remove(path) == 0 ? 0 : -1;
#endif
}

/* Copy src's bytes over dst, truncating dst. 0 on success, -1 on any read or
 * write failure -- including a short write, so a full disk fails the caller
 * instead of leaving a half copy that compares clean. */
long tycho_copy(char *src, char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[8192];
    size_t n;
    long rc = 0;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { rc = -1; break; }
    if (ferror(in)) rc = -1;
    if (fclose(out) != 0) rc = -1;     /* the flush can fail after every fwrite passed */
    fclose(in);
    return rc;
}

/* Replace dst with src in one operation. 0 on success, -1 on failure.
 *
 * This is `tycho fmt -w`'s install step, and the atomicity is the safety
 * property: either the original file or the fully formatted one, never a
 * partial write. POSIX rename() replaces an existing destination; MSVCRT's
 * rename() FAILS with EEXIST instead, so Windows goes through MoveFileEx with
 * MOVEFILE_REPLACE_EXISTING to get the POSIX behaviour. */
long tycho_rename(char *src, char *dst) {
#ifdef _WIN32
    return MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
#else
    return rename(src, dst) == 0 ? 0 : -1;
#endif
}

/* Compare two files byte for byte: 1 identical, 0 different, -1 if either
 * could not be read to the end.
 *
 * The -1 is load-bearing. This replaced `diff -q`, whose non-zero exit the
 * caller read as "not proven identical" and refused the in-place write on --
 * so an unreadable file must NOT be able to look equal. Reading both into
 * strings and comparing those would fail open: a failed read yields "" on both
 * sides, which compares equal.
 *
 * Regular files only: a short fread means EOF here, so na != nb ends the loop
 * as a difference. */
long tycho_files_equal(char *a, char *b) {
    FILE *fa = fopen(a, "rb");
    if (!fa) return -1;
    FILE *fb = fopen(b, "rb");
    if (!fb) { fclose(fa); return -1; }
    char ba[8192], bb[8192];
    long r = 1;
    for (;;) {
        size_t na = fread(ba, 1, sizeof ba, fa);
        size_t nb = fread(bb, 1, sizeof bb, fb);
        if (na != nb) { r = 0; break; }
        if (na == 0) break;
        if (memcmp(ba, bb, na) != 0) { r = 0; break; }
    }
    if (r == 1 && (ferror(fa) || ferror(fb))) r = -1;
    fclose(fa);
    fclose(fb);
    return r;
}
