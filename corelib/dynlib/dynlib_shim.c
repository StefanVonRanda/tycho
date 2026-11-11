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
