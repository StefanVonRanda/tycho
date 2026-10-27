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
 *
 * N=0 means every matching call. Counters are per process.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
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

static long read_short = -1, read_eintr = -1, read_fail = -1;
static long write_short = -1, write_eintr = -1, write_fail = -1;
static long pread_short = -1, pread_eintr = -1, pread_fail = -1;
static long pwrite_short = -1, pwrite_eintr = -1, pwrite_fail = -1;
static long open_eintr = -1, open_fail = -1;
static int active = 1;

static atomic_long read_n;
static atomic_long write_n;
static atomic_long pread_n;
static atomic_long pwrite_n;
static atomic_long open_n;

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
    const char *only = getenv("FI_ONLY_PID");
    if (only && *only && (long)getpid() != strtol(only, NULL, 10)) active = 0;
}

#define FI_ACTIVE() (active && (read_short >= 0 || read_eintr >= 0 || read_fail >= 0 || \
    write_short >= 0 || write_eintr >= 0 || write_fail >= 0 || \
    pread_short >= 0 || pread_eintr >= 0 || pread_fail >= 0 || \
    pwrite_short >= 0 || pwrite_eintr >= 0 || pwrite_fail >= 0 || \
    open_eintr >= 0 || open_fail >= 0))

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
