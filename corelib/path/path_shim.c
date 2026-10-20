/* core:path shim -- the ONE filesystem call this otherwise-lexical package makes.
 *
 * `clean` and `safe_join` are pure string math and must stay that way: they are
 * the cheap first fence and they answer the same on every host. But a lexical
 * fence cannot see a SYMLINK, so a link under a document root pointing outside
 * it passes every string test there is. pathx_real is realpath(3): it resolves
 * every symlink and "..", so the caller can compare the ANSWER against its root
 * instead of comparing the request against it. Empty string on any failure
 * (nonexistent, unreadable, too long, NUL in the path) -- fail closed.
 *
 * Windows has no realpath; GetFullPathNameA normalises and makes absolute but
 * does NOT resolve reparse points, so the containment test there is lexical
 * over a normalised path. Stated rather than hidden: the POSIX target is the
 * one this repo gates.
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE          /* glibc: expose realpath(3) under -std=c11 */
#endif
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "../tycho.h"

#ifdef _WIN32
#include <windows.h>
#define TY_PATH_MAX MAX_PATH
#else
#include <limits.h>
#include <sys/stat.h>
#ifndef PATH_MAX
#define TY_PATH_MAX 4096
#else
#define TY_PATH_MAX PATH_MAX
#endif
#endif

const char *pathx_real(const char *p) {
    static __thread char buf[TY_PATH_MAX + 1];
    buf[0] = '\0';
    if (!p || !*p) return buf;
    if (strlen(p) > TY_PATH_MAX) return buf;      /* cannot fit an answer: refuse */
#ifdef _WIN32
    DWORD n = GetFullPathNameA(p, (DWORD)sizeof buf, buf, NULL);
    if (n == 0 || n >= sizeof buf) buf[0] = '\0';
    if (GetFileAttributesA(buf) == INVALID_FILE_ATTRIBUTES) buf[0] = '\0';
#else
    char tmp[TY_PATH_MAX + 1];
    if (!realpath(p, tmp)) return buf;            /* buf is already empty */
    if (strlen(tmp) > TY_PATH_MAX) return buf;
    memcpy(buf, tmp, strlen(tmp) + 1);
#endif
    return buf;
}

/* The error channel path.real lacks. Status codes are mirrored in path.ty:
 * 0 Ok, 1 NotFound, 2 Denied, 3 TooLong, 4 BadPath, 5 Failed. The string is
 * empty on every non-Ok, so a caller that ignores the status still fails closed. */
#define TY_PE_OK       0
#define TY_PE_NOTFOUND 1
#define TY_PE_DENIED   2
#define TY_PE_TOOLONG  3
#define TY_PE_BADPATH  4
#define TY_PE_FAILED   5

const char *pathx_real_checked(const char *p, tycho_int *status) {
    static __thread char buf[TY_PATH_MAX + 1];
    buf[0] = '\0';
    *status = TY_PE_OK;
    if (!p || !*p) { *status = TY_PE_BADPATH; return buf; }
    if (strlen(p) > TY_PATH_MAX) { *status = TY_PE_TOOLONG; return buf; }
#ifdef _WIN32
    DWORD n = GetFullPathNameA(p, (DWORD)sizeof buf, buf, NULL);
    if (n == 0 || n >= sizeof buf) { *status = TY_PE_FAILED; buf[0] = '\0'; return buf; }
    if (GetFileAttributesA(buf) == INVALID_FILE_ATTRIBUTES) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) *status = TY_PE_NOTFOUND;
        else if (e == ERROR_ACCESS_DENIED) *status = TY_PE_DENIED;
        else if (e == ERROR_FILENAME_EXCED_RANGE) *status = TY_PE_TOOLONG;
        else *status = TY_PE_FAILED;
        buf[0] = '\0';
    }
#else
    char tmp[TY_PATH_MAX + 1];
    errno = 0;
    if (!realpath(p, tmp)) {
        if (errno == ENOENT || errno == ENOTDIR) *status = TY_PE_NOTFOUND;
        else if (errno == EACCES || errno == EPERM) *status = TY_PE_DENIED;
        else if (errno == ENAMETOOLONG) *status = TY_PE_TOOLONG;
        else if (errno == EINVAL) *status = TY_PE_BADPATH;
        else *status = TY_PE_FAILED;
        return buf;                                /* buf is already empty */
    }
    if (strlen(tmp) > TY_PATH_MAX) { *status = TY_PE_TOOLONG; return buf; }
    memcpy(buf, tmp, strlen(tmp) + 1);
#endif
    return buf;
}

/* A dangling symlink makes realpath fail just like a missing ordinary leaf.
 * resolve_under walks existing ancestors to preserve 404 semantics, so expose
 * lstat/GetFileAttributes separately and refuse that symlink instead of treating
 * it as a missing path beneath its parent. */
int pathx_is_symlink(const char *p) {
    if (!p || !*p || strlen(p) > TY_PATH_MAX) return 0;
#ifdef _WIN32
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    struct stat st;
    return lstat(p, &st) == 0 && S_ISLNK(st.st_mode);
#endif
}
