/* core:signal shim -- a deliberately narrow SIGTERM/SIGINT surface whose only
 * action is to release the accept loops of a server. Pure libc (no external
 * dependency, no `deps` file), so `import "core:signal"` is turnkey and its test
 * never skips -- the same self-contained model as core:os and core:net.
 *
 * WHY THIS IS NOT A GENERAL `signal.on(sig, fn)`. A Tycho function invoked from
 * a handler would have to be re-entrant against the arena allocator and the
 * scheduler, and neither is. Nothing in this tree needs it; see the header of
 * signal.ty for what a wide version would have to add. Here NO Tycho code runs
 * in handler context at all -- the handler is sigx_handler below and nothing
 * else.
 *
 * ASYNC-SIGNAL-SAFETY, the whole of it. Ignoring the errno save/restore that
 * brackets it, sigx_handler does exactly three things, and each is safe by the
 * list in `man 7 signal-safety`:
 *
 *   1. `sigx_flag = 1;`      a store to a `volatile sig_atomic_t`. POSIX names
 *                            this the one object type a handler may write while
 *                            the rest of the program may read it.
 *   2. `int fd = sigx_fd;`   a load from a `volatile sig_atomic_t`, same
 *                            guarantee. No lock, no TLS, no libc call.
 *   3. `shutdown(fd, ...)`   on the POSIX async-signal-safe function list. It is
 *                            a bare syscall; it takes no lock the interrupted
 *                            thread could already hold, and it allocates nothing.
 *
 * There is no malloc, no printf, no arena touch and no pthread call, so there is
 * no lock a handler can deadlock the interrupted thread against. errno IS saved
 * and restored around the three steps: shutdown() may set it, and the thread this
 * handler interrupted is entitled to find its own value there afterwards -- a
 * handler that clobbers errno corrupts the error reporting of code that never
 * called it.
 *
 * WHY `shutdown` AND NOT `close`. Measured, not argued: with four accept loops on
 * one listener and a process-directed SIGTERM, `shutdown(fd, SHUT_RDWR)` released
 * 4/4 loops in the same millisecond (`accept` returning -1/EINVAL), while
 * `close(fd)` released 1/4 and left three blocked forever -- and the closed fd
 * number was immediately handed back out by a later open(), with three threads
 * still blocked on it. The table is in plan.md's phase 1 evidence.
 *
 * PORTABILITY, recorded honestly: `shutdown` on a LISTENING socket waking a
 * blocked `accept` is a Linux behaviour, not a POSIX guarantee, and pending
 * connections still in the backlog are dropped rather than drained. Both are
 * acceptable for a shutdown path. This tree is POSIX-only in practice and Linux
 * tested; Windows is out of scope, so the whole file is guarded like core:net's.
 */
#ifndef _WIN32
#include <signal.h>
#include <string.h>             /* memset -- struct sigaction, outside the handler */
#include <errno.h>              /* saved/restored across the handler */
#include <limits.h>             /* INT_MAX -- the tycho_int -> int fd narrowing */
#include <sys/types.h>
#include <sys/socket.h>         /* shutdown, SHUT_RDWR */
#endif
#include <stdint.h>

/* int64-migration (Phase 3): Tycho `int` lowers to tycho_int (int64_t) in the
 * emitted program; this shim is a separate translation unit, so it defines the
 * same type to match the FFI ABI on ILP32/LLP64, not just LP64. */
#ifndef TYCHO_INT_T
#define TYCHO_INT_T
typedef int64_t tycho_int;
#endif

#ifndef _WIN32

/* The listening fd the handler will shut down, and whether a shutdown signal has
 * arrived. `volatile sig_atomic_t` is the only shape a handler may write; an fd
 * is an `int` and sig_atomic_t is at least as wide, so the fd fits -- the range
 * is checked in sigx_on_shutdown before the store, not in the handler.
 *
 * -1 means "no fd registered": the handler then only sets the flag, so a signal
 * that lands before sigx_on_shutdown finishes can never shutdown() a stale
 * descriptor. */
static volatile sig_atomic_t sigx_fd = -1;
static volatile sig_atomic_t sigx_flag = 0;

static void sigx_handler(int sig) {
    (void)sig;
    int saved = errno;              /* shutdown() may clobber errno; the interrupted
                                     * thread is entitled to find its own value */
    sigx_flag = 1;
    int fd = (int)sigx_fd;
    if (fd >= 0) shutdown(fd, SHUT_RDWR);
    errno = saved;
}

/* Install the handler for SIGTERM and SIGINT, remembering `fd` as the listener to
 * release. Returns 1 on success, 0 on failure -- fail closed: a caller that
 * ignores the result keeps the default disposition (the process dies on SIGTERM),
 * which is today's behaviour, never a half-armed one.
 *
 * The fd is stored BEFORE the first sigaction, so there is no window in which a
 * signal finds a handler installed and no fd to act on.
 *
 * sa_flags is 0, i.e. NO SA_RESTART, on purpose. Measured: with SA_RESTART the
 * kernel restarts the interrupted accept() under the receiving thread and that
 * thread does not wake at all (0/4); without it the receiver additionally gets
 * -1/EINTR, which reaches the same wind-down arm a hair earlier. shutdown()
 * releases the other loops either way -- the mechanism does not depend on which
 * thread the kernel picked, which is exactly why it was chosen. */
tycho_int sigx_on_shutdown(tycho_int fd) {
    if (fd < 0 || fd > INT_MAX) return 0;
    sigx_fd = (sig_atomic_t)fd;
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = sigx_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, NULL) != 0) return 0;
    if (sigaction(SIGINT, &sa, NULL) != 0) return 0;
    return 1;
}

/* Has a shutdown signal been seen? Reads the flag the handler sets. 1 or 0. */
tycho_int sigx_requested(void) {
    return sigx_flag ? 1 : 0;
}

#else

/* Windows has no sigaction and no shutdown-wakes-accept behaviour. Fail closed:
 * install reports failure, and nothing is ever reported as requested. */
tycho_int sigx_on_shutdown(tycho_int fd) { (void)fd; return 0; }
tycho_int sigx_requested(void) { return 0; }

#endif
