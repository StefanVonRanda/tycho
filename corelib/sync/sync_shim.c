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
