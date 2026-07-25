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

void tmx_sleep_ns(tycho_int ns) {
    if (ns <= 0) return;                                   /* (1) non-positive: return now */
    struct timespec req, rem;
    req.tv_sec  = (time_t)(ns / 1000000000);
    req.tv_nsec = (long)  (ns % 1000000000);
    while (nanosleep(&req, &rem) != 0) {
        if (errno != EINTR) return;                        /* EINVAL is impossible above; fail open */
        req = rem;                                         /* (2) finish the remainder */
    }
}
