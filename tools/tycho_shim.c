/* Companion C for tools/tycho.ty (the `tycho` daily-driver CLI), linked via
 * `tychoc --shim`. These wrap libc calls the FFI can't reach directly: a bare
 * `extern fn system(...)` emits `extern long system(char*)`, which clashes with
 * stdlib.h's `int system(const char*)` in the compiler preamble. Routing through
 * our own symbols (in no standard header) sidesteps the clash. Signatures match
 * tycho's extern emission: tycho `int` == C `long`, tycho `string` == `char*`. */
#include <stdlib.h>
#include <unistd.h>

/* Run a command line through the shell; returns the raw wait status (0 == ok). */
long tycho_run(char *cmd) { return (long)system(cmd); }

/* Sleep for `ms` milliseconds (for `tycho watch`'s poll loop). */
long tycho_sleep_ms(long ms) {
    if (ms > 0) usleep((useconds_t)(ms * 1000));
    return 0;
}
