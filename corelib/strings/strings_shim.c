/* core:strings shim -- a LOCALE-INDEPENDENT string-to-double for
 * strings.parse_float (corelib/strings/strings.ty). Pure libc, no `deps` file, so
 * core:strings stays turnkey and core tier like core:time / core:os.
 *
 * WHY THIS FILE EXISTS AT ALL. There is no string-to-float conversion anywhere in
 * the language: `to_float` is a builtin that takes an int, a sized int, an f32 or
 * a float newtype (src/tychoc.c:5918-5922), never a string. So the conversion has
 * to cross the FFI boundary, and the only libc routine for it is strtod.
 *
 * WHY strtod ALONE IS A BUG. strtod reads the decimal separator from LC_NUMERIC.
 * Under a comma-decimal locale strtod("1.5") returns 1.0 and stops at the '.' --
 * no error, no signal, a silently wrong value. Measured on this host with
 * `cc -std=c11` and setlocale(LC_ALL, "") under LC_ALL=da_DK.UTF-8:
 *
 *     plain strtod 1.5 -> 1 rest=[.5]                  <- the trap
 *     strtod_l(C)  1.5 -> 1.5 rest=[]                  <- what this shim does
 *
 * Nothing in this tree calls setlocale (`grep -rn setlocale runtime/ src/
 * corelib/` is empty), so a Tycho program runs in the "C" locale and plain strtod
 * happens to be right today. That is an UNSTATED dependency on a global no gate
 * checks, and one linked C library calling setlocale would break every float this
 * corelib ever parses. This shim removes the dependency instead of documenting it.
 *
 * THE FEATURE-TEST MACRO, AND WHY IT IS _GNU_SOURCE AND NOT _POSIX_C_SOURCE.
 * newlocale/locale_t/LC_NUMERIC_MASK are POSIX 2008, but strtod_l is NOT in POSIX
 * -- glibc guards it behind __USE_GNU. Measured: with _POSIX_C_SOURCE 200809L and
 * -std=c11 this file fails with `implicit declaration of function 'strtod_l'`.
 * `make shim-check` is the only gate that can catch that (scripts/shim_check.sh's
 * header: the real build passes no -std, so the implicit _DEFAULT_SOURCE hides it).
 *
 * FALLBACK, AND WHY IT IS STILL CORRECT. Two things can go wrong: a libc without
 * strtod_l (TY_HAVE_STRTOD_L undefined), or newlocale failing at run time (out of
 * memory / no "C" locale). Either way we fall back to plain strtod on a copy of
 * the input in which every '.' has been rewritten to the RUNNING locale's own
 * decimal separator, taken from localeconv() (C89, always present). That converts
 * with the same digits and the same rounding, so the fallback is correct under any
 * locale -- it is slower and it allocates, it is not less right.
 *
 * FAIL CLOSED. Every failure -- a syntax refusal, a range error, a failed malloc
 * -- returns 0.0 with a NON-OK status. The Tycho caller never sees a value it can
 * mistake for a successful parse, which is the whole point of parse_float being
 * the strict opposite of parse_int.
 *
 * THE CALLER VALIDATES FIRST. strings.parse_float checks the WHOLE string against
 * a written-down grammar in Tycho before calling here, so this shim never has to
 * defend against strtod's own leniency ("inf", "nan", "0x1p3", leading
 * whitespace) and never sees a Tycho string whose bytes past an embedded NUL
 * would be invisible to C. The `whole` check below is belt and braces, not the
 * primary defence.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE          /* strtod_l: a GNU/musl extension, NOT POSIX 2008 */
#endif
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
#ifndef TYCHO_INT_T
#define TYCHO_INT_T
typedef int64_t tycho_int;
#endif

/* Status codes. Mirrored by the PF_* consts in corelib/strings/strings.ty --
 * change one, change the other. */
#define TY_PF_OK        0
#define TY_PF_OVERFLOW  1    /* ERANGE and the result is infinite */
#define TY_PF_UNDERFLOW 2    /* ERANGE and the result is finite: 0 or a subnormal */
#define TY_PF_SYNTAX    3    /* no conversion, or trailing bytes left over */

/* The "C" LC_NUMERIC handle, built ONCE. pthread_once and not a plain lazy
 * assignment because a Tycho task is a pthread (runtime/tycho_rt.c spawn/wait),
 * so two tasks can reach parse_float at the same time; a bare `if (!h) h = ...`
 * is a data race that also leaks a locale handle.
 *
 * Windows (mingw-w64) has no POSIX locale API at any version, and the
 * TY_HAVE_STRTOD_L list above already excludes it -- so on Windows the whole
 * handle stays 0 and strx_parse_double takes the localeconv fallback, which
 * is correct under any locale (its header argues why). */
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
        /* ERANGE covers both ends. isinf splits them: overflow saturates to
         * +/-HUGE_VAL, underflow lands on a finite 0 or subnormal. */
        *status = isinf(v) ? TY_PF_OVERFLOW : TY_PF_UNDERFLOW;
        return 0.0;
    }
    *status = TY_PF_OK;
    return v;
}

/* ---- TEST HOOK -------------------------------------------------------------
 * Deliberately NOT declared in corelib/strings/strings.ty: it is not part of
 * core:strings' API and no library caller should ever change a process-wide
 * global. corelib/test/strings/main.ty declares this extern itself, which the
 * linker resolves against this file because tychoc auto-discovers the imported
 * package's shim.
 *
 * It makes LC_NUMERIC hostile -- a locale whose decimal separator is not '.' --
 * so the parse_float assertions in that test run against the exact condition
 * that breaks plain strtod, rather than against the "C" locale a Tycho program
 * would otherwise never leave. Tries the environment first (so
 * `LC_ALL=da_DK.UTF-8 ./test` is honoured), then named comma-decimal locales.
 *
 * Returns 1 if LC_NUMERIC's separator is now something other than '.', 0 if the
 * host has none of these locales installed. The test PRINTS that answer into its
 * golden, so a host where it is 0 fails loudly with a line naming the reason
 * instead of silently testing nothing under "C".
 */
tycho_int strx_test_make_locale_hostile(void) {
    /* The Windows CRT rejects every POSIX name here (setlocale returns NULL),
     * so without the Windows-style names this returned 0 on Windows and the
     * test above failed loudly -- by its own design, rather than quietly
     * testing "C". Mirrors the same list in runtime/tycho_rt.c. */
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
