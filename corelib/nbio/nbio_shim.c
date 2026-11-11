#include <stdlib.h>
#include <stdint.h>
#include "../tycho.h"

#ifdef _WIN32
#include <winsock2.h>
typedef struct { int fd; int events; } TyReady;
#else
#include <poll.h>
typedef struct { int fd; int events; } TyReady;
#endif

typedef struct { int n; TyReady *v; } TyPoll;

void *nbx_poll(const tycho_int *fds, tycho_int nfds, const tycho_int *events, tycho_int nevents, tycho_int timeout_ms) {
    if (nfds < 0 || nfds > 65536 || nfds != nevents) return NULL;
    TyPoll *r = (TyPoll *)calloc(1, sizeof *r);
    if (!r) return NULL;
#ifdef _WIN32
    WSAPOLLFD *p = (WSAPOLLFD *)calloc((size_t)nfds, sizeof *p);
#else
    struct pollfd *p = (struct pollfd *)calloc((size_t)nfds, sizeof *p);
#endif
    if (nfds > 0 && !p) { free(r); return NULL; }
    tycho_int i = 0;
    for (i = 0; i < nfds; i++) {
        p[i].fd = (int)fds[i];
        short ev = 0;
        if (events[i] & 1) {
#ifdef _WIN32
            ev |= POLLRDNORM;
#else
            ev |= POLLIN;
#endif
        }
        if (events[i] & 2) {
#ifdef _WIN32
            ev |= POLLWRNORM;
#else
            ev |= POLLOUT;
#endif
        }
        p[i].events = ev;
    }
#ifdef _WIN32
    int rc = WSAPoll(p, (ULONG)nfds, (INT)timeout_ms);
#else
    int rc = poll(p, (nfds_t)nfds, (int)timeout_ms);
#endif
    if (rc < 0) { free(p); free(r); return NULL; }
    r->v = (TyReady *)calloc((size_t)(rc > 0 ? rc : 1), sizeof *r->v);
    if (!r->v) { free(p); free(r); return NULL; }
    for (i = 0; i < nfds; i++) {
        short re = p[i].revents;
        if (re == 0) continue;
        int m = 0;
#ifdef _WIN32
        if (re & (POLLRDNORM | POLLIN)) m |= 1;
        if (re & (POLLWRNORM | POLLOUT)) m |= 2;
#else
        if (re & (POLLIN | POLLPRI)) m |= 1;
        if (re & POLLOUT) m |= 2;
#endif
        if (re & (POLLERR | POLLHUP | POLLNVAL)) m |= 4;
        r->v[r->n].fd = p[i].fd;
        r->v[r->n].events = m;
        r->n += 1;
    }
    free(p);
    return r;
}

tycho_int nbx_count(void *vp) { return ((TyPoll *)vp)->n; }

tycho_int nbx_fd(void *vp, tycho_int i) {
    TyPoll *r = (TyPoll *)vp;
    if (i < 0 || i >= r->n) return -1;
    return r->v[i].fd;
}

tycho_int nbx_events(void *vp, tycho_int i) {
    TyPoll *r = (TyPoll *)vp;
    if (i < 0 || i >= r->n) return 0;
    return r->v[i].events;
}

void nbx_free(void *vp) {
    TyPoll *r = (TyPoll *)vp;
    if (!r) return;
    free(r->v);
    free(r);
}
