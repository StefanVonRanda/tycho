/* Hier runtime - embedded verbatim into every generated C file.
 *
 * Memory model: implicit hierarchical arenas.
 *
 *   - Every scope (each proc, each if/else block, each loop) gets its own
 *     arena. An arena owns a singly-linked list of bump-allocated blocks
 *     and grows by adding blocks; it never reallocs a live block, so
 *     every pointer it hands out stays valid until the arena is reset or
 *     freed.
 *   - Arenas form a hierarchy: arena_child(parent) creates a child arena.
 *     A child is freed (its blocks released) when its scope ends; a loop's
 *     scratch arena is reset (blocks released) every iteration, so loop
 *     memory stays bounded.
 *
 * Data moves between arenas exactly two ways, and because each arena has
 * its own storage both are simply "allocate in the target arena":
 *
 *   1. Down, as a function argument: a pointer is passed to a callee. The
 *      callee's arena is a child, so the pointer outlives the call. No copy.
 *   2. Up, by being returned: the value is allocated directly in the
 *      caller's (parent) arena, so it survives the callee's arena being
 *      freed. Assigning to a variable owned by an outer scope works the
 *      same way - the compiler allocates the value in that variable's
 *      arena.
 *
 * No part of this is visible in Hier source: a programmer only declares
 * and uses values, as if the language were dynamically managed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HIER_BLOCK_DEFAULT (1u << 16)

typedef struct HBlock HBlock;
struct HBlock { HBlock *next; size_t cap; size_t off; };
/* block payload lives immediately after the header */

typedef struct { HBlock *head; size_t blocksz; } Arena;

static void hier_oom(void) { fprintf(stderr, "hier: out of memory\n"); exit(1); }

Arena arena_new(size_t blocksz) {
    Arena a;
    a.head = NULL;
    a.blocksz = blocksz ? blocksz : HIER_BLOCK_DEFAULT;
    return a;
}

Arena arena_child(Arena *parent) { return arena_new(parent->blocksz); }

void *arena_alloc(Arena *a, size_t n) {
    n = (n + 15u) & ~(size_t)15u;            /* 16-byte align */
    if (!a->head || a->head->off + n > a->head->cap) {
        size_t cap = n > a->blocksz ? n : a->blocksz;
        HBlock *b = (HBlock *)malloc(sizeof(HBlock) + cap);
        if (!b) hier_oom();
        b->cap = cap;
        b->off = 0;
        b->next = a->head;
        a->head = b;
    }
    void *p = (char *)(a->head + 1) + a->head->off;
    a->head->off += n;
    return p;
}

/* release every block; the arena can be used again afterwards (this is
 * what a loop's scratch arena does each iteration) */
void arena_reset(Arena *a) {
    HBlock *b = a->head;
    while (b) { HBlock *nx = b->next; free(b); b = nx; }
    a->head = NULL;
}

void arena_free(Arena *a) { arena_reset(a); }

char *hier_str_concat(Arena *a, const char *x, const char *y) {
    size_t lx = strlen(x), ly = strlen(y);
    char *r = (char *)arena_alloc(a, lx + ly + 1);
    memcpy(r, x, lx);
    memcpy(r + lx, y, ly);
    r[lx + ly] = '\0';
    return r;
}

void hier_print(const char *s) { fputs(s, stdout); }

char *hier_input(Arena *a) {
    size_t cap = 128, len = 0;
    char *buf = (char *)arena_alloc(a, cap);
    int c;
    fflush(stdout);                          /* flush any prompt first */
    while ((c = getchar()) != EOF && c != '\n') {
        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            char *nb = (char *)arena_alloc(a, ncap);
            memcpy(nb, buf, len);
            buf = nb;
            cap = ncap;
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    return buf;
}

char *hier_int_to_str(Arena *a, long n) {
    char tmp[32];
    int m = snprintf(tmp, sizeof tmp, "%ld", n);
    char *r = (char *)arena_alloc(a, (size_t)m + 1);
    memcpy(r, tmp, (size_t)m + 1);
    return r;
}

/* --- [int] arrays ---------------------------------------------------------
 * A HierArrInt is a value (passed/copied by value, 3 words). Its backing
 * buffer lives in the arena that owns the variable holding it; growth
 * allocates a fresh, larger buffer in that arena (old one wasted but
 * bounded by geometric growth, reclaimed when the arena ends). */

typedef struct { long *data; long len; long cap; } HierArrInt;

HierArrInt hier_arr_int_with_cap(Arena *a, long cap) {
    HierArrInt r;
    r.len = 0;
    r.cap = cap;
    r.data = cap > 0 ? (long *)arena_alloc(a, (size_t)cap * sizeof(long)) : NULL;
    return r;
}

void hier_arr_int_push(Arena *a, HierArrInt *xs, long v) {
    if (xs->len == xs->cap) {
        long ncap = xs->cap ? xs->cap * 2 : 4;
        long *nd = (long *)arena_alloc(a, (size_t)ncap * sizeof(long));
        if (xs->len) memcpy(nd, xs->data, (size_t)xs->len * sizeof(long));
        xs->data = nd;
        xs->cap = ncap;
    }
    xs->data[xs->len++] = v;
}

long hier_arr_int_get(HierArrInt xs, long i) {
    if (i < 0 || i >= xs.len) {
        fprintf(stderr, "hier: index %ld out of bounds (len %ld)\n", i, xs.len);
        exit(1);
    }
    return xs.data[i];
}

void hier_arr_int_set(HierArrInt *xs, long i, long v) {
    if (i < 0 || i >= xs->len) {
        fprintf(stderr, "hier: index %ld out of bounds (len %ld)\n", i, xs->len);
        exit(1);
    }
    xs->data[i] = v;
}

/* value-semantic copy: independent buffer in arena `a` */
HierArrInt hier_arr_int_copy(Arena *a, HierArrInt src) {
    HierArrInt r = hier_arr_int_with_cap(a, src.len);
    r.len = src.len;
    if (src.len) memcpy(r.data, src.data, (size_t)src.len * sizeof(long));
    return r;
}
