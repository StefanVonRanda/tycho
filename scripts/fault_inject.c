/* LD_PRELOAD fault injector for the corelib I/O shims.
 *
 * Reads and writes can be made SHORT (one byte) or fail with EINTR/EIO at the
 * Nth call, so loops that assume a full transfer or forget to retry are visible
 * without patching the shim. Build: see scripts/fault_inject.sh.
 *
 *   FI_READ_SHORT=N    FI_READ_EINTR=N    FI_READ_FAIL=N
 *   FI_WRITE_SHORT=N   FI_WRITE_EINTR=N   FI_WRITE_FAIL=N
 *   FI_PREAD_SHORT=N   FI_PREAD_EINTR=N   FI_PREAD_FAIL=N
 *   FI_PWRITE_SHORT=N  FI_PWRITE_EINTR=N  FI_PWRITE_FAIL=N
 *   FI_OPEN_EINTR=N    FI_OPEN_FAIL=N
 *   FI_PIPE_FAIL=N     FI_PIPE_EVERY=N  (Nth pipe, or every Nth pipe)
 *   FI_SOCKET_FAIL=N   FI_ACCEPT_FAIL=N   FI_CONNECT_FAIL=N
 *   FI_POLL_FAIL=N     FI_FSYNC_FAIL=N
 *   FI_RENAME_FAIL=N   FI_REMOVE_FAIL=N   FI_MKDIR_FAIL=N
 *
 * N=0 means every matching call. Counters are per process.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

static ssize_t (*real_read)(int, void *, size_t);
static ssize_t (*real_write)(int, const void *, size_t);
static ssize_t (*real_pread)(int, void *, size_t, off_t);
static ssize_t (*real_pwrite)(int, const void *, size_t, off_t);
static int (*real_open)(const char *, int, ...);
static int (*real_open64)(const char *, int, ...);
static int (*real_openat)(int, const char *, int, ...);
static int (*real_pipe)(int[2]);
static int (*real_pipe2)(int[2], int);
static int (*real_socket)(int, int, int);
static int (*real_accept)(int, struct sockaddr *, socklen_t *);
static int (*real_accept4)(int, struct sockaddr *, socklen_t *, int);
static int (*real_connect)(int, const struct sockaddr *, socklen_t);
static int (*real_poll)(struct pollfd *, nfds_t, int);
static int (*real_fsync)(int);
static int (*real_rename)(const char *, const char *);
static int (*real_remove)(const char *);
static int (*real_mkdir)(const char *, mode_t);

static long read_short = -1, read_eintr = -1, read_fail = -1;
static long write_short = -1, write_eintr = -1, write_fail = -1;
static long pread_short = -1, pread_eintr = -1, pread_fail = -1;
static long pwrite_short = -1, pwrite_eintr = -1, pwrite_fail = -1;
static long open_eintr = -1, open_fail = -1;
static long pipe_fail = -1, pipe_every = -1;
static long socket_fail = -1, accept_fail = -1, connect_fail = -1;
static long poll_fail = -1, fsync_fail = -1, rename_fail = -1;
static long remove_fail = -1, mkdir_fail = -1;
static int active = 1;

static atomic_long read_n;
static atomic_long write_n;
static atomic_long pread_n;
static atomic_long pwrite_n;
static atomic_long open_n;
static atomic_long pipe_n, socket_n, accept_n, connect_n, poll_n, fsync_n;
static atomic_long rename_n, remove_n, mkdir_n;

static long env_num(const char *name) {
    const char *v = getenv(name);
    if (!v || !*v) return -1;
    return strtol(v, NULL, 10);
}

static int at_call(atomic_long *counter, long n) {
    long c = atomic_fetch_add(counter, 1) + 1;
    return n == 0 || c == n;
}

__attribute__((constructor))
static void fi_init(void) {
    real_read = dlsym(RTLD_NEXT, "read");
    real_write = dlsym(RTLD_NEXT, "write");
    real_pread = dlsym(RTLD_NEXT, "pread");
    real_pwrite = dlsym(RTLD_NEXT, "pwrite");
    real_open = dlsym(RTLD_NEXT, "open");
    real_open64 = dlsym(RTLD_NEXT, "open64");
    real_openat = dlsym(RTLD_NEXT, "openat");
    real_pipe = dlsym(RTLD_NEXT, "pipe");
    real_pipe2 = dlsym(RTLD_NEXT, "pipe2");
    real_socket = dlsym(RTLD_NEXT, "socket");
    real_accept = dlsym(RTLD_NEXT, "accept");
    real_accept4 = dlsym(RTLD_NEXT, "accept4");
    real_connect = dlsym(RTLD_NEXT, "connect");
    real_poll = dlsym(RTLD_NEXT, "poll");
    real_fsync = dlsym(RTLD_NEXT, "fsync");
    real_rename = dlsym(RTLD_NEXT, "rename");
    real_remove = dlsym(RTLD_NEXT, "remove");
    real_mkdir = dlsym(RTLD_NEXT, "mkdir");
    read_short = env_num("FI_READ_SHORT");
    read_eintr = env_num("FI_READ_EINTR");
    read_fail = env_num("FI_READ_FAIL");
    write_short = env_num("FI_WRITE_SHORT");
    write_eintr = env_num("FI_WRITE_EINTR");
    write_fail = env_num("FI_WRITE_FAIL");
    pread_short = env_num("FI_PREAD_SHORT");
    pread_eintr = env_num("FI_PREAD_EINTR");
    pread_fail = env_num("FI_PREAD_FAIL");
    pwrite_short = env_num("FI_PWRITE_SHORT");
    pwrite_eintr = env_num("FI_PWRITE_EINTR");
    pwrite_fail = env_num("FI_PWRITE_FAIL");
    open_eintr = env_num("FI_OPEN_EINTR");
    open_fail = env_num("FI_OPEN_FAIL");
    pipe_fail = env_num("FI_PIPE_FAIL");
    pipe_every = env_num("FI_PIPE_EVERY");
    socket_fail = env_num("FI_SOCKET_FAIL");
    accept_fail = env_num("FI_ACCEPT_FAIL");
    connect_fail = env_num("FI_CONNECT_FAIL");
    poll_fail = env_num("FI_POLL_FAIL");
    fsync_fail = env_num("FI_FSYNC_FAIL");
    rename_fail = env_num("FI_RENAME_FAIL");
    remove_fail = env_num("FI_REMOVE_FAIL");
    mkdir_fail = env_num("FI_MKDIR_FAIL");
    const char *only = getenv("FI_ONLY_PID");
    if (only && *only && (long)getpid() != strtol(only, NULL, 10)) active = 0;
}

#define FI_ACTIVE() (active && (read_short >= 0 || read_eintr >= 0 || read_fail >= 0 || \
    write_short >= 0 || write_eintr >= 0 || write_fail >= 0 || \
    pread_short >= 0 || pread_eintr >= 0 || pread_fail >= 0 || \
    pwrite_short >= 0 || pwrite_eintr >= 0 || pwrite_fail >= 0 || \
    open_eintr >= 0 || open_fail >= 0 || pipe_fail >= 0 || pipe_every >= 0 || \
    socket_fail >= 0 || accept_fail >= 0 || connect_fail >= 0 || poll_fail >= 0 || \
    fsync_fail >= 0 || rename_fail >= 0 || remove_fail >= 0 || mkdir_fail >= 0))

ssize_t read(int fd, void *buf, size_t count) {
    if (!FI_ACTIVE()) return real_read(fd, buf, count);
    if (read_eintr >= 0 && at_call(&read_n, read_eintr)) {
        errno = EINTR;
        return -1;
    }
    if (read_fail >= 0 && at_call(&read_n, read_fail)) {
        errno = EIO;
        return -1;
    }
    if (read_short >= 0 && count > 1 && at_call(&read_n, read_short))
        return real_read(fd, buf, 1);
    return real_read(fd, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count) {
    if (!FI_ACTIVE()) return real_write(fd, buf, count);
    if (write_eintr >= 0 && at_call(&write_n, write_eintr)) {
        errno = EINTR;
        return -1;
    }
    if (write_fail >= 0 && at_call(&write_n, write_fail)) {
        errno = EIO;
        return -1;
    }
    if (write_short >= 0 && count > 1 && at_call(&write_n, write_short))
        return real_write(fd, buf, 1);
    return real_write(fd, buf, count);
}

ssize_t pread(int fd, void *buf, size_t count, off_t off) {
    if (!FI_ACTIVE()) return real_pread(fd, buf, count, off);
    if (pread_eintr >= 0 && at_call(&pread_n, pread_eintr)) {
        errno = EINTR;
        return -1;
    }
    if (pread_fail >= 0 && at_call(&pread_n, pread_fail)) {
        errno = EIO;
        return -1;
    }
    if (pread_short >= 0 && count > 1 && at_call(&pread_n, pread_short))
        return real_pread(fd, buf, 1, off);
    return real_pread(fd, buf, count, off);
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t off) {
    if (!FI_ACTIVE()) return real_pwrite(fd, buf, count, off);
    if (pwrite_eintr >= 0 && at_call(&pwrite_n, pwrite_eintr)) {
        errno = EINTR;
        return -1;
    }
    if (pwrite_fail >= 0 && at_call(&pwrite_n, pwrite_fail)) {
        errno = EIO;
        return -1;
    }
    if (pwrite_short >= 0 && count > 1 && at_call(&pwrite_n, pwrite_short))
        return real_pwrite(fd, buf, 1, off);
    return real_pwrite(fd, buf, count, off);
}

static int pipe_fail_now(atomic_long *c) {
    long n = atomic_fetch_add(c, 1) + 1;
    if (pipe_every > 0 && n % pipe_every == 0) return 1;
    return pipe_fail >= 0 && (pipe_fail == 0 || n == pipe_fail);
}

int pipe(int fds[2]) {
    if (!FI_ACTIVE()) return real_pipe(fds);
    if (pipe_fail_now(&pipe_n)) { errno = EMFILE; return -1; }
    return real_pipe(fds);
}

int pipe2(int fds[2], int flags) {
    if (!FI_ACTIVE()) return real_pipe2(fds, flags);
    if (pipe_fail_now(&pipe_n)) { errno = EMFILE; return -1; }
    return real_pipe2(fds, flags);
}

int socket(int domain, int type, int protocol) {
    if (!FI_ACTIVE()) return real_socket(domain, type, protocol);
    if (socket_fail >= 0 && at_call(&socket_n, socket_fail)) { errno = EACCES; return -1; }
    return real_socket(domain, type, protocol);
}

int accept(int fd, struct sockaddr *addr, socklen_t *len) {
    if (!FI_ACTIVE()) return real_accept(fd, addr, len);
    if (accept_fail >= 0 && at_call(&accept_n, accept_fail)) { errno = EAGAIN; return -1; }
    return real_accept(fd, addr, len);
}

int accept4(int fd, struct sockaddr *addr, socklen_t *len, int flags) {
    if (!FI_ACTIVE()) return real_accept4(fd, addr, len, flags);
    if (accept_fail >= 0 && at_call(&accept_n, accept_fail)) { errno = EAGAIN; return -1; }
    return real_accept4(fd, addr, len, flags);
}

int connect(int fd, const struct sockaddr *addr, socklen_t len) {
    if (!FI_ACTIVE()) return real_connect(fd, addr, len);
    if (connect_fail >= 0 && at_call(&connect_n, connect_fail)) { errno = ECONNREFUSED; return -1; }
    return real_connect(fd, addr, len);
}

int poll(struct pollfd *fds, nfds_t n, int timeout) {
    if (!FI_ACTIVE()) return real_poll(fds, n, timeout);
    if (poll_fail >= 0 && at_call(&poll_n, poll_fail)) { errno = EIO; return -1; }
    return real_poll(fds, n, timeout);
}

int fsync(int fd) {
    if (!FI_ACTIVE()) return real_fsync(fd);
    if (fsync_fail >= 0 && at_call(&fsync_n, fsync_fail)) { errno = EIO; return -1; }
    return real_fsync(fd);
}

int rename(const char *a, const char *b) {
    if (!FI_ACTIVE()) return real_rename(a, b);
    if (rename_fail >= 0 && at_call(&rename_n, rename_fail)) { errno = EACCES; return -1; }
    return real_rename(a, b);
}

int remove(const char *p) {
    if (!FI_ACTIVE()) return real_remove(p);
    if (remove_fail >= 0 && at_call(&remove_n, remove_fail)) { errno = EACCES; return -1; }
    return real_remove(p);
}

int mkdir(const char *p, mode_t m) {
    if (!FI_ACTIVE()) return real_mkdir(p, m);
    if (mkdir_fail >= 0 && at_call(&mkdir_n, mkdir_fail)) { errno = EACCES; return -1; }
    return real_mkdir(p, m);
}

int open(const char *path, int flags, ...) {
    va_list ap;
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if (!FI_ACTIVE()) return real_open(path, flags, mode);
    if (open_eintr >= 0 && at_call(&open_n, open_eintr)) {
        errno = EINTR;
        return -1;
    }
    if (open_fail >= 0 && at_call(&open_n, open_fail)) {
        errno = EACCES;
        return -1;
    }
    return real_open ? real_open(path, flags, mode) : -1;
}

int open64(const char *path, int flags, ...) {
    va_list ap;
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if (!FI_ACTIVE()) return real_open64(path, flags, mode);
    if (open_eintr >= 0 && at_call(&open_n, open_eintr)) {
        errno = EINTR;
        return -1;
    }
    if (open_fail >= 0 && at_call(&open_n, open_fail)) {
        errno = EACCES;
        return -1;
    }
    return real_open64 ? real_open64(path, flags, mode) : -1;
}

int openat(int dirfd, const char *path, int flags, ...) {
    va_list ap;
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if (!FI_ACTIVE()) return real_openat(dirfd, path, flags, mode);
    if (open_eintr >= 0 && at_call(&open_n, open_eintr)) {
        errno = EINTR;
        return -1;
    }
    if (open_fail >= 0 && at_call(&open_n, open_fail)) {
        errno = EACCES;
        return -1;
    }
    return real_openat ? real_openat(dirfd, path, flags, mode) : -1;
}
