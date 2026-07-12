/* core:tls shim -- a TLS 1.2/1.3 CLIENT over OpenSSL libssl. The deps manifest
 * names `openssl`; pkg-config supplies -lssl -lcrypto, so it is turnkey where
 * OpenSSL is installed and its test is skipped where it is not.
 *
 * Secure by default: the server certificate is verified against the system CA
 * store AND the hostname is checked, and SNI is sent. Any resolve / connect /
 * handshake / verification failure yields a NULL handle -- fail closed, so a
 * caller that forgets to check simply has no connection rather than an insecure
 * one. connect returns an opaque handle (SSL* + SSL_CTX* + fd); read/write move
 * the encrypted stream; close_conn shuts it down and frees everything.
 *
 * OpenSSL 1.1.0+ self-initializes, so there is no explicit library init here.
 */
#include <openssl/ssl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct { SSL_CTX *ctx; SSL *ssl; int fd; } Tls;

/* Resolve host:port and open a blocking TCP connection; -1 on any failure. */
static int tcp_connect(const char *host, long port) {
    if (!host || port < 0 || port > 65535) return -1;
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%ld", port);
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

void *tlsx_connect(const char *host, long port) {
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
long tlsx_write(void *p, const unsigned char *data, long len) {
    if (!p || len < 0) return -1;
    Tls *t = (Tls *)p;
    long off = 0;
    while (off < len) {
        int n = SSL_write(t->ssl, data + off, (int)(len - off));
        if (n <= 0) return -1;                                 /* fail closed */
        off += n;
    }
    return off;
}

/* Read up to `max` decrypted bytes (one SSL_read); empty on close/error. */
void tlsx_read(void *p, long max, unsigned char **out, long *outlen) {
    *out = NULL;
    *outlen = 0;
    if (!p || max <= 0) return;
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
