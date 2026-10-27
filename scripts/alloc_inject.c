/* LD_PRELOAD allocation failpoint. Linux/glibc only.
 *
 * FI_MALLOC_FAIL=N, FI_CALLOC_FAIL=N, FI_REALLOC_FAIL=N fail the Nth matching
 * allocation (N=0 = every one). FI_ONLY_PID limits the injection to the process
 * that runs the test, not children it spawns.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __APPLE__
#include <malloc/malloc.h>
static malloc_zone_t *zone;
static void *platform_malloc(size_t n) { return malloc_zone_malloc(zone, n); }
static void *platform_calloc(size_t n, size_t m) { return malloc_zone_calloc(zone, n, m); }
static void *platform_realloc(void *p, size_t n) { return malloc_zone_realloc(zone, p, n); }
#else
extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);
#define platform_malloc __libc_malloc
#define platform_calloc __libc_calloc
#define platform_realloc __libc_realloc
#endif

static long malloc_fail = -1, calloc_fail = -1, realloc_fail = -1;
static long malloc_size = -1;
static int active = 1;
static atomic_long malloc_n, calloc_n, realloc_n;

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
static void ai_init(void) {
#ifdef __APPLE__
    zone = malloc_default_zone();
#endif
    malloc_fail = env_num("FI_MALLOC_FAIL");
    calloc_fail = env_num("FI_CALLOC_FAIL");
    realloc_fail = env_num("FI_REALLOC_FAIL");
    malloc_size = env_num("FI_MALLOC_SIZE");
    const char *only = getenv("FI_ONLY_PID");
    if (only && *only && (long)getpid() != strtol(only, NULL, 10)) active = 0;
}

void *malloc(size_t n) {
    if (active && malloc_size >= 0 && n == (size_t)malloc_size) return NULL;
    if (active && malloc_fail >= 0 && at_call(&malloc_n, malloc_fail)) return NULL;
    return platform_malloc(n);
}

void *calloc(size_t n, size_t m) {
    if (active && calloc_fail >= 0 && at_call(&calloc_n, calloc_fail)) return NULL;
    return platform_calloc(n, m);
}

void *realloc(void *p, size_t n) {
    if (active && realloc_fail >= 0 && at_call(&realloc_n, realloc_fail)) return NULL;
    return platform_realloc(p, n);
}
