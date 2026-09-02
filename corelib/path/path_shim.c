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
#include "../tycho.h"

#ifdef _WIN32
#include <windows.h>
#define TY_PATH_MAX MAX_PATH
#else
#include <limits.h>
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
