/* The ABI types a `--shim` C file shares with the code tychoc emits.
 *
 * A shim is its own translation unit, compiled standalone under -std=c11 by
 * `make shim-check` and spliced onto the real cc line as a separate .c file.
 * There is no generated header, so before this file every shim hand-declared
 * `tycho_int` -- 13 of 14 carried the same four guarded lines, and a first-time
 * shim author had to know to copy them (FRICTION #11).
 *
 * Include it relative to the shim's own directory, which needs no -I in either
 * build path:  #include "../tycho.h"
 *
 * The guard is load-bearing: in the real build the generated C also defines
 * this type, and both may reach one translation unit.
 */
#ifndef TYCHO_SHIM_H
#define TYCHO_SHIM_H

#include <stdint.h>

#ifndef TYCHO_INT_T
#define TYCHO_INT_T
typedef int64_t tycho_int;      /* `int` in tycho; int64_t in every emitted C */
#endif

#endif
