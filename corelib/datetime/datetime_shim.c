/* core:datetime shim -- system/zone UTC offsets via libc <time.h>. Pure libc (no
 * external dependency, no `deps` file), so it stays turnkey like core:os/core:net.
 * The rest of core:datetime is pure integer math; these two functions are the
 * only FFI, and they exist to answer one question libc already knows how to
 * answer correctly: what is the UTC offset (including DST) at a given instant?
 *
 * Both return the offset in SECONDS east of UTC (negative = west), which the
 * Tycho side feeds straight into the pure from_unix_at / format_iso_tz layer.
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE          /* glibc: struct tm .tm_gmtoff + setenv/unsetenv */
#endif
#include <time.h>
#include <stdlib.h>    /* malloc/free -- the _WIN32 env shims below */
#include <string.h>    /* strlen/memcpy -- the same */
#include <ctype.h>     /* isalpha/isdigit -- the Windows POSIX-TZ parser */
#ifdef _WIN32
/* mingw has no localtime_r; localtime_s is the same call with swapped args and
 * a returned errno. The POSIX wrapper's contract -- NULL on failure -- holds. */
static struct tm *ty_localtime_r(const time_t *t, struct tm *out) {
    return localtime_s(out, t) == 0 ? out : NULL;
}
static struct tm *ty_gmtime_r(const time_t *t, struct tm *out) {
    return gmtime_s(out, t) == 0 ? out : NULL;
}
#define localtime_r(t, o) ty_localtime_r((t), (o))
#define gmtime_r(t, o)    ty_gmtime_r((t), (o))
/* setenv/unsetenv are POSIX; mingw has _putenv_s. The shim only ever calls
 * setenv with overwrite=1, so the flag is ignored. unsetenv maps to an empty
 * value (Windows has no removal semantics worth the difference here). */
static int ty_setenv(const char *name, const char *val, int ov) {
    (void)ov;
    size_t n = strlen(name), v = strlen(val);
    char *e = malloc(n + v + 2);
    if (!e) return -1;
    memcpy(e, name, n); e[n] = '=';
    memcpy(e + n + 1, val, v); e[n + v + 1] = '\0';
    int r = _putenv(e);        /* mingw's _putenv duplicates the string */
    free(e);
    return r;
}
static int ty_unsetenv(const char *name) {
    size_t n = strlen(name);
    char *e = malloc(n + 2);
    if (!e) return -1;
    memcpy(e, name, n); e[n] = '='; e[n + 1] = '\0';
    int r = _putenv(e);
    free(e);
    return r;
}
#define setenv(n, v, o) ty_setenv((n), (v), (o))
#define unsetenv(n)     ty_unsetenv((n))
/* glibc's struct tm has tm_gmtoff; mingw's does not. The offset is the
 * difference between a local and a UTC interpretation of the same instant --
 * mktime() normalises both, so the subtraction is the offset in seconds
 * (the classic portable formulation; DST edges resolve to one of the two
 * valid answers, which is all an offset lookup promises). */
static long ty_gmtoff(const time_t *t) {
    struct tm lt, ut;
    if (!localtime_r(t, &lt)) return 0;
    if (!gmtime_r(t, &ut)) return 0;
    return (long)(mktime(&lt) - mktime(&ut));
}
#define tm_gmtoff ty_gmtoff_placeholder   /* the shim must not touch tm_gmtoff */

/* ---- POSIX TZ strings, parsed here because the Windows CRT will not --------
 *
 * dtx_offset_at's whole contract is that an EXPLICIT POSIX rule like
 * "EST5EDT,M3.2.0,M11.1.0" is self-contained and therefore gives the same
 * answer on every host -- that is what makes DST testable in a golden. The
 * MSVCRT tzset() accepts only the "TZN[+-]hh[:mm[:ss]][DZN]" prefix and
 * ignores the ",start,end" rule, so it reported standard time all year:
 * US Eastern in July came back -18000 (EST) instead of -14400 (EDT). Not a
 * failed lookup -- a wrong number, silently, for any Tycho program asking for
 * a summer offset on Windows.
 *
 * So parse the rule directly. Supported, which is the whole of what POSIX
 * defines for this field:
 *     std offset [dst [offset] [,start[/time],end[/time]]]
 *   - names: bare alphabetic, or bracketed <...>
 *   - offsets: [+-]hh[:mm[:ss]], POSIX sign (WEST positive, so "EST5" is
 *     UTC-5 and the seconds-east value is negated)
 *   - rules: Mm.w.d (w==5 means "last"), Jn (1..365, no leap day) or n
 *     (0..365, leap day counted); /time defaults to 02:00:00
 * A string that does not parse falls back to the CRT, which is no worse than
 * what this replaced. */
typedef struct { int has; int mon, week, day, yday, julian; long time; } TyTzRule;

static const char *ty_tz_name(const char *p) {
    if (*p == '<') { while (*p && *p != '>') p++; return *p ? p + 1 : p; }
    while (*p && (isalpha((unsigned char)*p))) p++;
    return p;
}

/* [+-]hh[:mm[:ss]] -> seconds. POSIX sign, i.e. the value is WEST-positive. */
static const char *ty_tz_off(const char *p, long *out) {
    int sign = 1;
    if (*p == '+') p++;
    else if (*p == '-') { sign = -1; p++; }
    if (!isdigit((unsigned char)*p)) return NULL;
    long h = 0, m = 0, s = 0;
    while (isdigit((unsigned char)*p)) h = h * 10 + (*p++ - '0');
    if (*p == ':') { p++; while (isdigit((unsigned char)*p)) m = m * 10 + (*p++ - '0'); }
    if (*p == ':') { p++; while (isdigit((unsigned char)*p)) s = s * 10 + (*p++ - '0'); }
    *out = sign * (h * 3600 + m * 60 + s);
    return p;
}

static const char *ty_tz_rule(const char *p, TyTzRule *r) {
    r->has = 0; r->julian = 0; r->time = 2 * 3600;
    if (*p == 'M') {
        p++;
        long mo = 0, wk = 0, dy = 0;
        if (!isdigit((unsigned char)*p)) return NULL;
        while (isdigit((unsigned char)*p)) mo = mo * 10 + (*p++ - '0');
        if (*p++ != '.') return NULL;
        while (isdigit((unsigned char)*p)) wk = wk * 10 + (*p++ - '0');
        if (*p++ != '.') return NULL;
        while (isdigit((unsigned char)*p)) dy = dy * 10 + (*p++ - '0');
        r->mon = (int)mo; r->week = (int)wk; r->day = (int)dy; r->has = 1;
    } else if (*p == 'J' || isdigit((unsigned char)*p)) {
        if (*p == 'J') { r->julian = 1; p++; }
        long n = 0;
        if (!isdigit((unsigned char)*p)) return NULL;
        while (isdigit((unsigned char)*p)) n = n * 10 + (*p++ - '0');
        r->yday = (int)n; r->has = 2;
    } else {
        return NULL;
    }
    if (*p == '/') {
        long t = 0;
        const char *q = ty_tz_off(p + 1, &t);
        if (!q) return NULL;
        r->time = t; p = q;
    }
    return p;
}

/* days since 1970-01-01 for a civil date (Howard Hinnant's algorithm) */
static long ty_days_from_civil(long y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static int ty_is_leap(long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

/* The local-time instant (as seconds since epoch in LOCAL wall clock terms)
 * at which the rule fires in year `y`. */
static long ty_rule_secs(const TyTzRule *r, long y) {
    long day;
    if (r->has == 2) {
        int n = r->yday;
        if (r->julian) {                       /* Jn: 1..365, Feb 29 never counted */
            if (n > 59 && ty_is_leap(y)) n += 1;
            day = ty_days_from_civil(y, 1, 1) + (n - 1);
        } else {                               /* n: 0..365, leap day counted */
            day = ty_days_from_civil(y, 1, 1) + n;
        }
    } else {
        long first = ty_days_from_civil(y, r->mon, 1);
        int dow_first = (int)(((first % 7) + 11) % 7);   /* 1970-01-01 was a Thursday(4) */
        int delta = (r->day - dow_first + 7) % 7;
        long d = first + delta + (long)(r->week - 1) * 7;
        long last = ty_days_from_civil(y, r->mon, 1)
                  + (r->mon == 12 ? 30 : ty_days_from_civil(y, r->mon + 1, 1) - first - 1);
        while (d > last) d -= 7;                          /* week==5 means "the last one" */
        day = d;
    }
    return day * 86400 + r->time;
}

/* Seconds east of UTC for `secs` under `tz`, or 1 on a string we do not parse. */
static int ty_posix_tz_offset(const char *tz, long long secs, long *out) {
    if (!tz || !*tz) return 1;
    const char *p = ty_tz_name(tz);
    if (p == tz) return 1;                                  /* no std name */
    long stdoff_w = 0;
    const char *q = ty_tz_off(p, &stdoff_w);
    if (!q) return 1;                                       /* std name with no offset */
    long stdoff = -stdoff_w;                                /* POSIX is west-positive */
    p = q;
    if (!*p) { *out = stdoff; return 0; }                   /* no DST part: e.g. "UTC0" */
    const char *dname = p;
    p = ty_tz_name(p);
    if (p == dname) return 1;
    long dstoff = stdoff + 3600;
    if (*p && *p != ',') {
        long w = 0;
        q = ty_tz_off(p, &w);
        if (!q) return 1;
        dstoff = -w; p = q;
    }
    if (*p != ',') { *out = stdoff; return 0; }             /* DST named, no rule: no transitions */
    TyTzRule start, end;
    p = ty_tz_rule(p + 1, &start);
    if (!p || *p != ',') return 1;
    p = ty_tz_rule(p + 1, &end);
    if (!p) return 1;

    /* Transitions are stated in local time: start fires at standard offset,
     * end fires at the DST offset (POSIX 8.3). Compare in UTC. */
    long y = 1970;
    {   /* civil year of `secs` in UTC -- close enough, the rules are re-derived
         * for the neighbouring years below so a boundary cannot be misread. */
        long days = (long)(secs / 86400);
        if (secs < 0 && secs % 86400) days -= 1;
        long lo = 1900, hi = 2200;
        while (lo < hi) { long mid = (lo + hi) / 2;
            if (ty_days_from_civil(mid + 1, 1, 1) <= days) lo = mid + 1; else hi = mid; }
        y = lo;
    }
    /* Both transitions are taken from the SAME civil year as `secs`. Widening
     * this to neighbouring years breaks the southern-hemisphere case: with an
     * October start and an April end, "secs >= start(y-1)" is true for every
     * instant in year y, so July in Australia reported DST. Within one year
     * the wrap test is exact, and an instant on either side of 1 January lands
     * in the year whose pair of transitions actually bracket it. */
    long s_utc = ty_rule_secs(&start, y) - stdoff;
    long e_utc = ty_rule_secs(&end,   y) - dstoff;
    if (s_utc <= e_utc) {                                   /* northern hemisphere */
        if (secs >= s_utc && secs < e_utc) { *out = dstoff; return 0; }
    } else {                                                /* southern: DST wraps the year */
        if (secs >= s_utc || secs < e_utc) { *out = dstoff; return 0; }
    }
    *out = stdoff;
    return 0;
}
#endif
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
/* int64-migration (Phase 3): Tycho `int` lowers to tycho_int (int64_t) in the
 * emitted program; this shim is a separate translation unit, so it defines the
 * same type to match the FFI ABI on ILP32/LLP64, not just LP64. `secs` (epoch)
 * and the returned offset are Tycho ints; the libc `time_t` cast and reading
 * glibc's `long tm_gmtoff` stay on the C side. */
#ifndef TYCHO_INT_T
#define TYCHO_INT_T
typedef int64_t tycho_int;
#endif

/* The SYSTEM local timezone's UTC offset at `secs`, DST-aware. Reads the process
 * timezone (the TZ env var, else the OS default) via localtime_r. The exact value
 * is host-dependent, so a test must not hard-code it -- use offset_at for a
 * reproducible zone. */
tycho_int dtx_local_offset(tycho_int secs) {
    time_t t = (time_t)secs;
    struct tm lt;
    if (!localtime_r(&t, &lt)) return 0;   /* fail closed: unknown -> UTC */
#ifdef _WIN32
    return ty_gmtoff(&t);
#else
    return (tycho_int)lt.tm_gmtoff;
#endif
}

/* UTC offset at `secs` for an EXPLICIT POSIX TZ string, DST-aware. A POSIX rule
 * like "EST5EDT,M3.2.0,M11.1.0" or "UTC0" is self-contained -- libc parses the
 * DST rule from the string with no /usr/share/zoneinfo file -- so the result is
 * reproducible on any host (which is what makes DST testable in a golden).
 *
 * NOT thread-safe: it sets the process TZ (setenv + tzset), reads, then restores.
 * The FFI boundary is outside Tycho's race-free guarantee anyway (see
 * docs/reference/ffi.md#threads) -- serialize datetime.offset_at across threads. */
tycho_int dtx_offset_at(const char *tz, tycho_int secs) {
#ifdef _WIN32
    /* Answer from the rule itself. The CRT only reads the "TZN[+-]hh" prefix
     * and drops the ",start,end", which made every DST answer wrong (US
     * Eastern in July: -18000, want -14400). Falls through to the CRT below
     * for anything this parser does not recognise. */
    {
        long off = 0;
        if (ty_posix_tz_offset(tz, (long long)secs, &off) == 0) return (tycho_int)off;
    }
#endif
    char *cur = getenv("TZ");
    char *saved = cur ? strdup(cur) : NULL;   /* NULL if unset or on OOM: restore by unsetting */
    setenv("TZ", tz, 1);
    tzset();
    time_t t = (time_t)secs;
    struct tm lt;
    tycho_int off = 0;
#ifdef _WIN32
    if (localtime_r(&t, &lt)) off = (tycho_int)ty_gmtoff(&t);
#else
    if (localtime_r(&t, &lt)) off = (tycho_int)lt.tm_gmtoff;
#endif
    if (saved) { setenv("TZ", saved, 1); free(saved); } else { unsetenv("TZ"); }
    tzset();                                  /* restore the process zone exactly once */
    return off;
}
