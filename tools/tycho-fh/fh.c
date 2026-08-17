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
