#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE          /* glibc: expose sigaction + sigemptyset */
#endif
#ifdef _WIN32
#include <winsock2.h>     /* SOCKET / shutdown / SD_BOTH -- MUST precede windows.h */
#include <windows.h>      /* SetConsoleCtrlHandler / BOOL / WINAPI */
#include <signal.h>       /* sig_atomic_t -- C99 standard, present in mingw */
#include <limits.h>       /* INT_MAX -- the tycho_int -> int fd narrowing */
#else
#include <signal.h>
#include <string.h>             /* memset -- struct sigaction, outside the handler */
#include <errno.h>              /* saved/restored across the handler */
#include <limits.h>             /* INT_MAX -- the tycho_int -> int fd narrowing */
#include <sys/types.h>
#include <sys/socket.h>         /* shutdown, SHUT_RDWR */
#endif
#include <stdint.h>

#include "../tycho.h"

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

#define SIGX_MAX_SLOTS 256
static volatile sig_atomic_t sigx_conns[SIGX_MAX_SLOTS]; /* 0 = empty, else fd+1 */

#ifndef _WIN32

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
 * THE LISTENER GOES FIRST, deliberately: it requests an accept wakeup, and the
 * connection shutdowns then land on workers that are winding down. The
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

#endif

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

#ifndef _WIN32

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

tycho_int sigx_win_isolate_console(void) { return 0; }
tycho_int sigx_win_raise_break(void)     { return 0; }

#else

/* Windows. No sigaction and no POSIX shutdown-wakes-accept guarantee:
 * SetConsoleCtrlHandler gives ONE callback for the console control events
 * (Ctrl-C, Ctrl-Break, close/logoff/shutdown). The callback runs on a fresh
 * thread with a 64KB stack -- NOT in the interrupted thread's context -- so the
 * POSIX async-signal-safety list does not transfer verbatim, but the same
 * discipline holds: it touches only the flag, the listener fd and the registry
 * (volatile sig_atomic_t stores and loads), and calls shutdown() (a Winsock
 * call that wakes a blocked recv; whether it also wakes a blocked accept on a
 * LISTENING socket is Windows-version dependent -- closesocket is the accepted
 * modern fallback, so the listener gets both, in that order). The definitive
 * accept-wake behaviour is the windows CI leg's verification; either way the
 * flag is set, and a server winds down on its own timeouts if the accept is
 * not released.
 *
 * Returning TRUE for Ctrl-C/Ctrl-Break keeps the console from killing the
 * process out from under the wind-down; close/logoff/shutdown events are
 * returned FALSE so the console shutdown proceeds. */
static BOOL WINAPI sigx_ctrl_handler(DWORD type) {
    sigx_flag = 1;
    int fd = (int)sigx_fd;
    if (fd >= 0) {
        shutdown((SOCKET)fd, SD_BOTH);
        closesocket((SOCKET)fd);          /* the accept-wake on modern Windows */
    }
    for (int i = 0; i < SIGX_MAX_SLOTS; i++) {
        int v = (int)sigx_conns[i];
        if (v > 0) shutdown((SOCKET)(v - 1), SD_BOTH);
    }
    return (type == CTRL_CLOSE_EVENT || type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) ? FALSE : TRUE;
}

tycho_int sigx_on_shutdown(tycho_int fd) {
    if (fd < 0 || fd > INT_MAX) return 0;
    sigx_fd = (sig_atomic_t)fd;
    if (!SetConsoleCtrlHandler(sigx_ctrl_handler, TRUE)) return 0;
    return 1;
}

tycho_int sigx_win_isolate_console(void) {
    FreeConsole();                       /* may legitimately fail if there is none */
    if (!AllocConsole()) return 0;
    return SetConsoleCtrlHandler(sigx_ctrl_handler, TRUE) ? 1 : 0;
}

/* Raise CTRL_BREAK on this process's own console. CTRL_BREAK and not CTRL_C:
 * MSDN gives no way to direct a CTRL_C_EVENT at a specific group, and a new
 * process group starts with Ctrl-C disabled. The handler treats them alike. */
tycho_int sigx_win_raise_break(void) {
    return GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0) ? 1 : 0;
}

#endif

/* Has a shutdown signal been seen? Reads the flag the handler sets. 1 or 0. */
tycho_int sigx_requested(void) {
    return sigx_flag ? 1 : 0;
}
