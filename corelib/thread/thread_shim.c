#include <stdint.h>
#include <stdlib.h>
#include "../tycho.h"

#ifdef _WIN32
#include <windows.h>
tycho_int thx_id(void) { return (tycho_int)GetCurrentThreadId(); }
tycho_int thx_yield(void) { return SwitchToThread() ? 1 : 0; }
#else
#include <pthread.h>
#include <sched.h>
tycho_int thx_id(void) { return (tycho_int)(uintptr_t)pthread_self(); }
tycho_int thx_yield(void) { return sched_yield() == 0 ? 1 : 0; }
#endif

/* Thread-local storage: 32 integer slots per OS thread. The POSIX and Windows
 * implementations both free the slot value when the thread exits. */
#define THX_TLS_MAX 32

#ifdef _WIN32
static DWORD thx_tls_keys[THX_TLS_MAX];
static INIT_ONCE thx_tls_once = INIT_ONCE_STATIC_INIT;

static VOID WINAPI thx_tls_free(PVOID p) { free(p); }

static BOOL CALLBACK thx_tls_init(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once; (void)param; (void)ctx;
    for (int i = 0; i < THX_TLS_MAX; i++) thx_tls_keys[i] = FLS_OUT_OF_INDEXES;
    for (int i = 0; i < THX_TLS_MAX; i++) thx_tls_keys[i] = FlsAlloc(thx_tls_free);
    return TRUE;
}

tycho_int thx_tls_set(tycho_int key, tycho_int value) {
    if (key < 0 || key >= THX_TLS_MAX) return 0;
    InitOnceExecuteOnce(&thx_tls_once, thx_tls_init, NULL, NULL);
    if (thx_tls_keys[key] == FLS_OUT_OF_INDEXES) return 0;
    int *p = (int *)malloc(sizeof *p);
    if (!p) return 0;
    *p = (int)value;
    int *old = (int *)FlsGetValue(thx_tls_keys[key]);
    if (old) free(old);
    return FlsSetValue(thx_tls_keys[key], p) ? 1 : 0;
}

tycho_int thx_tls_get(tycho_int key) {
    if (key < 0 || key >= THX_TLS_MAX) return 0;
    InitOnceExecuteOnce(&thx_tls_once, thx_tls_init, NULL, NULL);
    if (thx_tls_keys[key] == FLS_OUT_OF_INDEXES) return 0;
    int *p = (int *)FlsGetValue(thx_tls_keys[key]);
    return p ? (tycho_int)*p : 0;
}

#else
static pthread_key_t thx_tls_keys[THX_TLS_MAX];
static pthread_once_t thx_tls_once = PTHREAD_ONCE_INIT;

static void thx_tls_free(void *p) { free(p); }

static void thx_tls_init(void) {
    for (int i = 0; i < THX_TLS_MAX; i++) pthread_key_create(&thx_tls_keys[i], thx_tls_free);
}

tycho_int thx_tls_set(tycho_int key, tycho_int value) {
    if (key < 0 || key >= THX_TLS_MAX) return 0;
    pthread_once(&thx_tls_once, thx_tls_init);
    int *p = (int *)malloc(sizeof *p);
    if (!p) return 0;
    *p = (int)value;
    int *old = (int *)pthread_getspecific(thx_tls_keys[key]);
    if (old) free(old);
    return pthread_setspecific(thx_tls_keys[key], p) == 0 ? 1 : 0;
}

tycho_int thx_tls_get(tycho_int key) {
    if (key < 0 || key >= THX_TLS_MAX) return 0;
    pthread_once(&thx_tls_once, thx_tls_init);
    int *p = (int *)pthread_getspecific(thx_tls_keys[key]);
    return p ? (tycho_int)*p : 0;
}
#endif
