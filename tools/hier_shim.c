/* Companion C for tools/hier.hi (the `hier` daily-driver CLI), linked via
 * `hierc --shim`. These wrap libc calls the FFI can't reach directly: a bare
 * `extern fn system(...)` emits `extern long system(char*)`, which clashes with
 * stdlib.h's `int system(const char*)` in the compiler preamble. Routing through
 * our own symbols (in no standard header) sidesteps the clash. Signatures match
 * hier's extern emission: hier `int` == C `long`, hier `string` == `char*`. */
#include <stdlib.h>
#include <unistd.h>

/* Run a command line through the shell; returns the raw wait status (0 == ok). */
long hier_run(char *cmd) { return (long)system(cmd); }

/* Sleep for `ms` milliseconds (for `hier watch`'s poll loop). */
long hier_sleep_ms(long ms) {
    if (ms > 0) usleep((useconds_t)(ms * 1000));
    return 0;
}
