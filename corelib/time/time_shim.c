/* core:time shim -- blocking sleep over POSIX nanosleep. Pure libc (no external
 * dependency, no `deps` file), so core:time stays turnkey and core tier like
 * core:datetime / core:os / core:net.
 *
 * The rest of core:time is pure Tycho over the clock()/now() builtins; this is
 * its only FFI, and it exists because a monotonic READING is not enough to
 * write a backoff, a retry, or a shutdown drain -- those need to WAIT.
 *
 * Three semantics are decided here, deliberately, and documented on the Tycho
 * side (corelib/time/time.ty) because callers depend on them:
 *
 *   1. A duration <= 0 returns IMMEDIATELY -- no syscall, no busy-spin, no
 *      abort. A zero backoff is a normal value at the start of a retry loop.
 *   2. NOT interruptible. nanosleep can return early with EINTR when a signal
 *      is delivered; this retries with the REMAINING time, so the full
 *      requested duration always elapses. A sleep that silently returns short
 *      turns a backoff schedule into a spin, which is the bug this avoids.
 *   3. Blocks the CALLING THREAD only. A Tycho task is a pthread
 *      (runtime/tycho_rt.c spawn/wait), so a worker that sleeps does not stall
 *      its siblings.
 */
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
    /* Semantics (2) is "at least the requested duration", and on Windows
     * nanosleep alone does not deliver it AS THE CALLER MEASURES IT: winpthreads
     * wakes on a timer tick that can land just before the observing clock
     * crosses the mark, so `sleep_ms(200)` reads back 199. Measured on Windows
     * 11 26200 under 12 spinners: 1 of 60 sleeps came back at 199 ms; idle, 0 of
     * 60 and a minimum of exactly 200. That one is enough to redden
     * corelib/test/time (sleep_ms_at_least) at random, which it did in a full
     * `make ci` sweep on 2026-08-08.
     *
     * So sleep against a DEADLINE read from the same clock the caller measures
     * with -- clock_gettime(CLOCK_MONOTONIC), which is what the runtime's
     * clock() builtin uses (runtime/tycho_rt.c:1434) and therefore what
     * time.elapsed_ms sees. Re-sleeping the remainder until that clock agrees
     * makes the contract true by construction in the domain it is asserted in,
     * whatever the timer granularity underneath. */
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
