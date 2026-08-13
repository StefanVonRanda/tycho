/* core:net shim -- TCP/UDP sockets over the POSIX sockets API. Pure libc (no
 * external dependency, no `deps` file), so `import "core:net"` is turnkey and its
 * test never skips -- the same self-contained model as core:os.
 *
 * The FFI boundary (see docs/reference/ffi.md):
 *   - A socket is a file descriptor, crossed as a plain `int` (Tycho int ==
 *     `tycho_int`). A negative return always means failure -- fail closed, the
 *     caller branches on `< 0` and never touches a half-open fd.
 *   - A binary payload crosses as `bytes`: a parameter lowers to
 *     `(const unsigned char *ptr, tycho_int len)`; a `bytes` RETURN uses the
 *     out-param convention `(unsigned char **out, tycho_int *outlen)` -- this
 *     shim mallocs *out (or leaves it NULL for the empty result) and
 *     tycho_bytes_from_c copies it into the caller's arena and frees it.
 *     Length-carried, so interior NUL bytes survive intact (unlike a `string`).
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE          /* glibc: expose getaddrinfo + struct addrinfo */
#endif
#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>           /* struct timeval, for SO_RCVTIMEO */
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>          /* inet_ntop -- netx_peer_addr */
#include <netdb.h>
#include <unistd.h>
#define TY_CLOSE close
#define TY_INVALID (-1)
#else
/* Best-effort Windows path: Winsock2. WSAStartup runs once on first use; MSVC
 * auto-links ws2_32 via the pragma. Untested lane -- the CI/dev target is POSIX. */
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define TY_CLOSE closesocket
#define TY_INVALID INVALID_SOCKET
static void ty_net_init(void) {
    static int done = 0;
    if (!done) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); done = 1; }
}
#endif

/* Writing to a socket whose peer has gone away raises SIGPIPE, whose default
 * disposition terminates the PROCESS -- every task, every other connection. For
 * a server that is a remote kill switch: one client that sends a partial request
 * and closes without reading is enough (measured 2026-07-26, `poll()` = -13).
 * MSG_NOSIGNAL turns that into a plain EPIPE return, which netx_write already
 * handles as "fail closed, report -1".
 *
 * This cannot be worked around by the Tycho caller. Signal disposition is a
 * process-wide property with no Tycho spelling, and netx_write loops over short
 * writes internally, so even a single logical net.write() can issue several
 * send() calls -- the first returns ECONNRESET, the second raises the signal.
 * Winsock has no such flag and no SIGPIPE, so the fallback of 0 is correct
 * there. BSD/macOS additionally get SO_NOSIGPIPE set on the socket in
 * make_sock(), because MSG_NOSIGNAL is not portable to older Darwin. */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>              /* EAGAIN/EWOULDBLOCK: SO_RCVTIMEO vs a real error */
/* int64-migration (Phase 3): Tycho `int` lowers to tycho_int (int64_t) in the
 * emitted program; this shim is a separate translation unit, so it defines the
 * same type to match the FFI ABI on ILP32/LLP64, not just LP64. */
#ifndef TYCHO_INT_T
#define TYCHO_INT_T
typedef int64_t tycho_int;
#endif

/* Resolve host + port to a single IPv4 endpoint via getaddrinfo. A numeric IP
 * (e.g. "127.0.0.1") resolves offline -- no DNS -- so the loopback test stays
 * hermetic; a hostname uses the system resolver. `st` is SOCK_STREAM or
 * SOCK_DGRAM so the hints match the socket we are about to make. Returns 0 and
 * fills *out on success; -1 on any failure (fail closed). */
static int resolve4(const char *host, tycho_int port, int st, struct sockaddr_in *out) {
#ifdef _WIN32
    ty_net_init();
#endif
    if (!host || port < 0 || port > 65535) return -1;
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", (int)port);   /* port validated 0..65535 above */
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;      /* IPv4 only in v1 -- IPv6 is a demand-gated follow-up */
    hints.ai_socktype = st;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return -1;
    memcpy(out, res->ai_addr, sizeof(struct sockaddr_in));
    freeaddrinfo(res);
    return 0;
}

/* socket(AF_INET, st, 0), returning the fd or -1. Centralizes the WSAStartup
 * touch on Windows so every entry point is covered. */
static tycho_int make_sock(int st) {
#ifdef _WIN32
    ty_net_init();
#endif
    tycho_int fd = (tycho_int)socket(AF_INET, st, 0);
    return fd;   /* socket() returns -1 / INVALID_SOCKET on failure; both are < 0 as a long on POSIX */
}

/* ---- TCP ---- */

/* Create a listening TCP socket bound to host:port. Passing port 0 asks the
 * kernel for an ephemeral port (recover it with netx_port_of) -- the hermetic
 * test relies on this so it never collides with a real service. SO_REUSEADDR is
 * set so a re-run does not trip TIME_WAIT "address already in use". */
tycho_int netx_listen(const char *host, tycho_int port) {
    struct sockaddr_in a;
    if (resolve4(host, port, SOCK_STREAM, &a) != 0) return -1;
    tycho_int fd = make_sock(SOCK_STREAM);
    if (fd < 0) return -1;
    int on = 1;
    setsockopt((int)fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&on, sizeof on);
    if (bind((int)fd, (struct sockaddr *)&a, sizeof a) != 0 || listen((int)fd, 16) != 0) {
        TY_CLOSE((int)fd);
        return -1;
    }
    return fd;
}

/* Per-connection tuning applied to every CONNECTED stream socket (accepted or
 * dialled). Both settings are best-effort; a failure leaves the kernel default.
 *
 * SO_NOSIGPIPE  -- BSD/macOS only. Linux uses the per-send MSG_NOSIGNAL flag in
 *                  netx_write instead; Windows has no SIGPIPE. See the note at
 *                  the top of this file.
 *
 * TCP_NODELAY   -- disable Nagle. This is not a micro-optimization, it is a
 *                  correctness-grade latency fix for request/response protocols.
 *                  A caller that writes a response as a header write followed by
 *                  a body write -- which is exactly what corelib/httpd's
 *                  write_response does, on purpose, to avoid copying the body --
 *                  has its second small segment held by Nagle until the first is
 *                  ACKed, and the peer's delayed-ACK timer is ~40 ms. Measured
 *                  2026-07-26 with tycho-httpd on loopback, 300 keep-alive
 *                  requests for a 294-byte file:
 *
 *                      two writes, Nagle on   43.73 ms/req      23 req/s
 *                      one write,  Nagle on    0.07 ms/req   14465 req/s
 *
 *                  Same server, same bytes; the whole difference is one stalled
 *                  segment per response. Every production HTTP server sets this,
 *                  and no Tycho program can: there is no setsockopt surface in
 *                  core:net, and this is a property of the connection, not of
 *                  anything the caller writes. */
static void ty_tune_stream(tycho_int fd) {
    int on = 1;
#ifdef SO_NOSIGPIPE
    setsockopt((int)fd, SOL_SOCKET, SO_NOSIGPIPE, (const void *)&on, sizeof on);
#endif
#ifdef TCP_NODELAY
    setsockopt((int)fd, IPPROTO_TCP, TCP_NODELAY, (const void *)&on, sizeof on);
#endif
    (void)on;
    (void)fd;
}

/* Accept one pending connection off a listener; returns the connected fd or -1. */
tycho_int netx_accept(tycho_int fd) {
    if (fd < 0) return -1;
    tycho_int c = (tycho_int)accept((int)fd, NULL, NULL);
    if (c >= 0) ty_tune_stream(c);
    return c;
}

/* Connect a new TCP socket to host:port; returns the connected fd or -1. */
tycho_int netx_connect(const char *host, tycho_int port) {
    struct sockaddr_in a;
    if (resolve4(host, port, SOCK_STREAM, &a) != 0) return -1;
    tycho_int fd = make_sock(SOCK_STREAM);
    if (fd < 0) return -1;
    if (connect((int)fd, (struct sockaddr *)&a, sizeof a) != 0) {
        TY_CLOSE((int)fd);
        return -1;
    }
    ty_tune_stream(fd);
    return fd;
}

/* The local port an fd is bound to (getsockname), or -1. Lets a caller that
 * bound port 0 learn the kernel-assigned ephemeral port. */
tycho_int netx_port_of(tycho_int fd) {
    if (fd < 0) return -1;
    struct sockaddr_in a;
    socklen_t len = sizeof a;
    if (getsockname((int)fd, (struct sockaddr *)&a, &len) != 0) return -1;
    return (tycho_int)ntohs(a.sin_port);
}

/* The PEER's address as presentation text (getpeername + inet_ntop): "127.0.0.1"
 * for IPv4, the compressed form for IPv6, "" if the fd is not connected, is a
 * family this shim does not know (AF_UNIX), or the call fails. Fail closed on the
 * empty string: core:net turns it into Err(Failed) rather than logging a lie.
 *
 * The buffer is `__thread`, not `static`: N worker TASKS are N pthreads sharing
 * one listening fd, so a single shared buffer would be a data race on the one
 * field an access log wants most (crypto_shim.c:43 sets the same precedent). The
 * caller only ever sees a copy anyway -- an extern `-> string` return is wrapped
 * in tycho_str_copy at the call site (src/tychoc.c:9083-9086), so the borrow ends
 * before the next request can overwrite it. */
const char *netx_peer_addr(tycho_int fd) {
    static __thread char buf[INET6_ADDRSTRLEN];
    buf[0] = '\0';
    if (fd < 0) return buf;
    struct sockaddr_storage ss;
    socklen_t len = sizeof ss;
    if (getpeername((int)fd, (struct sockaddr *)&ss, &len) != 0) return buf;
    if (ss.ss_family == AF_INET) {
        struct sockaddr_in *v4 = (struct sockaddr_in *)&ss;
        if (!inet_ntop(AF_INET, &v4->sin_addr, buf, sizeof buf)) buf[0] = '\0';
    } else if (ss.ss_family == AF_INET6) {
        struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)&ss;
        if (!inet_ntop(AF_INET6, &v6->sin6_addr, buf, sizeof buf)) buf[0] = '\0';
    }
    return buf;
}

/* Send the whole buffer (looping over short writes); returns the byte count sent
 * (== len) or -1 on error. A 0-length payload is a valid no-op that returns 0.
 * MSG_NOSIGNAL: a peer that vanished mid-write must yield EPIPE here, never a
 * SIGPIPE that kills the process -- see the note at the top of this file. */
tycho_int netx_write(tycho_int fd, const unsigned char *data, tycho_int len) {
    if (fd < 0 || len < 0) return -1;
    tycho_int off = 0;
    while (off < len) {
        tycho_int n = (tycho_int)send((int)fd, (const char *)data + off, (size_t)(len - off), MSG_NOSIGNAL);
        if (n <= 0) return -1;   /* fail closed: report the partial-send as an error */
        off += n;
    }
    return off;
}

/* Read up to `max` bytes (one recv), CLASSIFYING the outcome in *status:
 *
 *      TY_RD_DATA (1)  data was read; *out holds *outlen bytes
 *      TY_RD_EOF  (0)  the peer closed cleanly (recv returned 0)
 *      TY_RD_TMO  (2)  the SO_RCVTIMEO deadline expired with no data
 *      TY_RD_ERR  (3)  the read failed for any other reason
 *
 * The classification is the whole point of this signature. Until 2026-07-26 this
 * function returned only `bytes`, so EOF, a timeout and a hard error were all the
 * empty result, and a caller could not tell "the peer hung up" from "the peer is
 * being slow" -- the collision that made tycho-httpd unable to answer 400 to
 * garbage while closing silently on a hangup (FRICTION.md, phase 4/7). recv
 * distinguishes them at the syscall level; only the FFI shape was throwing the
 * distinction away, so core:net now hands it up as a Result(bytes, net.NetErr).
 *
 * `status` is an `inout int` on the Tycho side, which lowers to a leading
 * tycho_int* out-param ahead of the two `bytes` return out-params. On EOF, a
 * timeout or an error, *out stays NULL (tycho_bytes_from_c allocates an empty
 * buffer and does not free NULL); on data, *out is a malloc'd buffer the runtime
 * frees after arena-copying exactly *outlen bytes. */
#define TY_RD_EOF  0
#define TY_RD_DATA 1
#define TY_RD_TMO  2
#define TY_RD_ERR  3

/* Was the last failed recv a receive-timeout rather than a real error? On POSIX
 * SO_RCVTIMEO surfaces as EAGAIN/EWOULDBLOCK; Winsock reports WSAETIMEDOUT. */
static int ty_recv_timed_out(void) {
#ifndef _WIN32
    return errno == EAGAIN || errno == EWOULDBLOCK;
#else
    return WSAGetLastError() == WSAETIMEDOUT;
#endif
}

void netx_read(tycho_int fd, tycho_int max, tycho_int *status,
               unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    *status = TY_RD_ERR;
    if (fd < 0 || max <= 0) return;
    unsigned char *buf = (unsigned char *)malloc((size_t)max);
    if (!buf) return;                       /* fail closed: empty result, never a partial read */
    errno = 0;
    tycho_int n = (tycho_int)recv((int)fd, (char *)buf, (size_t)max, 0);
    if (n == 0) { free(buf); *status = TY_RD_EOF; return; }
    if (n < 0) {
        free(buf);
        *status = ty_recv_timed_out() ? TY_RD_TMO : TY_RD_ERR;
        return;
    }
    *out = buf;
    *outlen = n;
    *status = TY_RD_DATA;
}

/* Arm a receive timeout on `fd` (SO_RCVTIMEO). After `ms` milliseconds with no
 * data, recv() fails with EAGAIN/EWOULDBLOCK and netx_read yields the empty
 * bytes -- indistinguishable from EOF on purpose, because a server's response to
 * both is the same: drop the connection. ms <= 0 clears the timeout (block
 * forever, the default). Returns 1 on success, 0 on failure -- fail closed, so a
 * caller that ignores the result still gets the blocking default, never a
 * silently-half-armed socket. This is what lets a keep-alive loop refuse to be
 * pinned by an idle or slow-loris peer. */
tycho_int netx_set_read_timeout(tycho_int fd, tycho_int ms) {
    if (fd < 0) return 0;
    if (ms < 0) ms = 0;
#ifndef _WIN32
    struct timeval tv;
    tv.tv_sec = (time_t)(ms / 1000);
    tv.tv_usec = (suseconds_t)((ms % 1000) * 1000);
    if (setsockopt((int)fd, SOL_SOCKET, SO_RCVTIMEO, (const void *)&tv, sizeof tv) != 0) return 0;
#else
    DWORD tv = (DWORD)ms;   /* Winsock takes a DWORD of milliseconds, not a timeval */
    if (setsockopt((int)fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) != 0) return 0;
#endif
    return 1;
}

/* Close an fd (idempotent-safe on a negative fd). */
void netx_close(tycho_int fd) {
    if (fd >= 0) TY_CLOSE((int)fd);
}

/* ---- UDP ---- */

/* Bind a UDP socket to host:port (port 0 -> ephemeral, recover with port_of);
 * returns the fd or -1. */
tycho_int netx_udp_bind(const char *host, tycho_int port) {
    struct sockaddr_in a;
    if (resolve4(host, port, SOCK_DGRAM, &a) != 0) return -1;
    tycho_int fd = make_sock(SOCK_DGRAM);
    if (fd < 0) return -1;
    int on = 1;
    setsockopt((int)fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&on, sizeof on);
    if (bind((int)fd, (struct sockaddr *)&a, sizeof a) != 0) {
        TY_CLOSE((int)fd);
        return -1;
    }
    return fd;
}

/* Send one datagram to host:port; returns the byte count sent or -1. */
tycho_int netx_udp_send(tycho_int fd, const char *host, tycho_int port, const unsigned char *data, tycho_int len) {
    if (fd < 0 || len < 0) return -1;
    struct sockaddr_in a;
    if (resolve4(host, port, SOCK_DGRAM, &a) != 0) return -1;
    tycho_int n = (tycho_int)sendto((int)fd, (const char *)data, (size_t)len, 0,
                          (struct sockaddr *)&a, sizeof a);
    return n < 0 ? -1 : n;
}

/* Receive one datagram (up to `max` bytes); the sender address is discarded in
 * v1. Same out-param bytes contract as netx_read. */
void netx_udp_read(tycho_int fd, tycho_int max, unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    if (fd < 0 || max <= 0) return;
    unsigned char *buf = (unsigned char *)malloc((size_t)max);
    if (!buf) return;
    tycho_int n = (tycho_int)recvfrom((int)fd, (char *)buf, (size_t)max, 0, NULL, NULL);
    if (n < 0) { free(buf); return; }       /* a 0-length datagram is legal -> keep buf, outlen 0 */
    if (n == 0) { free(buf); return; }
    *out = buf;
    *outlen = n;
}

/* Wait up to `ms` for one connection. The listener is made nonblocking before
 * select so two workers woken for one arrival cannot leave the loser stuck in
 * accept. Accepted sockets are forced back to blocking mode for net.read.
 * Returns -2 on timeout, -1 on failure, or the connected fd. */
#ifndef _WIN32
#include <fcntl.h>
#include <sys/select.h>
#endif
#include <limits.h>
static int ty_set_nonblocking(int fd, int on) {
#ifndef _WIN32
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return 0;
    flags = on ? flags | O_NONBLOCK : flags & ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags) == 0;
#else
    u_long mode = on ? 1 : 0;
    return ioctlsocket((SOCKET)fd, FIONBIO, &mode) == 0;
#endif
}

tycho_int netx_accept_wait(tycho_int fd, tycho_int ms) {
    if (fd < 0 || ms < 0 || ms > INT_MAX) return -1;
#ifndef _WIN32
    if (fd >= FD_SETSIZE) return -1;
#endif
    if (!ty_set_nonblocking((int)fd, 1)) return -1;
    fd_set ready;
    FD_ZERO(&ready);
    FD_SET((int)fd, &ready);
    struct timeval tv;
    tv.tv_sec = (long)(ms / 1000);
    tv.tv_usec = (long)((ms % 1000) * 1000);
#ifdef _WIN32
    int selected = select(0, &ready, NULL, NULL, &tv);
#else
    int selected = select((int)fd + 1, &ready, NULL, NULL, &tv);
#endif
    if (selected == 0) return -2;
    if (selected < 0) return -1;
    tycho_int conn = netx_accept(fd);
    if (conn < 0) {
#ifndef _WIN32
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -2;
#else
        if (WSAGetLastError() == WSAEWOULDBLOCK) return -2;
#endif
        return -1;
    }
    if (!ty_set_nonblocking((int)conn, 0)) { TY_CLOSE((int)conn); return -1; }
    return conn;
}
