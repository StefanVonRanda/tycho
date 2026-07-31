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
 * ASYNC-SIGNAL-SAFETY is argued statement by statement in the block above
 * sigx_handler, which is the only code that runs in handler context. In summary:
 * the handler stores to and loads from `volatile sig_atomic_t` objects, calls
 * shutdown() -- on the POSIX async-signal-safe list, a bare syscall that takes no
 * lock and allocates nothing -- and does nothing else. No malloc, no printf, no
 * arena touch, no pthread call, so there is no lock it can deadlock the
 * interrupted thread against. errno IS saved and restored around the whole body:
 * shutdown() may set it, and the thread this handler interrupted is entitled to
 * find its own value there afterwards -- a handler that clobbers errno corrupts
 * the error reporting of code that never called it.
 *
 * The handler shuts down TWO things: the listening socket, which releases every
 * thread parked in accept(2), and every accepted connection published in the
 * registry below, which releases every thread parked in recv(2). The listener
 * alone is not enough; see the registry's header for the measurement.
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

/* ---- the accepted-connection registry -------------------------------------
 *
 * WHY IT EXISTS. Shutting the listener down releases every thread parked in
 * accept(2), but it does nothing for a thread parked in recv(2) on an ALREADY
 * ACCEPTED connection: nothing wakes that read but its own SO_RCVTIMEO. Measured
 * on tycho-httpd with --idle-ms 5000, four idle keep-alive clients: 4878 ms from
 * kill(2) to wait(2) returning, entirely one idle timeout, against 1 ms once the
 * accepted fds are registered here (5 runs of 5). That is what this table buys.
 *
 * WHY A FIXED ARRAY AND NO LOCK. A mutex is NOT on the `man 7 signal-safety`
 * list, and a handler that blocks on one the interrupted thread already holds
 * deadlocks the process -- it is a bug that waits for the right interleaving
 * rather than failing in test. The way out is to need no mutual exclusion at
 * all: one slot per worker, written by exactly one thread.
 *
 *   * Slot i is written ONLY by worker i, from ordinary (non-handler) context.
 *     Workers never touch each other's slots, so there is no write/write pair
 *     anywhere in the program and nothing to serialise.
 *   * The handler only ever READS the slots. So the only concurrent pair is one
 *     writer and one reader on a single `volatile sig_atomic_t`, which is
 *     precisely the object POSIX defines as safe for exactly this -- the reader
 *     observes the old value or the new one, never a torn one.
 *   * 256 slots because server/main.ty:672@workers rejects `--workers` outside
 *     1..256, and a worker's accept loop holds at most one connection at a time
 *     (server/main.ty:557-591: accept, serve, retire, close, in one sequential
 *     body). One slot per worker is therefore sufficient, not merely convenient.
 *
 * WHY fd+1 AND NOT fd. 0 means "empty", so the whole table is correct at static
 * zero-initialisation and there is no init pass to race with a signal that
 * arrives during arming. Storing the fd directly would make 0 -- a perfectly
 * legal descriptor -- indistinguishable from an empty slot.
 *
 * THE STALE-fd WINDOW, stated rather than waved away. Retiring is the half that
 * matters: server/main.ty clears the slot BEFORE close(2), so a stale read
 * requires the handler to observe a value the owning thread overwrote strictly
 * earlier. If that window is ever hit, the handler calls shutdown() on a number
 * that is either closed (EBADF), reused by another connection (which is being
 * shut down anyway -- that is the point of the signal), or reused by a regular
 * file (ENOTSOCK). shutdown() closes nothing, frees nothing and discards no
 * written data; it is the same property that made it the right call over close()
 * for the listener, and it is what makes a benign race benign here. Reversing
 * the order -- close first, clear second -- would NOT be safe, because then a
 * handler reading the live slot targets a number the kernel has already handed
 * back out. */
#define SIGX_MAX_SLOTS 256
static volatile sig_atomic_t sigx_conns[SIGX_MAX_SLOTS]; /* 0 = empty, else fd+1 */

/* The handler. Every statement below is on the `man 7 signal-safety` list:
 *
 *   1. `int saved = errno;`     a load from a thread-local int. No call.
 *   2. `sigx_flag = 1;`         a store to a volatile sig_atomic_t -- the one
 *                               object type POSIX lets a handler write while the
 *                               rest of the program reads it.
 *   3. `int fd = sigx_fd;`      a load from a volatile sig_atomic_t, same rule.
 *   4. `shutdown(fd, ...)`      an async-signal-safe function; a bare syscall
 *                               that takes no lock and allocates nothing.
 *   5. the `for` loop           a local int counter and SIGX_MAX_SLOTS loads
 *                               from volatile sig_atomic_t, each into a local
 *                               before it is tested -- so the value that is
 *                               range-checked is the value that is passed to
 *                               shutdown(), and no slot is read twice.
 *   6. `errno = saved;`         a store to a thread-local int.
 *
 * No malloc, no stdio, no pthread call, no arena touch, and no libc function
 * outside the safe list, so there is no lock the handler can deadlock the
 * interrupted thread against. The loop is bounded and branch-only apart from the
 * shutdown() calls, which is why running it in handler context is affordable.
 *
 * THE LISTENER GOES FIRST, deliberately: it releases the accept loops, and the
 * connection shutdowns then land on workers that are already winding down. The
 * flag goes first of all, so any thread woken by either shutdown finds it set. */
static void sigx_handler(int sig) {
    (void)sig;
    int saved = errno;              /* shutdown() may clobber errno; the interrupted
                                     * thread is entitled to find its own value */
    sigx_flag = 1;
    int fd = (int)sigx_fd;
    if (fd >= 0) shutdown(fd, SHUT_RDWR);
    for (int i = 0; i < SIGX_MAX_SLOTS; i++) {
        int v = (int)sigx_conns[i];
        if (v > 0) shutdown(v - 1, SHUT_RDWR);
    }
    errno = saved;
}

/* Publish `fd` in `slot` so the handler will shut it down too. 1 on success, 0
 * if the slot is out of range or the fd will not fit -- fail closed: a caller
 * that is refused keeps exactly today's behaviour, one SO_RCVTIMEO of shutdown
 * latency, and never a wrong descriptor in the table. Every range check lives
 * here, in ordinary context, so the handler has none to do.
 *
 * Registering is not synchronised with the handler and does not need to be: a
 * signal that lands mid-store either sees the slot empty (the connection is one
 * idle timeout behind -- today's behaviour) or sees the fd (released at once).
 * Both outcomes are correct; neither is a torn read. */
tycho_int sigx_conn_register(tycho_int slot, tycho_int fd) {
    if (slot < 0 || slot >= SIGX_MAX_SLOTS) return 0;
    if (fd < 0 || fd >= INT_MAX) return 0;      /* fd+1 must not overflow int */
    sigx_conns[(int)slot] = (sig_atomic_t)(fd + 1);
    return 1;
}

/* Clear `slot`. MUST be called before the fd is closed -- see the stale-fd note
 * above for why that order and not the other one. Out-of-range slots are ignored
 * rather than reported: retire is a cleanup path and has nothing useful to do
 * with a failure, and the matching register already refused the same slot. */
void sigx_conn_retire(tycho_int slot) {
    if (slot < 0 || slot >= SIGX_MAX_SLOTS) return;
    sigx_conns[(int)slot] = 0;
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
tycho_int sigx_conn_register(tycho_int slot, tycho_int fd) { (void)slot; (void)fd; return 0; }
void sigx_conn_retire(tycho_int slot) { (void)slot; }

#endif
