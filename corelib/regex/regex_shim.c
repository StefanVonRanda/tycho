/* core:regex C shim -- POSIX extended regular expressions over <regex.h> (libc,
 * no extra -l). Auto-compiled+linked when a program imports "core:regex" (the
 * compiler discovers <pkg>/<pkg>_shim.c). A compiled pattern is a malloc'd
 * regex_t* handed to tycho as an opaque `ptr`; tycho never dereferences it and
 * frees it via rx_free (FFI memory is NOT arena-managed). */
#ifdef _WIN32
/* mingw-w64 has no POSIX <regex.h> at any version. pcre2-posix (the PCRE2
 * project's POSIX compatibility layer) provides the SAME API -- regex_t,
 * regcomp/regexec/regerror/regfree with POSIX signatures -- so this shim
 * compiles unchanged against it; the link flag -lpcre2-posix comes from the
 * package's `deps` file `_WIN32:` section. Where pcre2 is not installed the
 * test skips (the corelib skip convention); where it is, the semantics are
 * PCRE2's for patterns both engines accept, which is the documented
 * Windows divergence from glibc's regexec. */
#include <pcre2posix.h>
#else
#include <regex.h>
#endif
#include <stdlib.h>
#include <stdint.h>
/* int64-migration (Phase 3): Tycho `int` lowers to tycho_int (int64_t) in the
 * emitted program; this shim is a separate translation unit, so it defines the
 * same type to match the FFI ABI on ILP32/LLP64, not just LP64. */
#ifndef TYCHO_INT_T
#define TYCHO_INT_T
typedef int64_t tycho_int;
#endif

void *rx_compile(const char *pattern) {        /* compile ERE; NULL on a bad pattern */
    regex_t *re = (regex_t *)malloc(sizeof(regex_t));
    if (!re) return NULL;
    if (regcomp(re, pattern, REG_EXTENDED) != 0) { free(re); return NULL; }
    return re;
}

tycho_int rx_is_match(void *re, const char *s) {    /* 1 if re matches anywhere in s, else 0 */
    if (!re) return 0;
    return regexec((regex_t *)re, s, 0, NULL, 0) == 0 ? 1 : 0;
}

tycho_int rx_find(void *re, const char *s) {        /* byte offset of the first match, or -1 */
    regmatch_t m;
    if (!re || regexec((regex_t *)re, s, 1, &m, 0) != 0) return -1;
    return (tycho_int)m.rm_so;
}

tycho_int rx_find_end(void *re, const char *s) {    /* one-past-end offset of the first match, or -1 */
    regmatch_t m;
    if (!re || regexec((regex_t *)re, s, 1, &m, 0) != 0) return -1;
    return (tycho_int)m.rm_eo;
}

tycho_int rx_ngroups(void *re) {                     /* # of capturing groups (parenthesized subexprs) */
    return re ? (tycho_int)((regex_t *)re)->re_nsub : 0;
}

/* Offsets of capture group n (0 = the whole match) in the FIRST match. A
 * non-participating group and a non-match both yield -1 (rm_so == -1). Stateless
 * like rx_find: one regexec per call, pmatch sized to n+1. */
static tycho_int rx_group(void *re, const char *s, tycho_int n, int want_end) {
    if (!re || n < 0) return -1;
    size_t nm = (size_t)n + 1;
    regmatch_t *m = (regmatch_t *)malloc(nm * sizeof(regmatch_t));
    if (!m) return -1;                          /* fail closed on OOM */
    tycho_int r = -1;
    if (regexec((regex_t *)re, s, nm, m, 0) == 0)
        r = want_end ? (tycho_int)m[n].rm_eo : (tycho_int)m[n].rm_so;
    free(m);
    return r;
}

tycho_int rx_group_start(void *re, const char *s, tycho_int n) { return rx_group(re, s, n, 0); }
tycho_int rx_group_end  (void *re, const char *s, tycho_int n) { return rx_group(re, s, n, 1); }

void rx_free(void *re) {                        /* free a compiled pattern */
    if (re) { regfree((regex_t *)re); free(re); }
}
