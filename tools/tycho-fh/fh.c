/* The C side of tycho-fh: a FILE* wrapped as a typed handle.
 *
 * WHY THESE COUNTERS EXIST. A handle's whole contract is that its destructor
 * runs EXACTLY ONCE, at every scope exit. Nothing in a program's output shows
 * that: a leaked FILE* and a correctly closed one print the same lines, and a
 * double close is undefined behaviour that usually prints nothing either. So
 * the C side counts, and the gate reads the counters back.
 *
 * `fh_live` is the instrument: it is incremented by a successful open and
 * decremented by a close, so it is 0 after a balanced scope, >0 on a LEAK, and
 * <0 on a DOUBLE FREE. The two failure directions are distinguishable, which
 * matters -- RAII bugs come in both.
 *
 * The names are prefixed because binding libc's own `fopen` directly does not
 * work: the emitted C re-declares the symbol and collides with <stdio.h>
 * (FRICTION, tycho-fh). A shim with distinct names is the supported route, and
 * it is what tests/ffi/demo.c does too.
 */
#include <stdio.h>
#include <stdint.h>

static int64_t g_live = 0;    /* currently-open handles: 0 balanced, >0 leak, <0 double free */
static int64_t g_opens = 0;   /* successful opens, ever */
static int64_t g_closes = 0;  /* closes, ever -- including the null-guarded no-ops */

void *fh_open(char *path, char *mode) {
    FILE *f = fopen(path, mode);
    if (f) { g_live++; g_opens++; }
    return f;
}

/* The destructor. The compiler emits a call to this at scope exit and, after an
 * early `close(h)`, null-guards it -- so a correct implementation calls this at
 * most once with a non-NULL pointer per open. A NULL arrives when the open
 * failed or the handle was already closed; counting those separately is what
 * lets the gate tell "closed twice" from "never opened". */
int64_t fh_close(void *h) {
    g_closes++;
    if (!h) return 0;
    g_live--;
    return (int64_t)fclose((FILE *)h);
}

int64_t fh_getc(void *h) { return h ? (int64_t)fgetc((FILE *)h) : -1; }

/* A handle cannot be tested against null in Tycho -- it may not go in an Option
 * or a Result, and there is no null literal for one -- so asking C is the only
 * way to find out whether the open succeeded. */
int64_t fh_ok(void *h) { return h ? 1 : 0; }

int64_t fh_live(void)   { return g_live; }
int64_t fh_opens(void)  { return g_opens; }
int64_t fh_closes(void) { return g_closes; }
