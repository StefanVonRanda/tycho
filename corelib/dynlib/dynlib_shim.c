#include <string.h>
#include <stdint.h>
#include "../tycho.h"

#ifdef _WIN32
#include <windows.h>
void *dlx_open(const char *path) { return (void *)LoadLibraryA(path); }
void *dlx_sym(void *h, const char *name) { return (void *)GetProcAddress((HMODULE)h, name); }
void dlx_close(void *h) { if (h) FreeLibrary((HMODULE)h); }
#else
#include <dlfcn.h>
void *dlx_open(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
void *dlx_sym(void *h, const char *name) { return dlsym(h, name); }
void dlx_close(void *h) { if (h) dlclose(h); }
#endif

/* Typed invocation of a C function pointer returned by dlx_sym. These call
 * into C only; Tycho function values are not passed to C. */
typedef int32_t (*DlxI32_0)(void);
typedef int32_t (*DlxI32_1)(int32_t);
typedef int32_t (*DlxI32_2)(int32_t, int32_t);
typedef int32_t (*DlxI32_3)(int32_t, int32_t, int32_t);
typedef int64_t (*DlxI64_1)(int64_t);
typedef double (*DlxF64_1)(double);
typedef int32_t (*DlxI32Str)(const char *);
typedef int32_t (*DlxI32StrStr)(const char *, const char *);
typedef void *(*DlxPtrI64)(int64_t);
typedef void (*DlxVoidPtr)(void *);
typedef void *(*DlxPtr1)(void *);

tycho_int dlx_call_i32_0(void *fp) {
    if (!fp) return 0;
    DlxI32_0 f; memcpy(&f, &fp, sizeof f);
    return (tycho_int)f();
}

tycho_int dlx_call_i32_1(void *fp, tycho_int a) {
    if (!fp) return 0;
    DlxI32_1 f; memcpy(&f, &fp, sizeof f);
    return (tycho_int)f((int32_t)a);
}

tycho_int dlx_call_i32_2(void *fp, tycho_int a, tycho_int b) {
    if (!fp) return 0;
    DlxI32_2 f; memcpy(&f, &fp, sizeof f);
    return (tycho_int)f((int32_t)a, (int32_t)b);
}

tycho_int dlx_call_i32_3(void *fp, tycho_int a, tycho_int b, tycho_int c) {
    if (!fp) return 0;
    DlxI32_3 f; memcpy(&f, &fp, sizeof f);
    return (tycho_int)f((int32_t)a, (int32_t)b, (int32_t)c);
}

tycho_int dlx_call_i64_1(void *fp, tycho_int a) {
    if (!fp) return 0;
    DlxI64_1 f; memcpy(&f, &fp, sizeof f);
    return (tycho_int)f((int64_t)a);
}

double dlx_call_f64_1(void *fp, double a) {
    if (!fp) return 0.0;
    DlxF64_1 f; memcpy(&f, &fp, sizeof f);
    return f(a);
}

tycho_int dlx_call_i32_str(void *fp, const char *s) {
    if (!fp || !s) return 0;
    DlxI32Str f; memcpy(&f, &fp, sizeof f);
    return (tycho_int)f(s);
}

tycho_int dlx_call_i32_str_str(void *fp, const char *a, const char *b) {
    if (!fp || !a || !b) return 0;
    DlxI32StrStr f; memcpy(&f, &fp, sizeof f);
    return (tycho_int)f(a, b);
}

void *dlx_call_ptr_i64(void *fp, tycho_int n) {
    if (!fp) return NULL;
    DlxPtrI64 f; memcpy(&f, &fp, sizeof f);
    return f((int64_t)n);
}

void dlx_call_void_ptr(void *fp, void *p) {
    if (!fp) return;
    DlxVoidPtr f; memcpy(&f, &fp, sizeof f);
    f(p);
}

void *dlx_call_ptr_1(void *fp, void *p) {
    if (!fp) return NULL;
    DlxPtr1 f; memcpy(&f, &fp, sizeof f);
    return f(p);
}
