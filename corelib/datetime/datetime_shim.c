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
#include <stdlib.h>
#include <string.h>

/* The SYSTEM local timezone's UTC offset at `secs`, DST-aware. Reads the process
 * timezone (the TZ env var, else the OS default) via localtime_r. The exact value
 * is host-dependent, so a test must not hard-code it -- use offset_at for a
 * reproducible zone. */
long dtx_local_offset(long secs) {
    time_t t = (time_t)secs;
    struct tm lt;
    if (!localtime_r(&t, &lt)) return 0;   /* fail closed: unknown -> UTC */
    return (long)lt.tm_gmtoff;
}

/* UTC offset at `secs` for an EXPLICIT POSIX TZ string, DST-aware. A POSIX rule
 * like "EST5EDT,M3.2.0,M11.1.0" or "UTC0" is self-contained -- libc parses the
 * DST rule from the string with no /usr/share/zoneinfo file -- so the result is
 * reproducible on any host (which is what makes DST testable in a golden).
 *
 * NOT thread-safe: it sets the process TZ (setenv + tzset), reads, then restores.
 * The FFI boundary is outside Tycho's race-free guarantee anyway (see
 * docs/reference/ffi.md#threads) -- serialize datetime.offset_at across threads. */
long dtx_offset_at(const char *tz, long secs) {
    char *cur = getenv("TZ");
    char *saved = cur ? strdup(cur) : NULL;   /* NULL if unset or on OOM: restore by unsetting */
    setenv("TZ", tz, 1);
    tzset();
    time_t t = (time_t)secs;
    struct tm lt;
    long off = 0;
    if (localtime_r(&t, &lt)) off = (long)lt.tm_gmtoff;
    if (saved) { setenv("TZ", saved, 1); free(saved); } else { unsetenv("TZ"); }
    tzset();                                  /* restore the process zone exactly once */
    return off;
}
