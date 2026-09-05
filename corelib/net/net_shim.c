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

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>              /* EAGAIN/EWOULDBLOCK: SO_RCVTIMEO vs a real error */
#include "../tycho.h"

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
 * in tycho_str_copy at the call site (src/tychoc.c:9675-9678), so the borrow ends
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
    if (n < 0) { free(buf); return; }       /* receive error */
    if (n == 0) { free(buf); return; }      /* legal 0-length datagram, reported the same way */
    *out = buf;
    *outlen = n;
}

/* Wait up to `ms` for one connection. The listener is made nonblocking before
 * select so two workers woken for one arrival cannot leave the loser stuck in
 * accept. Accepted sockets are forced back to blocking mode for net.read.
 * Returns -2 on timeout, -1 on failure, or the connected fd. */
#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>               /* not select(): select cannot see a listener at fd >= FD_SETSIZE */
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

/* Wait up to `ms` for a connection, then accept it. The listener is put into
 * non-blocking mode only for the accept and put BACK on every exit path: leaving
 * it non-blocking made a later plain `net.accept` on the same listener return
 * EAGAIN at once instead of waiting. poll() replaces select() because select's
 * fd_set cannot represent a descriptor at or above FD_SETSIZE (1024) at all, so
 * a busy process could not use this call. */
tycho_int netx_accept_wait(tycho_int fd, tycho_int ms) {
    if (fd < 0 || ms < 0 || ms > INT_MAX) return -1;
#ifndef _WIN32
    int was_flags = fcntl((int)fd, F_GETFL, 0);
    if (was_flags < 0) return -1;
#endif
    if (!ty_set_nonblocking((int)fd, 1)) return -1;
#ifdef _WIN32
    fd_set ready;
    FD_ZERO(&ready);
    FD_SET((SOCKET)fd, &ready);
    struct timeval tv;
    tv.tv_sec = (long)(ms / 1000);
    tv.tv_usec = (long)((ms % 1000) * 1000);
    int selected = select(0, &ready, NULL, NULL, &tv);
#else
    struct pollfd pfd;
    pfd.fd = (int)fd; pfd.events = POLLIN; pfd.revents = 0;
    int selected;
    do { selected = poll(&pfd, 1, (int)ms); } while (selected < 0 && errno == EINTR);
#endif
    tycho_int rc;
    if (selected == 0) { rc = -2; goto done; }
    if (selected < 0)  { rc = -1; goto done; }
    rc = netx_accept(fd);
    if (rc < 0) {
#ifndef _WIN32
        if (errno == EAGAIN || errno == EWOULDBLOCK) rc = -2;
#else
        if (WSAGetLastError() == WSAEWOULDBLOCK) rc = -2;
#endif
        else rc = -1;
        goto done;
    }
    if (!ty_set_nonblocking((int)rc, 0)) { TY_CLOSE((int)rc); rc = -1; }
done:
#ifndef _WIN32
    (void)fcntl((int)fd, F_SETFL, was_flags);   /* restore the listener exactly as it was */
#else
    (void)ty_set_nonblocking((int)fd, 0);
#endif
    return rc;
}

/* ---- readiness over a SET of descriptors ---- */

/* poll(2), not select(2): select's fd_set cannot represent a descriptor at or
 * above FD_SETSIZE (1024), which is the ceiling netx_accept_wait above was
 * rewritten to escape. A readiness call over a SET is exactly where that
 * ceiling bites -- a server holding a thousand keep-alive peers is the case
 * this exists for -- so reintroducing it here would undo that work. */
#define TY_PL_READY 0
#define TY_PL_TMO   1
#define TY_PL_ERR   2
#define TY_PL_MAX   1048576     /* refuse an absurd n rather than malloc it */

#ifdef _WIN32
typedef WSAPOLLFD ty_pollfd;
#define TY_POLL_IN   POLLRDNORM
static int ty_poll(ty_pollfd *p, tycho_int n, int ms) { return WSAPoll(p, (ULONG)n, ms); }
#else
typedef struct pollfd ty_pollfd;
#define TY_POLL_IN   POLLIN
static int ty_poll(ty_pollfd *p, tycho_int n, int ms) { return poll(p, (nfds_t)n, ms); }
#endif

/* Which of `fds` can be read without blocking, waiting at most `ms` for the
 * first. Writes the READY SUBSET (a fresh malloc'd buffer the caller's runtime
 * copies and frees) and one of TY_PL_*.
 *
 * A peer that closed counts as ready: POLLHUP/POLLERR mean the next recv
 * returns 0 or fails at once, which is an answer the caller needs, not a wait.
 * POLLNVAL is the opposite -- it means the caller handed over a descriptor that
 * is not open, so the whole call fails closed rather than naming it ready and
 * sending the caller into a recv on a stranger's fd.
 *
 * Fail closed on an empty set, a negative fd anywhere in it, or a negative
 * timeout: each is a caller bug, and returning "timed out" for any of them
 * turns it into a silent spin. */
void netx_poll_readable(const tycho_int *fds, tycho_int nfds, tycho_int ms,
                        tycho_int *status, tycho_int **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    *status = TY_PL_ERR;
    if (!fds || nfds <= 0 || nfds > TY_PL_MAX || ms < 0 || ms > INT_MAX) return;
    for (tycho_int i = 0; i < nfds; i++) if (fds[i] < 0) return;

    ty_pollfd *pf = (ty_pollfd *)calloc((size_t)nfds, sizeof *pf);
    if (!pf) return;
    for (tycho_int i = 0; i < nfds; i++) {
        pf[i].fd = (int)fds[i];
        pf[i].events = TY_POLL_IN;
        pf[i].revents = 0;
    }
    int r;
    do { r = ty_poll(pf, nfds, (int)ms); } while (r < 0 && errno == EINTR);
    if (r < 0)  { free(pf); return; }
    if (r == 0) { free(pf); *status = TY_PL_TMO; return; }

    tycho_int *res = (tycho_int *)malloc((size_t)nfds * sizeof *res);
    if (!res) { free(pf); return; }
    tycho_int k = 0;
    for (tycho_int i = 0; i < nfds; i++) {
        if (pf[i].revents & POLLNVAL) { free(pf); free(res); return; }
        if (pf[i].revents & (TY_POLL_IN | POLLHUP | POLLERR)) res[k++] = fds[i];
    }
    free(pf);
    if (k == 0) { free(res); *status = TY_PL_TMO; return; }   /* woken by nothing we asked for */
    *out = res;
    *outlen = k;
    *status = TY_PL_READY;
}
