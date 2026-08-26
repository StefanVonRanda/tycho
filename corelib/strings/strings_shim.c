#ifndef _GNU_SOURCE
#define _GNU_SOURCE          /* strtod_l: a GNU/musl extension, NOT POSIX 2008 */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <xlocale.h>         /* the BSDs put strtod_l / newlocale here */
#endif

/* Which libcs are known to declare strtod_l. Guessing WRONG is safe in one
 * direction only, and this is that direction: an unlisted libc takes the
 * localeconv fallback, which is correct everywhere. musl is deliberately not
 * listed -- it has no reliable identifying macro, so it gets the fallback. */
#if defined(__GLIBC__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define TY_HAVE_STRTOD_L 1
#endif

/* int64-migration: Tycho `int` lowers to tycho_int (int64_t) in the emitted
 * program; this shim is a separate translation unit, so it defines the same type
 * to match the FFI ABI on ILP32/LLP64, not just LP64. */
#include "../tycho.h"

#define TY_PF_OK        0
#define TY_PF_OVERFLOW  1    /* ERANGE and the result is infinite */
#define TY_PF_UNDERFLOW 2    /* ERANGE and the result is 0 -- TOTAL underflow.
                              * A subnormal is ERANGE too and is NOT this. */
#define TY_PF_SYNTAX    3    /* no conversion, or trailing bytes left over */

#ifdef TY_HAVE_STRTOD_L
static locale_t       ty_c_numeric;
static pthread_once_t ty_c_numeric_once = PTHREAD_ONCE_INIT;

static void ty_c_numeric_init(void) {
    ty_c_numeric = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);   /* 0 on failure */
}
#endif

/* Fallback: plain strtod over a copy whose '.' has become the running locale's
 * decimal separator. Sets *whole (did it consume every byte) and *range (ERANGE).
 * errno is read HERE, before free(), because free() may clobber it. */
static double ty_strtod_localeconv(const char *s, int *whole, int *range) {
    struct lconv *lc = localeconv();
    const char *dp  = (lc && lc->decimal_point && lc->decimal_point[0]) ? lc->decimal_point : ".";
    size_t dplen    = strlen(dp);
    char  *end      = NULL;
    double v;

    *whole = 0;
    *range = 0;
    if (dplen == 1 && dp[0] == '.') {                 /* already C-like: no copy */
        errno = 0;
        v = strtod(s, &end);
        *range = (errno == ERANGE);
        *whole = (end != s && *end == '\0');
        return v;
    }
    size_t n = strlen(s);
    char *buf = (char *)malloc(n * dplen + 1);        /* every byte could be a '.' */
    if (!buf) return 0.0;                             /* fail closed: whole stays 0 -> SYNTAX */
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '.') { memcpy(buf + j, dp, dplen); j += dplen; }
        else             { buf[j++] = s[i]; }
    }
    buf[j] = '\0';
    errno = 0;
    v = strtod(buf, &end);
    *range = (errno == ERANGE);
    *whole = (end != buf && *end == '\0');            /* the rewrite is 1:1, so this
                                                       * is also "consumed all of s" */
    free(buf);
    return v;
}

/* The one entry point. Returns the value and writes a TY_PF_* code to *status.
 * On any non-OK status the returned double is 0.0 and must not be used. */
double strx_parse_double(const char *s, tycho_int *status) {
    int    whole = 0, range = 0;
    double v     = 0.0;

    *status = TY_PF_SYNTAX;
    if (!s) return 0.0;

#ifdef TY_HAVE_STRTOD_L
    pthread_once(&ty_c_numeric_once, ty_c_numeric_init);
    if (ty_c_numeric) {
        char *end = NULL;
        errno = 0;
        v     = strtod_l(s, &end, ty_c_numeric);
        range = (errno == ERANGE);
        whole = (end != s && *end == '\0');
    } else
#endif
    {
        v = ty_strtod_localeconv(s, &whole, &range);
    }

    if (!whole) { *status = TY_PF_SYNTAX; return 0.0; }
    if (range) {
        if (isinf(v)) { *status = TY_PF_OVERFLOW;  return 0.0; }
        if (v == 0.0) { *status = TY_PF_UNDERFLOW; return 0.0; }   /* -0.0 too: compares equal */
    }
    *status = TY_PF_OK;
    return v;
}

/* Rewrite the running locale's decimal separator (which may be several bytes)
 * to a single '.' in place; returns the new length. Fallback leg only -- %g
 * emits at most one separator, so one rewrite is enough. Twin of
 * runtime/tycho_rt.c@ty_fix_decimal_point. */
static int ty_fix_decimal_point(char *s, int n) {
    struct lconv *lc = localeconv();
    const char *dp   = (lc && lc->decimal_point && lc->decimal_point[0]) ? lc->decimal_point : ".";
    size_t dplen     = strlen(dp);
    if (dplen == 1 && dp[0] == '.') return n;        /* already C-like */
    char *at = strstr(s, dp);
    if (!at) return n;                               /* integral, or inf/nan */
    *at = '.';
    memmove(at + 1, at + dplen, (size_t)((s + n) - (at + dplen)) + 1);   /* +1 carries the NUL */
    return n - (int)dplen + 1;
}

/* WRITE, the mirror of strx_parse_double above and the twin of
 * src/tychoc.c@c_dtoa: v as C-locale `%.17g`. The buffer is thread-local and
 * the next call overwrites it, which is safe because tychoc copies an extern's
 * string return (`src/tychoc.c@tycho_str_copy`). */
const char *strx_format_g17(double v) {
    static _Thread_local char b[64];                 /* %.17g of a double is <= 24 bytes */
    int m;

#ifdef TY_HAVE_STRTOD_L
    pthread_once(&ty_c_numeric_once, ty_c_numeric_init);
    if (ty_c_numeric) {
        locale_t prev = uselocale(ty_c_numeric);
        snprintf(b, sizeof b, "%.17g", v);
        uselocale(prev);
        return b;
    }
#endif
    m = snprintf(b, sizeof b, "%.17g", v);
    if (m > 0 && (size_t)m < sizeof b) ty_fix_decimal_point(b, m);
    return b;
}

tycho_int strx_test_make_locale_hostile(void) {
    static const char *cands[] = { "", "da_DK.UTF-8", "da_DK.utf8", "da_DK",
                                   "de_DE.UTF-8", "fr_FR.UTF-8",
                                   "Danish_Denmark.1252",
                                   "German_Germany.1252",
                                   "French_France.1252", NULL };
    for (int i = 0; cands[i]; i++) {
        if (!setlocale(LC_NUMERIC, cands[i])) continue;
        struct lconv *lc = localeconv();
        if (lc && lc->decimal_point && strcmp(lc->decimal_point, ".") != 0) return 1;
    }
    setlocale(LC_NUMERIC, "C");     /* none of them was hostile: leave a known state */
    return 0;
}
