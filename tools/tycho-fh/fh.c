#include <stdio.h>
#include <stdint.h>

static int64_t g_live = 0;    /* currently-open handles: 0 balanced, >0 leak, <0 double free */
static int64_t g_opens = 0;   /* successful opens, ever */
static int64_t g_closes = 0;  /* destructor calls, ever. NULL never arrives -- see fh_close */

void *fh_open(char *path, char *mode) {
    FILE *f = fopen(path, mode);
    if (f) { g_live++; g_opens++; }
    return f;
}

/* The destructor. The compiler emits `if (h) fh_close(h);` at scope exit and
 * nulls the variable on an early `close(h)`, so this is called at most once per
 * open and ALWAYS with a non-NULL pointer.
 *
 * This comment used to say a NULL arrives "when the open failed or the handle
 * was already closed", and that counting those apart is what lets the gate tell
 * "closed twice" from "never opened". Measured 2026-09-21, both cases: a failed
 * open gives opens=0 closes=0, and close(h) followed by scope exit gives
 * closes=1 with zero NULL arrivals. The guard below is therefore defence against
 * a C caller, not against the compiler -- and an agent probe wrote a redundant
 * `_ok` shim into its own binding on the strength of the old wording before
 * measuring and deleting it. */
int64_t fh_close(void *h) {
    g_closes++;
    if (!h) return 0;
    g_live--;
    return (int64_t)fclose((FILE *)h);
}

int64_t fh_getc(void *h) { return h ? (int64_t)fgetc((FILE *)h) : -1; }

/* Asking C is NOT the only way: `is_null(h)` accepts a handle, and an early
 * `close(h)` nulls the variable, so `is_null` reads false after a good open and
 * true after a failed one or a close (measured 2026-09-21). This comment used to
 * claim the opposite -- that a handle cannot be tested against null in Tycho --
 * and it is why fh_ok exists. It is kept because main.ty calls it and because a
 * C-side liveness check is a reasonable thing for a shim to offer; it is no
 * longer the only option, and a new binding does not need one. */
int64_t fh_ok(void *h) { return h ? 1 : 0; }

int64_t fh_live(void)   { return g_live; }
int64_t fh_opens(void)  { return g_opens; }
int64_t fh_closes(void) { return g_closes; }
