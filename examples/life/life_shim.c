/* Companion C for examples/life/life.hi, linked via `hierc --shim`. Provides the
 * millisecond sleep the animation needs (hier has no sleep builtin). Signature
 * matches hier's extern emission: hier `int` == C `long`. */
#include <unistd.h>

long hier_life_sleep(long ms) {
    if (ms > 0) usleep((useconds_t)(ms * 1000));
    return 0;
}
