#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L    /* nanosleep + struct timespec */
#endif
#include <time.h>
#include <errno.h>
#include <stdint.h>

/* int64-migration: Tycho `int` lowers to tycho_int (int64_t) in the emitted
 * program; this shim is a separate translation unit, so it defines the same
 * type to match the FFI ABI on ILP32/LLP64, not just LP64. */
#ifndef TYCHO_INT_T
#define TYCHO_INT_T
typedef int64_t tycho_int;
#endif

static void tmx_nanosleep(tycho_int ns) {
    struct timespec req, rem;
    req.tv_sec  = (time_t)(ns / 1000000000);
    req.tv_nsec = (long)  (ns % 1000000000);
    while (nanosleep(&req, &rem) != 0) {
        if (errno != EINTR) return;                        /* EINVAL is impossible above; fail open */
        req = rem;                                         /* (2) finish the remainder */
    }
}

void tmx_sleep_ns(tycho_int ns) {
    if (ns <= 0) return;                                   /* (1) non-positive: return now */
#ifdef _WIN32
    struct timespec t0;
    if (clock_gettime(CLOCK_MONOTONIC, &t0) != 0) { tmx_nanosleep(ns); return; }
    for (;;) {
        struct timespec now;
        tycho_int elapsed;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return;
        elapsed = (tycho_int)(now.tv_sec - t0.tv_sec) * 1000000000
                + (tycho_int)(now.tv_nsec - t0.tv_nsec);
        if (elapsed >= ns) return;
        tmx_nanosleep(ns - elapsed);
    }
#else
    tmx_nanosleep(ns);
#endif
}
