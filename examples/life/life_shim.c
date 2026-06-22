/* Companion C for examples/life/life.ty, linked via `tychoc --shim`. Provides the
 * millisecond sleep the animation needs (tycho has no sleep builtin). Signature
 * matches tycho's extern emission: tycho `int` == C `long`. */
#include <unistd.h>

long tycho_life_sleep(long ms) {
    if (ms > 0) usleep((useconds_t)(ms * 1000));
    return 0;
}
