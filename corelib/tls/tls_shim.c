#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE          /* glibc: expose getaddrinfo + struct addrinfo */
#endif
#ifndef _WIN32
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "../tycho.h"

typedef struct { SSL_CTX *ctx; SSL *ssl; int fd; } Tls;

/* Resolve host:port and open a blocking TCP connection; -1 on any failure. */
static int tcp_connect(const char *host, tycho_int port) {
    if (!host || port < 0 || port > 65535) return -1;
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
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

void *tlsx_connect(const char *host, tycho_int port) {
    int fd = tcp_connect(host, port);
    if (fd < 0) return NULL;
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
    Tls *t = (Tls *)malloc(sizeof *t);
    if (!t) { SSL_free(ssl); SSL_CTX_free(ctx); close(fd); return NULL; }
    t->ctx = ctx; t->ssl = ssl; t->fd = fd;
    return t;
}

/* Write the whole buffer over the encrypted stream; bytes sent (== len) or -1. */
tycho_int tlsx_write(void *p, const unsigned char *data, tycho_int len) {
    if (!p || len < 0) return -1;
    Tls *t = (Tls *)p;
    tycho_int off = 0;
    while (off < len) {
        int n = SSL_write(t->ssl, data + off, (int)(len - off));
        if (n <= 0) return -1;                                 /* fail closed */
        off += n;
    }
    return off;
}

/* Read up to `max` decrypted bytes (one SSL_read); empty on close/error. */
void tlsx_read(void *p, tycho_int max, unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
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
    SSL_shutdown(t->ssl);
    SSL_free(t->ssl);
    SSL_CTX_free(t->ctx);
    close(t->fd);
    free(t);
}
