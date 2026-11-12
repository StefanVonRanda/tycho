/* core:sync shim -- C11 atomics, pthread mutex/rwlock/condvar on POSIX and the
 * equivalent Win32 primitives on Windows. Every object is malloc'd and returned
 * as an opaque handle; the Tycho side owns it and frees it explicitly after the
 * tasks that use it have been joined. All atomic operations are seq_cst. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE          /* glibc: pthread_rwlock_* under -std=c11 */
#endif
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include "../tycho.h"

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <errno.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#endif

typedef struct { _Atomic uint64_t v; } SyncAtomicInt;
typedef struct { _Atomic int v; } SyncAtomicBool;

void *syncx_atomic_int_new(tycho_int v) {
    SyncAtomicInt *p = (SyncAtomicInt *)malloc(sizeof *p);
    if (!p) return NULL;
    atomic_init(&p->v, (uint64_t)v);
    return p;
}

void syncx_atomic_int_free(void *vp) {
    free(vp);
}

tycho_int syncx_atomic_int_load(void *vp) {
    SyncAtomicInt *p = (SyncAtomicInt *)vp;
    return (tycho_int)atomic_load_explicit(&p->v, memory_order_seq_cst);
}

void syncx_atomic_int_store(void *vp, tycho_int v) {
    SyncAtomicInt *p = (SyncAtomicInt *)vp;
    atomic_store_explicit(&p->v, (uint64_t)v, memory_order_seq_cst);
}

tycho_int syncx_atomic_int_add(void *vp, tycho_int v) {
    SyncAtomicInt *p = (SyncAtomicInt *)vp;
    uint64_t old = atomic_fetch_add_explicit(&p->v, (uint64_t)v, memory_order_seq_cst);
    return (tycho_int)(old + (uint64_t)v);
}

tycho_int syncx_atomic_int_sub(void *vp, tycho_int v) {
    SyncAtomicInt *p = (SyncAtomicInt *)vp;
    uint64_t old = atomic_fetch_sub_explicit(&p->v, (uint64_t)v, memory_order_seq_cst);
    return (tycho_int)(old - (uint64_t)v);
}

tycho_int syncx_atomic_int_swap(void *vp, tycho_int v) {
    SyncAtomicInt *p = (SyncAtomicInt *)vp;
    return (tycho_int)atomic_exchange_explicit(&p->v, (uint64_t)v, memory_order_seq_cst);
}

tycho_int syncx_atomic_int_cas(void *vp, tycho_int expected, tycho_int desired) {
    SyncAtomicInt *p = (SyncAtomicInt *)vp;
    uint64_t old = (uint64_t)expected;
    int ok = atomic_compare_exchange_strong_explicit(
        &p->v, &old, (uint64_t)desired, memory_order_seq_cst, memory_order_seq_cst);
    return ok ? 1 : 0;
}

tycho_int syncx_atomic_int_compare_swap(void *vp, tycho_int expected, tycho_int desired) {
    SyncAtomicInt *p = (SyncAtomicInt *)vp;
    uint64_t old = (uint64_t)expected;
    atomic_compare_exchange_strong_explicit(
        &p->v, &old, (uint64_t)desired, memory_order_seq_cst, memory_order_seq_cst);
    return (tycho_int)old;
}

void *syncx_atomic_bool_new(tycho_int v) {
    SyncAtomicBool *p = (SyncAtomicBool *)malloc(sizeof *p);
    if (!p) return NULL;
    atomic_init(&p->v, v ? 1 : 0);
    return p;
}

void syncx_atomic_bool_free(void *vp) {
    free(vp);
}

tycho_int syncx_atomic_bool_load(void *vp) {
    SyncAtomicBool *p = (SyncAtomicBool *)vp;
    return atomic_load_explicit(&p->v, memory_order_seq_cst) ? 1 : 0;
}

void syncx_atomic_bool_store(void *vp, tycho_int v) {
    SyncAtomicBool *p = (SyncAtomicBool *)vp;
    atomic_store_explicit(&p->v, v ? 1 : 0, memory_order_seq_cst);
}

tycho_int syncx_atomic_bool_swap(void *vp, tycho_int v) {
    SyncAtomicBool *p = (SyncAtomicBool *)vp;
    int old = atomic_exchange_explicit(&p->v, v ? 1 : 0, memory_order_seq_cst);
    return old ? 1 : 0;
}

tycho_int syncx_atomic_bool_cas(void *vp, tycho_int expected, tycho_int desired) {
    SyncAtomicBool *p = (SyncAtomicBool *)vp;
    int old = expected ? 1 : 0;
    int ok = atomic_compare_exchange_strong_explicit(
        &p->v, &old, desired ? 1 : 0, memory_order_seq_cst, memory_order_seq_cst);
    return ok ? 1 : 0;
}

#ifdef _WIN32

typedef struct { SRWLOCK l; } SyncMutex;
typedef struct { SRWLOCK l; } SyncRwLock;
typedef struct { CRITICAL_SECTION cs; CONDITION_VARIABLE cv; tycho_int n; } SyncWaitGroup;

void *syncx_mutex_new(void) {
    SyncMutex *p = (SyncMutex *)malloc(sizeof *p);
    if (!p) return NULL;
    InitializeSRWLock(&p->l);
    return p;
}

tycho_int syncx_mutex_free(void *vp) {
    free(vp);
    return 0;
}

tycho_int syncx_mutex_lock(void *vp) {
    SyncMutex *p = (SyncMutex *)vp;
    AcquireSRWLockExclusive(&p->l);
    return 0;
}

tycho_int syncx_mutex_trylock(void *vp) {
    SyncMutex *p = (SyncMutex *)vp;
    return TryAcquireSRWLockExclusive(&p->l) ? 1 : 0;
}

tycho_int syncx_mutex_unlock(void *vp) {
    SyncMutex *p = (SyncMutex *)vp;
    ReleaseSRWLockExclusive(&p->l);
    return 0;
}

void *syncx_rwlock_new(void) {
    SyncRwLock *p = (SyncRwLock *)malloc(sizeof *p);
    if (!p) return NULL;
    InitializeSRWLock(&p->l);
    return p;
}

tycho_int syncx_rwlock_free(void *vp) {
    free(vp);
    return 0;
}

tycho_int syncx_rwlock_read_lock(void *vp) {
    SyncRwLock *p = (SyncRwLock *)vp;
    AcquireSRWLockShared(&p->l);
    return 0;
}

tycho_int syncx_rwlock_try_read_lock(void *vp) {
    SyncRwLock *p = (SyncRwLock *)vp;
    return TryAcquireSRWLockShared(&p->l) ? 1 : 0;
}

tycho_int syncx_rwlock_read_unlock(void *vp) {
    SyncRwLock *p = (SyncRwLock *)vp;
    ReleaseSRWLockShared(&p->l);
    return 0;
}

tycho_int syncx_rwlock_write_lock(void *vp) {
    SyncRwLock *p = (SyncRwLock *)vp;
    AcquireSRWLockExclusive(&p->l);
    return 0;
}

tycho_int syncx_rwlock_try_write_lock(void *vp) {
    SyncRwLock *p = (SyncRwLock *)vp;
    return TryAcquireSRWLockExclusive(&p->l) ? 1 : 0;
}

tycho_int syncx_rwlock_write_unlock(void *vp) {
    SyncRwLock *p = (SyncRwLock *)vp;
    ReleaseSRWLockExclusive(&p->l);
    return 0;
}

void *syncx_waitgroup_new(void) {
    SyncWaitGroup *p = (SyncWaitGroup *)malloc(sizeof *p);
    if (!p) return NULL;
    InitializeCriticalSection(&p->cs);
    InitializeConditionVariable(&p->cv);
    p->n = 0;
    return p;
}

tycho_int syncx_waitgroup_free(void *vp) {
    SyncWaitGroup *p = (SyncWaitGroup *)vp;
    if (!p) return 0;
    DeleteCriticalSection(&p->cs);
    free(p);
    return 0;
}

tycho_int syncx_waitgroup_add(void *vp, tycho_int n) {
    SyncWaitGroup *p = (SyncWaitGroup *)vp;
    EnterCriticalSection(&p->cs);
    p->n += n;
    LeaveCriticalSection(&p->cs);
    return 0;
}

tycho_int syncx_waitgroup_done(void *vp) {
    SyncWaitGroup *p = (SyncWaitGroup *)vp;
    EnterCriticalSection(&p->cs);
    if (p->n <= 0) {
        LeaveCriticalSection(&p->cs);
        return -1;
    }
    p->n -= 1;
    if (p->n == 0) WakeAllConditionVariable(&p->cv);
    LeaveCriticalSection(&p->cs);
    return 0;
}

tycho_int syncx_waitgroup_wait(void *vp) {
    SyncWaitGroup *p = (SyncWaitGroup *)vp;
    EnterCriticalSection(&p->cs);
    while (p->n > 0) SleepConditionVariableCS(&p->cv, &p->cs, INFINITE);
    LeaveCriticalSection(&p->cs);
    return 0;
}

#else

typedef struct { pthread_mutex_t m; } SyncMutex;
typedef struct { pthread_rwlock_t l; } SyncRwLock;
typedef struct { pthread_mutex_t m; pthread_cond_t c; tycho_int n; } SyncWaitGroup;

void *syncx_mutex_new(void) {
    SyncMutex *p = (SyncMutex *)malloc(sizeof *p);
    if (!p) return NULL;
    if (pthread_mutex_init(&p->m, NULL) != 0) { free(p); return NULL; }
    return p;
}

tycho_int syncx_mutex_free(void *vp) {
    SyncMutex *p = (SyncMutex *)vp;
    if (!p) return 0;
    if (pthread_mutex_destroy(&p->m) != 0) return -1;
    free(p);
    return 0;
}

tycho_int syncx_mutex_lock(void *vp) {
    SyncMutex *p = (SyncMutex *)vp;
    return pthread_mutex_lock(&p->m) == 0 ? 0 : -1;
}

tycho_int syncx_mutex_trylock(void *vp) {
    SyncMutex *p = (SyncMutex *)vp;
    return pthread_mutex_trylock(&p->m) == 0 ? 1 : 0;
}

tycho_int syncx_mutex_unlock(void *vp) {
    SyncMutex *p = (SyncMutex *)vp;
    return pthread_mutex_unlock(&p->m) == 0 ? 0 : -1;
}

void *syncx_rwlock_new(void) {
    SyncRwLock *p = (SyncRwLock *)malloc(sizeof *p);
    if (!p) return NULL;
    if (pthread_rwlock_init(&p->l, NULL) != 0) { free(p); return NULL; }
    return p;
}

tycho_int syncx_rwlock_free(void *vp) {
    SyncRwLock *p = (SyncRwLock *)vp;
    if (!p) return 0;
    if (pthread_rwlock_destroy(&p->l) != 0) return -1;
    free(p);
    return 0;
}

tycho_int syncx_rwlock_read_lock(void *vp) {
    SyncRwLock *p = (SyncRwLock *)vp;
    return pthread_rwlock_rdlock(&p->l) == 0 ? 0 : -1;
}

tycho_int syncx_rwlock_try_read_lock(void *vp) {
    SyncRwLock *p = (SyncRwLock *)vp;
    return pthread_rwlock_tryrdlock(&p->l) == 0 ? 1 : 0;
}

tycho_int syncx_rwlock_read_unlock(void *vp) {
    SyncRwLock *p = (SyncRwLock *)vp;
    return pthread_rwlock_unlock(&p->l) == 0 ? 0 : -1;
}

tycho_int syncx_rwlock_write_lock(void *vp) {
    SyncRwLock *p = (SyncRwLock *)vp;
    return pthread_rwlock_wrlock(&p->l) == 0 ? 0 : -1;
}

tycho_int syncx_rwlock_try_write_lock(void *vp) {
    SyncRwLock *p = (SyncRwLock *)vp;
    return pthread_rwlock_trywrlock(&p->l) == 0 ? 1 : 0;
}

tycho_int syncx_rwlock_write_unlock(void *vp) {
    SyncRwLock *p = (SyncRwLock *)vp;
    return pthread_rwlock_unlock(&p->l) == 0 ? 0 : -1;
}

void *syncx_waitgroup_new(void) {
    SyncWaitGroup *p = (SyncWaitGroup *)malloc(sizeof *p);
    if (!p) return NULL;
    if (pthread_mutex_init(&p->m, NULL) != 0) { free(p); return NULL; }
    if (pthread_cond_init(&p->c, NULL) != 0) {
        pthread_mutex_destroy(&p->m);
        free(p);
        return NULL;
    }
    p->n = 0;
    return p;
}

tycho_int syncx_waitgroup_free(void *vp) {
    SyncWaitGroup *p = (SyncWaitGroup *)vp;
    if (!p) return 0;
    if (pthread_cond_destroy(&p->c) != 0) return -1;
    if (pthread_mutex_destroy(&p->m) != 0) return -1;
    free(p);
    return 0;
}

tycho_int syncx_waitgroup_add(void *vp, tycho_int n) {
    SyncWaitGroup *p = (SyncWaitGroup *)vp;
    if (pthread_mutex_lock(&p->m) != 0) return -1;
    p->n += n;
    if (pthread_mutex_unlock(&p->m) != 0) return -1;
    return 0;
}

tycho_int syncx_waitgroup_done(void *vp) {
    SyncWaitGroup *p = (SyncWaitGroup *)vp;
    if (pthread_mutex_lock(&p->m) != 0) return -1;
    if (p->n <= 0) {
        pthread_mutex_unlock(&p->m);
        return -1;
    }
    p->n -= 1;
    if (p->n == 0) pthread_cond_broadcast(&p->c);
    if (pthread_mutex_unlock(&p->m) != 0) return -1;
    return 0;
}

tycho_int syncx_waitgroup_wait(void *vp) {
    SyncWaitGroup *p = (SyncWaitGroup *)vp;
    if (pthread_mutex_lock(&p->m) != 0) return -1;
    while (p->n > 0) {
        if (pthread_cond_wait(&p->c, &p->m) != 0) {
            pthread_mutex_unlock(&p->m);
            return -1;
        }
    }
    if (pthread_mutex_unlock(&p->m) != 0) return -1;
    return 0;
}

#endif

/* ---- condition variables, semaphores and atomic pointers ---- */
#ifdef _WIN32

typedef struct { CONDITION_VARIABLE cv; } SyncCond;
typedef struct { CRITICAL_SECTION cs; CONDITION_VARIABLE cv; tycho_int n; } SyncSemaphore;
typedef struct { _Atomic(void *) v; } SyncAtomicPtr;

void *syncx_cond_new(void) {
    SyncCond *c = (SyncCond *)malloc(sizeof *c);
    if (!c) return NULL;
    InitializeConditionVariable(&c->cv);
    return c;
}

tycho_int syncx_cond_free(void *vp) {
    free(vp);
    return 0;
}

tycho_int syncx_cond_wait(void *cvp, void *mvp) {
    SyncCond *c = (SyncCond *)cvp;
    SyncMutex *m = (SyncMutex *)mvp;
    return SleepConditionVariableSRW(&c->cv, &m->l, INFINITE, 0) ? 0 : -1;
}

tycho_int syncx_cond_timed_wait(void *cvp, void *mvp, tycho_int ms) {
    SyncCond *c = (SyncCond *)cvp;
    SyncMutex *m = (SyncMutex *)mvp;
    if (ms < 0) return -1;
    return SleepConditionVariableSRW(&c->cv, &m->l, (DWORD)ms, 0) ? 1 : 0;
}

tycho_int syncx_cond_signal(void *vp) {
    SyncCond *c = (SyncCond *)vp;
    WakeConditionVariable(&c->cv);
    return 0;
}

tycho_int syncx_cond_broadcast(void *vp) {
    SyncCond *c = (SyncCond *)vp;
    WakeAllConditionVariable(&c->cv);
    return 0;
}

void *syncx_semaphore_new(tycho_int n) {
    if (n < 0) return NULL;
    SyncSemaphore *s = (SyncSemaphore *)malloc(sizeof *s);
    if (!s) return NULL;
    InitializeCriticalSection(&s->cs);
    InitializeConditionVariable(&s->cv);
    s->n = n;
    return s;
}

tycho_int syncx_semaphore_free(void *vp) {
    SyncSemaphore *s = (SyncSemaphore *)vp;
    if (!s) return 0;
    DeleteCriticalSection(&s->cs);
    free(s);
    return 0;
}

tycho_int syncx_semaphore_wait(void *vp) {
    SyncSemaphore *s = (SyncSemaphore *)vp;
    EnterCriticalSection(&s->cs);
    while (s->n == 0) SleepConditionVariableCS(&s->cv, &s->cs, INFINITE);
    s->n -= 1;
    LeaveCriticalSection(&s->cs);
    return 0;
}

tycho_int syncx_semaphore_try_wait(void *vp) {
    SyncSemaphore *s = (SyncSemaphore *)vp;
    EnterCriticalSection(&s->cs);
    if (s->n == 0) {
        LeaveCriticalSection(&s->cs);
        return 0;
    }
    s->n -= 1;
    LeaveCriticalSection(&s->cs);
    return 1;
}

tycho_int syncx_semaphore_post(void *vp) {
    SyncSemaphore *s = (SyncSemaphore *)vp;
    EnterCriticalSection(&s->cs);
    s->n += 1;
    WakeConditionVariable(&s->cv);
    LeaveCriticalSection(&s->cs);
    return 0;
}

tycho_int syncx_semaphore_count(void *vp) {
    SyncSemaphore *s = (SyncSemaphore *)vp;
    EnterCriticalSection(&s->cs);
    tycho_int n = s->n;
    LeaveCriticalSection(&s->cs);
    return n;
}

void *syncx_atomic_ptr_new(void *p) {
    SyncAtomicPtr *a = (SyncAtomicPtr *)malloc(sizeof *a);
    if (!a) return NULL;
    atomic_init(&a->v, p);
    return a;
}

void syncx_atomic_ptr_free(void *vp) { free(vp); }

void *syncx_atomic_ptr_load(void *vp) {
    SyncAtomicPtr *a = (SyncAtomicPtr *)vp;
    return atomic_load_explicit(&a->v, memory_order_seq_cst);
}

void syncx_atomic_ptr_store(void *vp, void *v) {
    SyncAtomicPtr *a = (SyncAtomicPtr *)vp;
    atomic_store_explicit(&a->v, v, memory_order_seq_cst);
}

void *syncx_atomic_ptr_swap(void *vp, void *v) {
    SyncAtomicPtr *a = (SyncAtomicPtr *)vp;
    return atomic_exchange_explicit(&a->v, v, memory_order_seq_cst);
}

tycho_int syncx_atomic_ptr_cas(void *vp, void *expected, void *desired) {
    SyncAtomicPtr *a = (SyncAtomicPtr *)vp;
    void *old = expected;
    return atomic_compare_exchange_strong_explicit(
        &a->v, &old, desired, memory_order_seq_cst, memory_order_seq_cst) ? 1 : 0;
}

#else

typedef struct { pthread_cond_t cv; } SyncCond;
typedef struct { pthread_mutex_t m; pthread_cond_t cv; tycho_int n; } SyncSemaphore;
typedef struct { _Atomic(void *) v; } SyncAtomicPtr;

void *syncx_cond_new(void) {
    SyncCond *c = (SyncCond *)malloc(sizeof *c);
    if (!c) return NULL;
    if (pthread_cond_init(&c->cv, NULL) != 0) { free(c); return NULL; }
    return c;
}

tycho_int syncx_cond_free(void *vp) {
    SyncCond *c = (SyncCond *)vp;
    if (!c) return 0;
    if (pthread_cond_destroy(&c->cv) != 0) return -1;
    free(c);
    return 0;
}

tycho_int syncx_cond_wait(void *cvp, void *mvp) {
    SyncCond *c = (SyncCond *)cvp;
    SyncMutex *m = (SyncMutex *)mvp;
    return pthread_cond_wait(&c->cv, &m->m) == 0 ? 0 : -1;
}

tycho_int syncx_cond_timed_wait(void *cvp, void *mvp, tycho_int ms) {
    SyncCond *c = (SyncCond *)cvp;
    SyncMutex *m = (SyncMutex *)mvp;
    if (ms < 0) return -1;
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return -1;
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(&c->cv, &m->m, &ts) == 0 ? 1 : 0;
}

tycho_int syncx_cond_signal(void *vp) {
    SyncCond *c = (SyncCond *)vp;
    return pthread_cond_signal(&c->cv) == 0 ? 0 : -1;
}

tycho_int syncx_cond_broadcast(void *vp) {
    SyncCond *c = (SyncCond *)vp;
    return pthread_cond_broadcast(&c->cv) == 0 ? 0 : -1;
}

void *syncx_semaphore_new(tycho_int n) {
    if (n < 0) return NULL;
    SyncSemaphore *s = (SyncSemaphore *)malloc(sizeof *s);
    if (!s) return NULL;
    if (pthread_mutex_init(&s->m, NULL) != 0) { free(s); return NULL; }
    if (pthread_cond_init(&s->cv, NULL) != 0) {
        pthread_mutex_destroy(&s->m);
        free(s);
        return NULL;
    }
    s->n = n;
    return s;
}

tycho_int syncx_semaphore_free(void *vp) {
    SyncSemaphore *s = (SyncSemaphore *)vp;
    if (!s) return 0;
    if (pthread_cond_destroy(&s->cv) != 0) return -1;
    if (pthread_mutex_destroy(&s->m) != 0) return -1;
    free(s);
    return 0;
}

tycho_int syncx_semaphore_wait(void *vp) {
    SyncSemaphore *s = (SyncSemaphore *)vp;
    if (pthread_mutex_lock(&s->m) != 0) return -1;
    while (s->n == 0) {
        if (pthread_cond_wait(&s->cv, &s->m) != 0) {
            pthread_mutex_unlock(&s->m);
            return -1;
        }
    }
    s->n -= 1;
    if (pthread_mutex_unlock(&s->m) != 0) return -1;
    return 0;
}

tycho_int syncx_semaphore_try_wait(void *vp) {
    SyncSemaphore *s = (SyncSemaphore *)vp;
    if (pthread_mutex_lock(&s->m) != 0) return 0;
    if (s->n == 0) {
        pthread_mutex_unlock(&s->m);
        return 0;
    }
    s->n -= 1;
    pthread_mutex_unlock(&s->m);
    return 1;
}

tycho_int syncx_semaphore_post(void *vp) {
    SyncSemaphore *s = (SyncSemaphore *)vp;
    if (pthread_mutex_lock(&s->m) != 0) return -1;
    s->n += 1;
    if (pthread_cond_signal(&s->cv) != 0) {
        pthread_mutex_unlock(&s->m);
        return -1;
    }
    if (pthread_mutex_unlock(&s->m) != 0) return -1;
    return 0;
}

tycho_int syncx_semaphore_count(void *vp) {
    SyncSemaphore *s = (SyncSemaphore *)vp;
    if (pthread_mutex_lock(&s->m) != 0) return -1;
    tycho_int n = s->n;
    pthread_mutex_unlock(&s->m);
    return n;
}

void *syncx_atomic_ptr_new(void *p) {
    SyncAtomicPtr *a = (SyncAtomicPtr *)malloc(sizeof *a);
    if (!a) return NULL;
    atomic_init(&a->v, p);
    return a;
}

void syncx_atomic_ptr_free(void *vp) { free(vp); }

void *syncx_atomic_ptr_load(void *vp) {
    SyncAtomicPtr *a = (SyncAtomicPtr *)vp;
    return atomic_load_explicit(&a->v, memory_order_seq_cst);
}

void syncx_atomic_ptr_store(void *vp, void *v) {
    SyncAtomicPtr *a = (SyncAtomicPtr *)vp;
    atomic_store_explicit(&a->v, v, memory_order_seq_cst);
}

void *syncx_atomic_ptr_swap(void *vp, void *v) {
    SyncAtomicPtr *a = (SyncAtomicPtr *)vp;
    return atomic_exchange_explicit(&a->v, v, memory_order_seq_cst);
}

tycho_int syncx_atomic_ptr_cas(void *vp, void *expected, void *desired) {
    SyncAtomicPtr *a = (SyncAtomicPtr *)vp;
    void *old = expected;
    return atomic_compare_exchange_strong_explicit(
        &a->v, &old, desired, memory_order_seq_cst, memory_order_seq_cst) ? 1 : 0;
}

#endif

/* ---- barrier and recursive mutex ---- */
#ifdef _WIN32

typedef struct {
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
    tycho_int n;
    tycho_int count;
    tycho_int generation;
} SyncBarrier;
typedef struct { CRITICAL_SECTION cs; } SyncRecursiveMutex;

void *syncx_barrier_new(tycho_int n) {
    if (n < 1) return NULL;
    SyncBarrier *b = (SyncBarrier *)malloc(sizeof *b);
    if (!b) return NULL;
    InitializeCriticalSection(&b->cs);
    InitializeConditionVariable(&b->cv);
    b->n = n;
    b->count = 0;
    b->generation = 0;
    return b;
}

tycho_int syncx_barrier_free(void *vp) {
    SyncBarrier *b = (SyncBarrier *)vp;
    if (!b) return 0;
    DeleteCriticalSection(&b->cs);
    free(b);
    return 0;
}

tycho_int syncx_barrier_wait(void *vp) {
    SyncBarrier *b = (SyncBarrier *)vp;
    EnterCriticalSection(&b->cs);
    tycho_int gen = b->generation;
    b->count += 1;
    if (b->count == b->n) {
        b->count = 0;
        b->generation += 1;
        WakeAllConditionVariable(&b->cv);
        LeaveCriticalSection(&b->cs);
        return 1;
    }
    while (gen == b->generation) SleepConditionVariableCS(&b->cv, &b->cs, INFINITE);
    LeaveCriticalSection(&b->cs);
    return 0;
}

void *syncx_recursive_mutex_new(void) {
    SyncRecursiveMutex *m = (SyncRecursiveMutex *)malloc(sizeof *m);
    if (!m) return NULL;
    InitializeCriticalSection(&m->cs);
    return m;
}

tycho_int syncx_recursive_mutex_free(void *vp) {
    SyncRecursiveMutex *m = (SyncRecursiveMutex *)vp;
    if (!m) return 0;
    DeleteCriticalSection(&m->cs);
    free(m);
    return 0;
}

tycho_int syncx_recursive_mutex_lock(void *vp) {
    EnterCriticalSection(&((SyncRecursiveMutex *)vp)->cs);
    return 0;
}

tycho_int syncx_recursive_mutex_trylock(void *vp) {
    return TryEnterCriticalSection(&((SyncRecursiveMutex *)vp)->cs) ? 1 : 0;
}

tycho_int syncx_recursive_mutex_unlock(void *vp) {
    LeaveCriticalSection(&((SyncRecursiveMutex *)vp)->cs);
    return 0;
}

#else

typedef struct { pthread_mutex_t m; } SyncRecursiveMutex;

#if defined(__APPLE__) && TARGET_OS_IPHONE
/* iOS does not ship pthread_barrier_t. A mutex/condvar generation barrier has
 * the same contract: every caller blocks until the last arrival releases the
 * generation, and exactly one caller returns 1. */
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t cv;
    tycho_int n;
    tycho_int count;
    tycho_int generation;
} SyncBarrier;

void *syncx_barrier_new(tycho_int n) {
    if (n < 1) return NULL;
    SyncBarrier *b = (SyncBarrier *)malloc(sizeof *b);
    if (!b) return NULL;
    if (pthread_mutex_init(&b->m, NULL) != 0) { free(b); return NULL; }
    if (pthread_cond_init(&b->cv, NULL) != 0) {
        pthread_mutex_destroy(&b->m);
        free(b);
        return NULL;
    }
    b->n = n;
    b->count = 0;
    b->generation = 0;
    return b;
}

tycho_int syncx_barrier_free(void *vp) {
    SyncBarrier *b = (SyncBarrier *)vp;
    if (!b) return 0;
    pthread_cond_destroy(&b->cv);
    pthread_mutex_destroy(&b->m);
    free(b);
    return 0;
}

tycho_int syncx_barrier_wait(void *vp) {
    SyncBarrier *b = (SyncBarrier *)vp;
    if (pthread_mutex_lock(&b->m) != 0) return -1;
    tycho_int gen = b->generation;
    b->count += 1;
    if (b->count == b->n) {
        b->count = 0;
        b->generation += 1;
        pthread_cond_broadcast(&b->cv);
        pthread_mutex_unlock(&b->m);
        return 1;
    }
    while (gen == b->generation) {
        if (pthread_cond_wait(&b->cv, &b->m) != 0) {
            pthread_mutex_unlock(&b->m);
            return -1;
        }
    }
    pthread_mutex_unlock(&b->m);
    return 0;
}
#else
typedef struct { pthread_barrier_t b; } SyncBarrier;

void *syncx_barrier_new(tycho_int n) {
    if (n < 1) return NULL;
    SyncBarrier *b = (SyncBarrier *)malloc(sizeof *b);
    if (!b) return NULL;
    if (pthread_barrier_init(&b->b, NULL, (unsigned)n) != 0) { free(b); return NULL; }
    return b;
}

tycho_int syncx_barrier_free(void *vp) {
    SyncBarrier *b = (SyncBarrier *)vp;
    if (!b) return 0;
    if (pthread_barrier_destroy(&b->b) != 0) return -1;
    free(b);
    return 0;
}

tycho_int syncx_barrier_wait(void *vp) {
    SyncBarrier *b = (SyncBarrier *)vp;
    int rc = pthread_barrier_wait(&b->b);
    if (rc == PTHREAD_BARRIER_SERIAL_THREAD) return 1;
    return rc == 0 ? 0 : -1;
}
#endif

void *syncx_recursive_mutex_new(void) {
    SyncRecursiveMutex *m = (SyncRecursiveMutex *)malloc(sizeof *m);
    if (!m) return NULL;
    pthread_mutexattr_t a;
    if (pthread_mutexattr_init(&a) != 0) { free(m); return NULL; }
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    if (pthread_mutex_init(&m->m, &a) != 0) {
        pthread_mutexattr_destroy(&a);
        free(m);
        return NULL;
    }
    pthread_mutexattr_destroy(&a);
    return m;
}

tycho_int syncx_recursive_mutex_free(void *vp) {
    SyncRecursiveMutex *m = (SyncRecursiveMutex *)vp;
    if (!m) return 0;
    if (pthread_mutex_destroy(&m->m) != 0) return -1;
    free(m);
    return 0;
}

tycho_int syncx_recursive_mutex_lock(void *vp) {
    return pthread_mutex_lock(&((SyncRecursiveMutex *)vp)->m) == 0 ? 0 : -1;
}

tycho_int syncx_recursive_mutex_trylock(void *vp) {
    return pthread_mutex_trylock(&((SyncRecursiveMutex *)vp)->m) == 0 ? 1 : 0;
}

tycho_int syncx_recursive_mutex_unlock(void *vp) {
    return pthread_mutex_unlock(&((SyncRecursiveMutex *)vp)->m) == 0 ? 0 : -1;
}

#endif
