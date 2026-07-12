/* core:net shim -- TCP/UDP sockets over the POSIX sockets API. Pure libc (no
 * external dependency, no `deps` file), so `import "core:net"` is turnkey and its
 * test never skips -- the same self-contained model as core:os.
 *
 * The FFI boundary (see docs/reference/ffi.md):
 *   - A socket is a file descriptor, crossed as a plain `int` (Tycho int == C
 *     `long`). A negative return always means failure -- fail closed, the caller
 *     branches on `< 0` and never touches a half-open fd.
 *   - A binary payload crosses as `bytes`: a parameter lowers to
 *     `(const unsigned char *ptr, long len)`; a `bytes` RETURN uses the out-param
 *     convention `(unsigned char **out, long *outlen)` -- this shim mallocs *out
 *     (or leaves it NULL for the empty result) and tycho_bytes_from_c copies it
 *     into the caller's arena and frees it. Length-carried, so interior NUL bytes
 *     survive intact (unlike a `string` payload).
 */
#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Resolve host + port to a single IPv4 endpoint via getaddrinfo. A numeric IP
 * (e.g. "127.0.0.1") resolves offline -- no DNS -- so the loopback test stays
 * hermetic; a hostname uses the system resolver. `st` is SOCK_STREAM or
 * SOCK_DGRAM so the hints match the socket we are about to make. Returns 0 and
 * fills *out on success; -1 on any failure (fail closed). */
static int resolve4(const char *host, long port, int st, struct sockaddr_in *out) {
#ifdef _WIN32
    ty_net_init();
#endif
    if (!host || port < 0 || port > 65535) return -1;
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%ld", port);
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
static long make_sock(int st) {
#ifdef _WIN32
    ty_net_init();
#endif
    long fd = (long)socket(AF_INET, st, 0);
    return fd;   /* socket() returns -1 / INVALID_SOCKET on failure; both are < 0 as a long on POSIX */
}

/* ---- TCP ---- */

/* Create a listening TCP socket bound to host:port. Passing port 0 asks the
 * kernel for an ephemeral port (recover it with netx_port_of) -- the hermetic
 * test relies on this so it never collides with a real service. SO_REUSEADDR is
 * set so a re-run does not trip TIME_WAIT "address already in use". */
long netx_listen(const char *host, long port) {
    struct sockaddr_in a;
    if (resolve4(host, port, SOCK_STREAM, &a) != 0) return -1;
    long fd = make_sock(SOCK_STREAM);
    if (fd < 0) return -1;
    int on = 1;
    setsockopt((int)fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&on, sizeof on);
    if (bind((int)fd, (struct sockaddr *)&a, sizeof a) != 0 || listen((int)fd, 16) != 0) {
        TY_CLOSE((int)fd);
        return -1;
    }
    return fd;
}

/* Accept one pending connection off a listener; returns the connected fd or -1. */
long netx_accept(long fd) {
    if (fd < 0) return -1;
    long c = (long)accept((int)fd, NULL, NULL);
    return c;
}

/* Connect a new TCP socket to host:port; returns the connected fd or -1. */
long netx_connect(const char *host, long port) {
    struct sockaddr_in a;
    if (resolve4(host, port, SOCK_STREAM, &a) != 0) return -1;
    long fd = make_sock(SOCK_STREAM);
    if (fd < 0) return -1;
    if (connect((int)fd, (struct sockaddr *)&a, sizeof a) != 0) {
        TY_CLOSE((int)fd);
        return -1;
    }
    return fd;
}

/* The local port an fd is bound to (getsockname), or -1. Lets a caller that
 * bound port 0 learn the kernel-assigned ephemeral port. */
long netx_port_of(long fd) {
    if (fd < 0) return -1;
    struct sockaddr_in a;
    socklen_t len = sizeof a;
    if (getsockname((int)fd, (struct sockaddr *)&a, &len) != 0) return -1;
    return (long)ntohs(a.sin_port);
}

/* Send the whole buffer (looping over short writes); returns the byte count sent
 * (== len) or -1 on error. A 0-length payload is a valid no-op that returns 0. */
long netx_write(long fd, const unsigned char *data, long len) {
    if (fd < 0 || len < 0) return -1;
    long off = 0;
    while (off < len) {
        long n = (long)send((int)fd, (const char *)data + off, (size_t)(len - off), 0);
        if (n <= 0) return -1;   /* fail closed: report the partial-send as an error */
        off += n;
    }
    return off;
}

/* Read up to `max` bytes (one recv). On EOF (0) or error (<0) yields the empty
 * bytes with *out == NULL (tycho_bytes_from_c allocates an empty buffer and does
 * not free NULL). On data, *out is a malloc'd buffer of the bytes read and the
 * runtime frees it after arena-copying exactly `*outlen` bytes. */
void netx_read(long fd, long max, unsigned char **out, long *outlen) {
    *out = NULL;
    *outlen = 0;
    if (fd < 0 || max <= 0) return;
    unsigned char *buf = (unsigned char *)malloc((size_t)max);
    if (!buf) return;                       /* fail closed: empty result, never a partial read */
    long n = (long)recv((int)fd, (char *)buf, (size_t)max, 0);
    if (n <= 0) { free(buf); return; }      /* EOF or error -> empty */
    *out = buf;
    *outlen = n;
}

/* Close an fd (idempotent-safe on a negative fd). */
void netx_close(long fd) {
    if (fd >= 0) TY_CLOSE((int)fd);
}

/* ---- UDP ---- */

/* Bind a UDP socket to host:port (port 0 -> ephemeral, recover with port_of);
 * returns the fd or -1. */
long netx_udp_bind(const char *host, long port) {
    struct sockaddr_in a;
    if (resolve4(host, port, SOCK_DGRAM, &a) != 0) return -1;
    long fd = make_sock(SOCK_DGRAM);
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
long netx_udp_send(long fd, const char *host, long port, const unsigned char *data, long len) {
    if (fd < 0 || len < 0) return -1;
    struct sockaddr_in a;
    if (resolve4(host, port, SOCK_DGRAM, &a) != 0) return -1;
    long n = (long)sendto((int)fd, (const char *)data, (size_t)len, 0,
                          (struct sockaddr *)&a, sizeof a);
    return n < 0 ? -1 : n;
}

/* Receive one datagram (up to `max` bytes); the sender address is discarded in
 * v1. Same out-param bytes contract as netx_read. */
void netx_udp_read(long fd, long max, unsigned char **out, long *outlen) {
    *out = NULL;
    *outlen = 0;
    if (fd < 0 || max <= 0) return;
    unsigned char *buf = (unsigned char *)malloc((size_t)max);
    if (!buf) return;
    long n = (long)recvfrom((int)fd, (char *)buf, (size_t)max, 0, NULL, NULL);
    if (n < 0) { free(buf); return; }       /* a 0-length datagram is legal -> keep buf, outlen 0 */
    if (n == 0) { free(buf); return; }
    *out = buf;
    *outlen = n;
}
