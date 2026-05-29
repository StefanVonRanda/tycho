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

/* value-semantic copy of a string into arena `a`. Used when a bare string
 * variable is returned or assigned to an outer scope: the variable is only
 * a pointer into a scope about to be freed, so the bytes must be copied
 * into the destination arena to survive (cf. hier_arr_int_copy). */
char *hier_str_copy(Arena *a, const char *s) {
    size_t n = strlen(s);
    char *r = (char *)arena_alloc(a, n + 1);
    memcpy(r, s, n + 1);
    return r;
}

void hier_print(const char *s) { fputs(s, stdout); }

/* --- string builtins ------------------------------------------------------
 * Strings are NUL-terminated byte buffers (char *). len/index are byte-
 * oriented. substr returns a fresh copy in the target arena (value
 * semantics, like everything else); its range is clamped Python-style. */

long hier_str_len(const char *s) { return (long)strlen(s); }

long hier_str_get(const char *s, long i) {
    long n = (long)strlen(s);
    if (i < 0 || i >= n) {
        fprintf(stderr, "hier: string index %ld out of bounds (len %ld)\n", i, n);
        exit(1);
    }
    return (long)(unsigned char)s[i];   /* unsigned: 0..255, never negative */
}

/* substring [start, end); out-of-range bounds are clamped, not an error */
char *hier_str_substr(Arena *a, const char *s, long start, long end) {
    long n = (long)strlen(s);
    if (start < 0) start = 0;
    if (end > n) end = n;
    if (end < start) end = start;
    long m = end - start;
    char *r = (char *)arena_alloc(a, (size_t)m + 1);
    memcpy(r, s + start, (size_t)m);
    r[m] = '\0';
    return r;
}

/* byte index of the first occurrence of sub in s, or -1 if absent */
long hier_str_find(const char *s, const char *sub) {
    const char *p = strstr(s, sub);
    return p ? (long)(p - s) : -1;
}

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

/* structural equality: same length and equal elements (value semantics) */
int hier_arr_int_eq(HierArrInt x, HierArrInt y) {
    if (x.len != y.len) return 0;
    for (long i = 0; i < x.len; i++)
        if (x.data[i] != y.data[i]) return 0;
    return 1;
}

/* --- [string] arrays ------------------------------------------------------
 * Like HierArrInt, but the elements are char* whose bytes live in an arena.
 * The lifetime seam (see hier_str_copy) nests here: every operation that
 * moves a string *into* the array (push/set/literal/copy) must copy the
 * bytes into the array's owning arena, and copying the array must deep-copy
 * each element too — otherwise a promoted array keeps pointers into a freed
 * scope. */

typedef struct { char **data; long len; long cap; } HierArrStr;

HierArrStr hier_arr_str_with_cap(Arena *a, long cap) {
    HierArrStr r;
    r.len = 0;
    r.cap = cap;
    r.data = cap > 0 ? (char **)arena_alloc(a, (size_t)cap * sizeof(char *)) : NULL;
    return r;
}

void hier_arr_str_push(Arena *a, HierArrStr *xs, const char *v) {
    if (xs->len == xs->cap) {
        long ncap = xs->cap ? xs->cap * 2 : 4;
        char **nd = (char **)arena_alloc(a, (size_t)ncap * sizeof(char *));
        if (xs->len) memcpy(nd, xs->data, (size_t)xs->len * sizeof(char *));
        xs->data = nd;
        xs->cap = ncap;
    }
    xs->data[xs->len++] = hier_str_copy(a, v);   /* copy bytes into owner arena */
}

char *hier_arr_str_get(HierArrStr xs, long i) {
    if (i < 0 || i >= xs.len) {
        fprintf(stderr, "hier: index %ld out of bounds (len %ld)\n", i, xs.len);
        exit(1);
    }
    return xs.data[i];
}

void hier_arr_str_set(Arena *a, HierArrStr *xs, long i, const char *v) {
    if (i < 0 || i >= xs->len) {
        fprintf(stderr, "hier: index %ld out of bounds (len %ld)\n", i, xs->len);
        exit(1);
    }
    xs->data[i] = hier_str_copy(a, v);           /* copy bytes into owner arena */
}

/* value-semantic copy: independent buffer AND independent element bytes in a */
HierArrStr hier_arr_str_copy(Arena *a, HierArrStr src) {
    HierArrStr r = hier_arr_str_with_cap(a, src.len);
    r.len = src.len;
    for (long i = 0; i < src.len; i++) r.data[i] = hier_str_copy(a, src.data[i]);
    return r;
}

/* structural equality: same length and byte-equal elements (value semantics) */
int hier_arr_str_eq(HierArrStr x, HierArrStr y) {
    if (x.len != y.len) return 0;
    for (long i = 0; i < x.len; i++)
        if (strcmp(x.data[i], y.data[i]) != 0) return 0;
    return 1;
}

/* split s on a (non-empty) separator into a fresh [string] in arena a.
 * n separators -> n+1 fields (leading/trailing/adjacent seps yield empty
 * fields); an empty s yields one empty field. An empty separator has no
 * well-defined splitting, so fail closed rather than guess. */
HierArrStr hier_str_split(Arena *a, const char *s, const char *sep) {
    size_t seplen = strlen(sep);
    if (seplen == 0) {
        fprintf(stderr, "hier: split with an empty separator\n");
        exit(1);
    }
    HierArrStr r = hier_arr_str_with_cap(a, 4);
    const char *start = s;
    for (;;) {
        const char *hit = strstr(start, sep);
        if (!hit) {                              /* last field: rest of string */
            hier_arr_str_push(a, &r, start);
            break;
        }
        /* field is [start, hit); copy it as a counted slice */
        size_t flen = (size_t)(hit - start);
        char *field = (char *)arena_alloc(a, flen + 1);
        memcpy(field, start, flen);
        field[flen] = '\0';
        hier_arr_str_push(a, &r, field);
        start = hit + seplen;
    }
    return r;
}
