/* Local-only transport for the actual HTTP/DB reader benchmark. */
#define _POSIX_C_SOURCE 200809L
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../../corelib/tycho.h"
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
static struct { unsigned char *data; size_t n, line; int peer, fd; tycho_int chunk; pthread_t thread; } transport;
static int active;
static void *produce(void *unused) {
    (void)unused;
    size_t off = 0;
    while (off < transport.n) {
        ssize_t n = send(transport.peer, transport.data + off, transport.n - off, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;  /* early refusal is allowed to close before consuming the frame */
        off += (size_t)n;
    }
    close(transport.peer);
    return NULL;
}
tycho_int caller_open(const unsigned char *data, tycho_int n, tycho_int chunk) {
    if (active || n < 0 || chunk <= 0 || chunk > 1048576) return -1;
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds)) return -1;
    unsigned char *copy = malloc(n ? (size_t)n : 1);
    if (!copy) { close(fds[0]); close(fds[1]); return -1; }
    if (n) memcpy(copy, data, (size_t)n);  /* never retain an FFI argument borrow */
    transport.data = copy; transport.n = (size_t)n; transport.line = 0;
    transport.fd = fds[0]; transport.peer = fds[1]; transport.chunk = chunk;
    signal(SIGPIPE, SIG_IGN);  /* fixture process only; Darwin has no MSG_NOSIGNAL */
    if (pthread_create(&transport.thread, NULL, produce, NULL)) {
        free(copy); close(fds[0]); close(fds[1]); return -1;
    }
    active = 1; return fds[0];
}
void caller_close(tycho_int fd) {
    if (!active || fd != transport.fd) abort();
    close(transport.fd); pthread_join(transport.thread, NULL);
    free(transport.data); active = 0;
}
/* The real shim still performs recv, malloc/ownership transfer, and error
 * classification. Capping only its request makes fragmentation reproducible
 * without adding sleeps or substituting a synthetic reader implementation. */
void caller_real_read(tycho_int, tycho_int, tycho_int *, unsigned char **, tycho_int *);
void netx_read(tycho_int fd, tycho_int max, tycho_int *status, unsigned char **out, tycho_int *n) {
    if (max > transport.chunk) max = transport.chunk;
    caller_real_read(fd, max, status, out, n);
    if (getenv("CALLER_TRACE")) fprintf(stderr, "read: %lld %lld %lld\n",
        (long long)max, (long long)*status, (long long)*n);
}
void caller_note(tycho_int kind, tycho_int raw, tycho_int output, tycho_int rest) {
    if (getenv("CALLER_TRACE")) fprintf(stderr, "phase: %lld %lld %lld %lld\n",
        (long long)kind, (long long)raw, (long long)output, (long long)rest);
}
tycho_int caller_verify_body(const unsigned char *body, tycho_int n) {
    for (size_t i = 0; i + 4 <= transport.n; i++) {
        if (!memcmp(transport.data + i, "\r\n\r\n", 4)) {
            size_t expected = transport.n - i - 4;
            return n >= 0 && (size_t)n == expected && !memcmp(body, transport.data + i + 4, expected);
        }
    }
    return 0;
}
tycho_int caller_verify_line(const char *line, tycho_int n) {
    size_t end = transport.line;
    while (end < transport.n && transport.data[end] != '\n') end++;
    size_t len = end - transport.line;
    if (len && transport.data[transport.line + len - 1] == '\r') len--;
    int ok = n >= 0 && (size_t)n == len && !memcmp(line, transport.data + transport.line, len);
    transport.line = end + 1;
    return ok;
}
