#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE          /* glibc: expose getaddrinfo + struct addrinfo */
#endif
#include <limits.h>    /* INT_MAX -- the SSL_read/SSL_write length clamps */
#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>  /* struct timeval -- SO_RCVTIMEO/SO_SNDTIMEO */
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>     /* O_NONBLOCK -- the bounded connect below */
#include <poll.h>
#else
/* Windows: the socket surface lives in Winsock (ws2_32, via the deps `_WIN32:`
 * section); winsock2.h must precede any header that pulls in windows.h.
 * `close` is POSIX; mingw spells it `_close` (unistd.h, which declares it,
 * is not available). */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>        /* _close */
#define close _close
#endif
#include <openssl/ssl.h>
#include <errno.h>     /* EINTR -- the SIGPIPE drain below */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "../tycho.h"

/* A RUN-TIME guard, not a compile-time rule: a `ptr` handle is not affine, so
   nothing stops a caller freeing one twice or using it afterwards. The header is
   deliberately NOT released on free -- the sentinel in it is what the next call
   reads. */
#define TLSX_LIVE 0x546c73784c495645ull
#define TLSX_DEAD 0x546c737844454144ull
typedef struct { uint64_t magic; SSL_CTX *ctx; SSL *ssl; int fd; } Tls;

static void tlsx_live(const Tls *t, int freeing) {
    if (!t || t->magic == TLSX_LIVE) return;
    fputs(freeing ? "tycho: double free of tls connection handle\n"
                  : "tycho: tls connection handle used after free\n", stderr);
    exit(1);
}

/* ---- SIGPIPE, suppressed for THIS write only ------------------------------
 * Writing to a peer that has gone away raises SIGPIPE, whose default action
 * kills the process -- so a departed peer took the whole program down instead
 * of tlsx_write returning -1. The fix must not be process-wide: a Tycho
 * program that installs its own SIGPIPE disposition, or wants the default
 * elsewhere, still gets it.
 *
 * OpenSSL's socket BIO writes with write(2), not send(MSG_NOSIGNAL), so the
 * flag cannot be passed per-call. Two per-socket/per-thread routes instead:
 * SO_NOSIGPIPE where the platform has it (macOS/BSD), and otherwise blocking
 * SIGPIPE on the CALLING THREAD across the write and draining what the write
 * raised. A SIGPIPE already pending on entry is left alone -- it belongs to
 * somebody else and consuming it would swallow their signal. */
#ifndef _WIN32
#include <signal.h>
#include <time.h>
#endif

#if !defined(_WIN32) && !defined(SO_NOSIGPIPE)
#include <pthread.h>
typedef struct { sigset_t old; int blocked, was_pending; } TlsSigGuard;

static void tls_sig_begin(TlsSigGuard *g) {
    g->blocked = 0; g->was_pending = 0;
    sigset_t pipeset, pend;
    sigemptyset(&pipeset);
    sigaddset(&pipeset, SIGPIPE);
    if (sigpending(&pend) == 0 && sigismember(&pend, SIGPIPE) == 1) g->was_pending = 1;
    if (pthread_sigmask(SIG_BLOCK, &pipeset, &g->old) == 0) g->blocked = 1;
}

static void tls_sig_end(TlsSigGuard *g) {
    if (!g->blocked) return;
    if (!g->was_pending) {                     /* drain only what WE raised */
        sigset_t pipeset, pend;
        sigemptyset(&pipeset);
        sigaddset(&pipeset, SIGPIPE);
        struct timespec zero = { 0, 0 };
        if (sigpending(&pend) == 0 && sigismember(&pend, SIGPIPE) == 1)
            while (sigtimedwait(&pipeset, NULL, &zero) < 0 && errno == EINTR) { }
    }
    pthread_sigmask(SIG_SETMASK, &g->old, NULL);
}
#else
typedef struct { int unused; } TlsSigGuard;
static void tls_sig_begin(TlsSigGuard *g) { (void)g; }
static void tls_sig_end(TlsSigGuard *g)   { (void)g; }
#endif

/* Resolve host:port and open a blocking TCP connection; -1 on any failure. */
/* Arm (ms > 0) or clear (ms == 0) both socket timeouts. A TLS read blocks in
 * SSL_read on the underlying recv, so SO_RCVTIMEO is what bounds it; without one
 * a peer that accepts and then says nothing pins the caller forever. */
static int tls_set_sock_timeout(int fd, tycho_int ms) {
    if (ms < 0) return 0;
#ifndef _WIN32
    struct timeval tv;
    tv.tv_sec  = (long)(ms / 1000);
    tv.tv_usec = (long)((ms % 1000) * 1000);
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const void *)&tv, sizeof tv) != 0) return 0;
    return setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const void *)&tv, sizeof tv) == 0;
#else
    DWORD tv = (DWORD)ms;
    if (setsockopt((SOCKET)fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) != 0) return 0;
    return setsockopt((SOCKET)fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof tv) == 0;
#endif
}

#ifndef _WIN32
/* connect(2) bounded by `ms`: non-blocking connect, poll for writability, then
 * read SO_ERROR -- a poll that returns ready still has to be asked whether the
 * connect SUCCEEDED. The descriptor is put back to blocking on every path. */
static int tcp_connect_timed(int fd, const struct sockaddr *sa, socklen_t salen, tycho_int ms) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) return -1;
    int rc = connect(fd, sa, salen);
    if (rc != 0 && errno == EINPROGRESS) {
        struct pollfd pfd;
        pfd.fd = fd; pfd.events = POLLOUT; pfd.revents = 0;
        int pr;
        do { pr = poll(&pfd, 1, (int)ms); } while (pr < 0 && errno == EINTR);
        if (pr <= 0) rc = -1;
        else {
            int soerr = 0;
            socklen_t l = sizeof soerr;
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &l) != 0 || soerr != 0) rc = -1;
            else rc = 0;
        }
    }
    (void)fcntl(fd, F_SETFL, flags);
    return rc;
}
#endif

static int tcp_connect(const char *host, tycho_int port, tycho_int ms) {
    if (!host || port < 0 || port > 65535 || ms < 0) return -1;
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", (int)port);   /* port validated 0..65535 above */
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return -1;
    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
#ifdef SO_NOSIGPIPE
        {   /* macOS/BSD: per-socket, so no signal mask is needed at write time */
            int on = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
        }
#endif
#ifndef _WIN32
        if (ms > 0) {
            if (tcp_connect_timed(fd, rp->ai_addr, (socklen_t)rp->ai_addrlen, ms) == 0) break;
            close(fd);
            fd = -1;
            continue;
        }
#endif
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* `ms` == 0 keeps the old unbounded behaviour (the same default `net.connect`
 * has); ms > 0 bounds the TCP connect AND the handshake, then clears the socket
 * timeouts again so the connection reads the way the caller expects. */
void *tlsx_connect_timeout(const char *host, tycho_int port, tycho_int ms) {
    int fd = tcp_connect(host, port, ms);
    if (fd < 0) return NULL;
    if (ms > 0 && !tls_set_sock_timeout(fd, ms)) { close(fd); return NULL; }
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { close(fd); return NULL; }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);            /* require a valid cert */
    if (!SSL_CTX_set_default_verify_paths(ctx)) {              /* system CA store */
        SSL_CTX_free(ctx); close(fd); return NULL;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL *ssl = SSL_new(ctx);
    if (!ssl) { SSL_CTX_free(ctx); close(fd); return NULL; }
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, host);                       /* SNI -> the server picks its cert */
    SSL_set1_host(ssl, host);                                  /* verify the cert matches the hostname */
    if (SSL_connect(ssl) != 1) {                               /* handshake + cert/hostname verification */
        SSL_free(ssl); SSL_CTX_free(ctx); close(fd); return NULL;
    }
    if (ms > 0) (void)tls_set_sock_timeout(fd, 0);              /* handshake done: unbound again */
    Tls *t = (Tls *)malloc(sizeof *t);
    if (!t) { SSL_free(ssl); SSL_CTX_free(ctx); close(fd); return NULL; }
    t->magic = TLSX_LIVE; t->ctx = ctx; t->ssl = ssl; t->fd = fd;
    return t;
}

void *tlsx_connect(const char *host, tycho_int port) {
    return tlsx_connect_timeout(host, port, 0);
}

/* Bound every later read/write on an established connection. 0 clears it.
 * Returns 1 on success, 0 on failure -- the net.set_read_timeout_ms shape. */
tycho_int tlsx_set_timeout(void *p, tycho_int ms) {
    tlsx_live((const Tls *)p, 0);
    if (!p || ms < 0) return 0;
    return tls_set_sock_timeout(((Tls *)p)->fd, ms) ? 1 : 0;
}

/* Write the whole buffer over the encrypted stream; bytes sent (== len) or -1. */
tycho_int tlsx_write(void *p, const unsigned char *data, tycho_int len) {
    tlsx_live((const Tls *)p, 0);
    if (!p || len < 0) return -1;
    Tls *t = (Tls *)p;
    tycho_int off = 0;
    TlsSigGuard g;
    tls_sig_begin(&g);                                         /* a departed peer must not kill the process */
    while (off < len) {
        /* SSL_write takes an int; clamp the same way tlsx_read does. */
        tycho_int want = len - off;
        if (want > INT_MAX) want = INT_MAX;
        int n = SSL_write(t->ssl, data + off, (int)want);
        if (n <= 0) { tls_sig_end(&g); return -1; }            /* fail closed */
        off += n;
    }
    tls_sig_end(&g);
    return off;
}

/* Read up to `max` decrypted bytes (one SSL_read); empty on close/error. */
void tlsx_read(void *p, tycho_int max, unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    tlsx_live((const Tls *)p, 0);
    if (!p || max <= 0) return;
    /* SSL_read takes an int. tycho_int is 64-bit, so a caller asking for more
     * than INT_MAX would hand SSL_read a TRUNCATED or negative length while the
     * buffer really was that big -- undefined, and silently. Clamp instead: a
     * short read is the documented contract here ("up to max"), so nothing is
     * lost. netx_read has the same shape but passes size_t and needs no clamp. */
    if (max > INT_MAX) max = INT_MAX;
    Tls *t = (Tls *)p;
    unsigned char *buf = (unsigned char *)malloc((size_t)max);
    if (!buf) return;
    int n = SSL_read(t->ssl, buf, (int)max);
    if (n <= 0) { free(buf); return; }
    *out = buf;
    *outlen = n;
}

void tlsx_close(void *p) {
    if (!p) return;
    Tls *t = (Tls *)p;
    tlsx_live(t, 1);
    SSL_shutdown(t->ssl);
    SSL_free(t->ssl);
    SSL_CTX_free(t->ctx);
    close(t->fd);
    t->ssl = NULL; t->ctx = NULL; t->fd = -1;
    t->magic = TLSX_DEAD;
}
