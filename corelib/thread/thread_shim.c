#include <stdint.h>
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
