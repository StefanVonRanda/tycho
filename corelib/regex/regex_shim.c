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
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../tycho.h"

/* Every subject crosses as (s, n) -- n is Tycho's len(), an O(1) header read --
 * because a Tycho string may hold an interior NUL and a bare `char*` ends at it.
 * REG_STARTEND (BSD, glibc, pcre2-posix) bounds the subject by length, so the
 * NUL is matched as an ordinary byte. It is NOT base POSIX: where it is absent a
 * NUL-bearing subject DIES here rather than being matched up to the NUL and
 * reported as "no match" -- a wrong answer to a validation question is worse
 * than a loud one. NUL-free subjects behave identically either way.
 * Returns 0 on a match, non-zero otherwise (regexec's own convention). */
static int rx_exec(void *re, const char *s, tycho_int n, size_t nm, regmatch_t *m) {
    if (!re || n < 0) return -1;
#ifdef REG_STARTEND
    m[0].rm_so = 0;
    m[0].rm_eo = (regoff_t)n;
    return regexec((regex_t *)re, s, nm, m, REG_STARTEND);
#else
    if (memchr(s, '\0', (size_t)n)) {
        fprintf(stderr, "tycho: core:regex: subject holds an interior NUL and this "
                        "platform's <regex.h> has no REG_STARTEND; refusing to match a "
                        "truncated subject\n");
        exit(1);
    }
    return regexec((regex_t *)re, s, nm, m, 0);
#endif
}

#define RX_MAX_REPEAT_PRODUCT 100000
static int rx_repeat_too_big(const char *p, size_t n) {
    unsigned long long product = 1;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == '\\') { i++; continue; }          /* an escaped brace is literal */
        if (p[i] != '{') continue;
        size_t j = i + 1;
        unsigned long long hi = 0;
        int digits = 0;
        while (j < n && p[j] >= '0' && p[j] <= '9') {  /* the lower bound */
            hi = hi * 10 + (unsigned)(p[j] - '0'); j++; digits++;
            if (hi > RX_MAX_REPEAT_PRODUCT) return 1;
        }
        if (j < n && p[j] == ',') {                    /* {n,m}: m is the max */
            j++; hi = 0; digits = 0;
            while (j < n && p[j] >= '0' && p[j] <= '9') {
                hi = hi * 10 + (unsigned)(p[j] - '0'); j++; digits++;
                if (hi > RX_MAX_REPEAT_PRODUCT) return 1;
            }
            if (digits == 0) return 1;                 /* {n,} is unbounded */
        }
        if (digits == 0) continue;                     /* not a repetition */
        product *= (hi ? hi : 1);
        if (product > RX_MAX_REPEAT_PRODUCT) return 1;
        i = j;
    }
    return 0;
}

/* POSIX ERE has NO backreferences. glibc's regcomp accepts \1..\9 anyway, as an
 * extension, and matching one is a backtracking search the {n,m} guard above
 * cannot see: `^(.*)*\1\1\1$` over 240 bytes ran past 30 seconds with no
 * repetition count anywhere in the pattern. Refusing to compile keeps the
 * documented dialect (ERE) and the bound at the same place.
 * A backslash is NOT special inside a bracket expression, so those are skipped
 * whole -- `[\1]` is the two literal characters and stays legal. */
static int rx_has_backref(const char *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (p[i] == '[') {
            size_t j = i + 1;
            if (j < n && p[j] == '^') j++;
            if (j < n && p[j] == ']') j++;         /* a leading ] is literal */
            while (j < n && p[j] != ']') {
                if (p[j] == '[' && j + 1 < n &&
                    (p[j + 1] == ':' || p[j + 1] == '.' || p[j + 1] == '=')) {
                    char k = p[j + 1];             /* [:class:] [.coll.] [=eq=] */
                    j += 2;
                    while (j + 1 < n && !(p[j] == k && p[j + 1] == ']')) j++;
                    j++;
                }
                j++;
            }
            i = j;                                 /* the closing ], or past the end */
            continue;
        }
        if (p[i] == '\\') {
            if (i + 1 < n && p[i + 1] >= '1' && p[i + 1] <= '9') return 1;
            i++;                                   /* an ordinary escape */
        }
    }
    return 0;
}

void *rx_compile(const char *pattern, tycho_int n) {
    if (n < 0 || memchr(pattern, '\0', (size_t)n)) return NULL;
    if (rx_repeat_too_big(pattern, (size_t)n)) return NULL;
    if (rx_has_backref(pattern, (size_t)n)) return NULL;
    regex_t *re = (regex_t *)malloc(sizeof(regex_t));
    if (!re) return NULL;
    if (regcomp(re, pattern, REG_EXTENDED) != 0) { free(re); return NULL; }
    return re;
}

tycho_int rx_is_match(void *re, const char *s, tycho_int n) {  /* 1 if re matches anywhere in s */
    regmatch_t m[1];
    return rx_exec(re, s, n, 1, m) == 0 ? 1 : 0;
}

tycho_int rx_find(void *re, const char *s, tycho_int n) {      /* first match start offset, or -1 */
    regmatch_t m[1];
    if (rx_exec(re, s, n, 1, m) != 0) return -1;
    return (tycho_int)m[0].rm_so;
}

tycho_int rx_find_end(void *re, const char *s, tycho_int n) {  /* first match end offset, or -1 */
    regmatch_t m[1];
    if (rx_exec(re, s, n, 1, m) != 0) return -1;
    return (tycho_int)m[0].rm_eo;
}

tycho_int rx_ngroups(void *re) {                     /* # of capturing groups (parenthesized subexprs) */
    return re ? (tycho_int)((regex_t *)re)->re_nsub : 0;
}

/* Offsets of capture group n (0 = the whole match) in the FIRST match. A
 * non-participating group and a non-match both yield -1 (rm_so == -1). Stateless
 * like rx_find: one regexec per call, pmatch sized to n+1. */
static tycho_int rx_group(void *re, const char *s, tycho_int slen, tycho_int n, int want_end) {
    if (!re || n < 0) return -1;
    size_t nm = (size_t)n + 1;
    regmatch_t *m = (regmatch_t *)malloc(nm * sizeof(regmatch_t));
    if (!m) return -1;                          /* fail closed on OOM */
    tycho_int r = -1;
    if (rx_exec(re, s, slen, nm, m) == 0)
        r = want_end ? (tycho_int)m[n].rm_eo : (tycho_int)m[n].rm_so;
    free(m);
    return r;
}

tycho_int rx_group_start(void *re, const char *s, tycho_int slen, tycho_int n) { return rx_group(re, s, slen, n, 0); }
tycho_int rx_group_end  (void *re, const char *s, tycho_int slen, tycho_int n) { return rx_group(re, s, slen, n, 1); }

void rx_free(void *re) {                        /* free a compiled pattern */
    if (re) { regfree((regex_t *)re); free(re); }
}
