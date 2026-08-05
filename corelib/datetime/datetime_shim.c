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
