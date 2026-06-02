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
#include <dirent.h>

#define HIER_BLOCK_DEFAULT (1u << 16)

typedef struct HBlock HBlock;
struct HBlock { HBlock *next; size_t cap; size_t off; };
/* block payload lives immediately after the header */

typedef struct { HBlock *head; size_t blocksz; } Arena;

static void hier_oom(void) { fprintf(stderr, "hier: out of memory\n"); exit(1); }

/* Global block free-list. Arenas are created/reset/freed per block scope, call,
 * and loop iteration, so naive malloc/free of a HIER_BLOCK_DEFAULT-sized block
 * per scope dominated runtime on allocation-heavy workloads (e.g. the
 * self-hosting compiler: ~13x slower than no-free). Instead of returning blocks
 * to the OS, reset/free hand them to this pool, and arena_alloc takes from it
 * first -- O(1) pointer ops, no malloc/free churn, no page re-faulting. Peak
 * live memory is unchanged (the pool holds at most what a scope just released);
 * pool blocks are reclaimed by the OS at process exit. Single-threaded. */
static HBlock *g_block_pool = NULL;

static HBlock *block_get(size_t cap) {
    if (g_block_pool && g_block_pool->cap >= cap) {  /* reuse a pooled block (uniform sizes hit here) */
        HBlock *b = g_block_pool;
        g_block_pool = b->next;
        b->off = 0;
        b->next = NULL;
        return b;
    }
    HBlock *b = (HBlock *)malloc(sizeof(HBlock) + cap);
    if (!b) hier_oom();
    b->cap = cap;
    b->off = 0;
    b->next = NULL;
    return b;
}

static void block_release_chain(HBlock *b) {     /* push a block chain onto the pool */
    while (b) { HBlock *nx = b->next; b->next = g_block_pool; g_block_pool = b; b = nx; }
}

Arena arena_new(size_t blocksz) {
    Arena a;
    a.head = NULL;
    a.blocksz = blocksz ? blocksz : HIER_BLOCK_DEFAULT;
    return a;
}

Arena arena_child(Arena *parent) { return arena_new(parent->blocksz); }

void *arena_alloc(Arena *a, size_t n) {
    n = (n + 7u) & ~(size_t)7u;             /* 8-byte align (max align of Hier types: long/double/ptr) */
    if (!a->head || a->head->off + n > a->head->cap) {
        size_t cap = n > a->blocksz ? n : a->blocksz;
        HBlock *b = block_get(cap);
        b->next = a->head;
        a->head = b;
    }
    void *p = (char *)(a->head + 1) + a->head->off;
    a->head->off += n;
    return p;
}

/* Reset for reuse (a loop's scratch arena, each iteration): RETAIN the head
 * block and just rewind it (off=0), releasing only any overflow blocks to the
 * pool. The common case -- a loop body whose per-iteration allocations fit in
 * one block -- then does zero pool traffic per iteration, only a pointer rewind. */
void arena_reset(Arena *a) {
    HBlock *b = a->head;
    if (!b) return;
    block_release_chain(b->next);   /* overflow blocks -> pool */
    b->next = NULL;
    b->off = 0;                     /* keep & reuse the head block */
}

/* Release the arena entirely (scope/call end): all blocks go to the pool. */
void arena_free(Arena *a) {
    block_release_chain(a->head);
    a->head = NULL;
}

char *hier_str_concat(Arena *a, const char *x, const char *y) {
    size_t lx = strlen(x), ly = strlen(y);
    char *r = (char *)arena_alloc(a, lx + ly + 1);
    memcpy(r, x, lx);
    memcpy(r + lx, y, ly);
    r[lx + ly] = '\0';
    return r;
}

/* In-place string append for the accumulator pattern `acc = acc + e`. The
 * compiler emits this only for a uniquely-owned local string it tracks with
 * sidecar len/cap, so growing the buffer in place is sound (value semantics
 * guarantees no alias observes the mutation). Geometric growth makes a loop
 * of appends amortized O(1) each -> O(N) total time and memory, vs the O(N^2)
 * of repeated hier_str_concat. Mirrors hier_arr_int_push. The new buffer
 * lives in arena `a` (the variable's owning arena, not the caller's scratch).
 * memmove, not memcpy: e may alias *s (acc = acc + acc). */
void hier_str_append(Arena *a, char **s, long *len, long *cap, const char *e) {
    long el = (long)strlen(e);
    long need = *len + el + 1;
    if (need > *cap) {
        long nc = *cap ? *cap * 2 : 16;
        while (nc < need) nc *= 2;
        char *nb = (char *)arena_alloc(a, (size_t)nc);
        memcpy(nb, *s, (size_t)*len);    /* old buffer still live (bump arena) */
        *s = nb;
        *cap = nc;
    }
    memmove(*s + *len, e, (size_t)el);
    *len += el;
    (*s)[*len] = '\0';
}

/* string + char: one-byte append, no strlen/snprintf. `c` is a byte carried in
 * a long (Hier's char type). New buffer lives in arena `a` (cf. hier_str_concat). */
char *hier_str_concat_char(Arena *a, const char *x, long c) {
    size_t lx = strlen(x);
    char *r = (char *)arena_alloc(a, lx + 2);
    memcpy(r, x, lx);
    r[lx] = (char)c;
    r[lx + 1] = '\0';
    return r;
}

/* In-place one-byte append for the accumulator `acc = acc + c` where c is a
 * char. Same uniqueness/geometric-growth contract as hier_str_append. */
void hier_str_append_char(Arena *a, char **s, long *len, long *cap, long c) {
    long need = *len + 2;
    if (need > *cap) {
        long nc = *cap ? *cap * 2 : 16;
        while (nc < need) nc *= 2;
        char *nb = (char *)arena_alloc(a, (size_t)nc);
        memcpy(nb, *s, (size_t)*len);
        *s = nb;
        *cap = nc;
    }
    (*s)[*len] = (char)c;
    *len += 1;
    (*s)[*len] = '\0';
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

/* Same bounds-checked byte read, but the caller passes the length — used when the
 * codegen has hoisted strlen(s) into a sidecar for an indexed, never-reassigned
 * string (its length is loop-invariant). Turns an O(n)-per-access bounds check
 * into O(1), so indexing a string in a loop is O(n) not O(n^2). */
long hier_str_get_n(const char *s, long i, long n) {
    if (i < 0 || i >= n) {
        fprintf(stderr, "hier: string index %ld out of bounds (len %ld)\n", i, n);
        exit(1);
    }
    return (long)(unsigned char)s[i];
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

/* read ALL of stdin into one string (the whole source file), newlines and all.
 * Unlike hier_input (one line, can't tell EOF from a blank line), this is what
 * a source-to-source tool needs: `hierc-hier < src.hi > out.c`. */
char *hier_read_all(Arena *a) {
    size_t cap = 4096, len = 0;
    char *buf = (char *)arena_alloc(a, cap);
    int c;
    while ((c = getchar()) != EOF) {
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

/* read_file(path): the whole file as a string, or "" if it can't be opened. */
char *hier_read_file(Arena *a, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return hier_str_copy(a, "");
    size_t cap = 4096, len = 0;
    char *buf = (char *)arena_alloc(a, cap);
    size_t n;
    while ((n = fread(buf + len, 1, cap - len, f)) > 0) {
        len += n;
        if (len == cap) {
            size_t ncap = cap * 2;
            char *nb = (char *)arena_alloc(a, ncap);
            memcpy(nb, buf, len);
            buf = nb;
            cap = ncap;
        }
    }
    fclose(f);
    buf[len] = '\0';
    return buf;
}

/* args(): the program's argv as a string array (args()[0] is the program name).
 * The emitted `main` stashes argc/argv here. */
static int    hier_argc = 0;
static char **hier_argv = NULL;

/* chr(n): the one-byte string for code point n (0..255) — the inverse of the
 * `s[i] -> int` byte read. n==0 yields the empty string (strings are
 * NUL-terminated), which is fine: codegen never emits a NUL byte. */
char *hier_chr(Arena *a, long n) {
    char *r = (char *)arena_alloc(a, 2);
    r[0] = (char)(n & 0xff);
    r[1] = '\0';
    return r;
}

/* die(msg): the error path for a Hier-written compiler (and any tool) — print
 * to stderr and exit non-zero. Never returns; declared T_VOID, so a non-void
 * function that dies in a branch still gets its defensive fallback return. */
void hier_die(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

char *hier_int_to_str(Arena *a, long n) {
    char tmp[32];
    int m = snprintf(tmp, sizeof tmp, "%ld", n);
    char *r = (char *)arena_alloc(a, (size_t)m + 1);
    memcpy(r, tmp, (size_t)m + 1);
    return r;
}

/* Float to string: %.15g trims trailing zeros while keeping ~15 significant
 * digits (readable, not full 17-digit round-trip). A value that prints with no
 * '.', exponent, or inf/nan marker (e.g. 3 for 3.0) gets a trailing ".0" so it
 * is never mistaken for an int. */
char *hier_float_to_str(Arena *a, double x) {
    char tmp[64];
    int m = snprintf(tmp, sizeof tmp, "%.15g", x);
    int floaty = 0;
    for (int i = 0; i < m; i++) {
        char c = tmp[i];
        if (c == '.' || c == 'e' || c == 'E' || c == 'n' || c == 'N' || c == 'i' || c == 'I') { floaty = 1; break; }
    }
    if (!floaty && m + 2 < (int)sizeof tmp) { tmp[m++] = '.'; tmp[m++] = '0'; tmp[m] = '\0'; }
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

/* --- [float] arrays -------------------------------------------------------
 * Exactly HierArrInt with double elements (a value word, no heap nesting), so
 * every op mirrors the int array. Equality is bitwise via ==, with the usual
 * float caveats. */

typedef struct { double *data; long len; long cap; } HierArrFloat;

HierArrFloat hier_arr_float_with_cap(Arena *a, long cap) {
    HierArrFloat r;
    r.len = 0;
    r.cap = cap;
    r.data = cap > 0 ? (double *)arena_alloc(a, (size_t)cap * sizeof(double)) : NULL;
    return r;
}

void hier_arr_float_push(Arena *a, HierArrFloat *xs, double v) {
    if (xs->len == xs->cap) {
        long ncap = xs->cap ? xs->cap * 2 : 4;
        double *nd = (double *)arena_alloc(a, (size_t)ncap * sizeof(double));
        if (xs->len) memcpy(nd, xs->data, (size_t)xs->len * sizeof(double));
        xs->data = nd;
        xs->cap = ncap;
    }
    xs->data[xs->len++] = v;
}

double hier_arr_float_get(HierArrFloat xs, long i) {
    if (i < 0 || i >= xs.len) {
        fprintf(stderr, "hier: index %ld out of bounds (len %ld)\n", i, xs.len);
        exit(1);
    }
    return xs.data[i];
}

void hier_arr_float_set(HierArrFloat *xs, long i, double v) {
    if (i < 0 || i >= xs->len) {
        fprintf(stderr, "hier: index %ld out of bounds (len %ld)\n", i, xs->len);
        exit(1);
    }
    xs->data[i] = v;
}

HierArrFloat hier_arr_float_copy(Arena *a, HierArrFloat src) {
    HierArrFloat r = hier_arr_float_with_cap(a, src.len);
    r.len = src.len;
    if (src.len) memcpy(r.data, src.data, (size_t)src.len * sizeof(double));
    return r;
}

int hier_arr_float_eq(HierArrFloat x, HierArrFloat y) {
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

/* list_dir(path): the directory's entries (excluding "." and ".."), or an empty
 * array if it can't be opened. Order is the filesystem's (sort if you need it). */
HierArrStr hier_list_dir(Arena *a, const char *path) {
    HierArrStr r = hier_arr_str_with_cap(a, 8);
    DIR *d = opendir(path);
    if (!d) return r;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' || (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;   /* skip "." and ".." */
        hier_arr_str_push(a, &r, de->d_name);
    }
    closedir(d);
    return r;
}

/* args(): the program's command-line arguments as a string array. */
HierArrStr hier_args(Arena *a) {
    HierArrStr r = hier_arr_str_with_cap(a, hier_argc > 0 ? hier_argc : 1);
    for (int i = 0; i < hier_argc; i++)
        hier_arr_str_push(a, &r, hier_argv[i]);
    return r;
}

/* ----------------------------------------------------------- Map(string,int)
 * A HierMapSI is a value (5 words, passed/copied by value like HierArr*). Its
 * backing tables live in an arena. Open addressing, linear probe, power-of-two
 * capacity. keys[i]==NULL means empty; keys[i]==HIER_MAP_TOMB is a deleted slot
 * (a tombstone) kept so a probe chain that ran through it is not broken. A
 * 0-cap map is the empty literal; the first insert allocates. `len` counts live
 * entries; `used` counts live + tombstone slots and drives rehash, so a
 * delete-heavy map cannot fill with tombstones and stall probing.
 *
 * Value semantics, same as the arrays: `m2 = map_set(m, k, v)` returns a NEW
 * map (deep copy + insert) so `m` is unchanged; the compiler rewrites the
 * uniquely-owned accumulator shape `m = map_set(m, ...)` to map_put in place
 * (cf. the in-place string append) so a build-up loop is amortized O(1) per
 * insert instead of O(n) deep-copy each step. `m = map_del(m, k)` is rewritten
 * the same way to an in-place tombstone delete. Key bytes are copied into the
 * owning arena exactly as [string] elements are (the lifetime seam nests). */
typedef struct { char **keys; long *vals; long len; long cap; long used; } HierMapSI;

/* the tombstone sentinel: a unique non-NULL char* that is never a real key. */
static char hier_map_tomb_;
#define HIER_MAP_TOMB (&hier_map_tomb_)
static int hier_map_live(const char *p) { return p != NULL && p != HIER_MAP_TOMB; }

static unsigned long hier_si_hash(const char *s) {        /* FNV-1a */
    unsigned long h = 1469598103934665603UL;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211UL; }
    return h;
}

HierMapSI hier_map_si_with_cap(Arena *a, long cap) {
    HierMapSI m;
    long c = 8; while (c < cap) c *= 2;
    if (cap == 0) c = 0;
    m.cap = c; m.len = 0; m.used = 0;
    if (c == 0) { m.keys = NULL; m.vals = NULL; return m; }
    m.keys = (char **)arena_alloc(a, (size_t)c * sizeof(char *));
    m.vals = (long *) arena_alloc(a, (size_t)c * sizeof(long));
    for (long i = 0; i < c; i++) m.keys[i] = NULL;
    return m;
}

/* lookup: slot of a live key, or -1 if absent (skips tombstones, never inserts).
 * used by get/has/eq/del. Terminates because used <= cap/2 leaves a NULL slot. */
static long hier_map_si_find(HierMapSI m, const char *k) {
    if (m.cap == 0) return -1;
    unsigned long mask = (unsigned long)m.cap - 1;
    long i = (long)(hier_si_hash(k) & mask);
    while (m.keys[i] != NULL) {
        if (m.keys[i] != HIER_MAP_TOMB && strcmp(m.keys[i], k) == 0) return i;
        i = (long)((i + 1) & mask);
    }
    return -1;
}

/* insert slot: a matching live key's slot, else the first tombstone the probe
 * passed (reuse it), else the terminating NULL slot. Caller tests liveness. */
static long hier_map_si_slot(HierMapSI m, const char *k) {
    unsigned long mask = (unsigned long)m.cap - 1;
    long i = (long)(hier_si_hash(k) & mask);
    long tomb = -1;
    while (m.keys[i] != NULL) {
        if (m.keys[i] == HIER_MAP_TOMB) { if (tomb < 0) tomb = i; }
        else if (strcmp(m.keys[i], k) == 0) return i;
        i = (long)((i + 1) & mask);
    }
    return tomb >= 0 ? tomb : i;
}

/* in-place insert into a uniquely-owned map; rehashes past 1/2 load (counting
 * tombstones), growing only when live entries pass the threshold — a rehash at
 * the same size purges tombstones. Key bytes copied into arena a so a pushed
 * loop-scratch key does not dangle. */
void hier_map_si_put(Arena *a, HierMapSI *m, const char *k, long v) {
    if (m->cap == 0 || (m->used + 1) * 2 > m->cap) {
        long nc = ((m->len + 1) * 2 > m->cap) ? (m->cap ? m->cap * 2 : 8)
                                              : (m->cap ? m->cap : 8);
        HierMapSI n = hier_map_si_with_cap(a, nc);
        for (long i = 0; i < m->cap; i++)
            if (hier_map_live(m->keys[i])) {
                long s = hier_map_si_slot(n, m->keys[i]);
                n.keys[s] = m->keys[i]; n.vals[s] = m->vals[i]; n.len++; n.used++;
            }
        *m = n;
    }
    long s = hier_map_si_slot(*m, k);
    if (!hier_map_live(m->keys[s])) {
        if (m->keys[s] == NULL) m->used++;     /* fresh slot; reusing a tombstone keeps used */
        m->keys[s] = hier_str_copy(a, k);
        m->len++;
    }
    m->vals[s] = v;
}

/* in-place delete from a uniquely-owned map: tombstone the slot if the key is
 * live. `used` is unchanged (the slot is still occupied); a later put purges it. */
void hier_map_si_del(HierMapSI *m, const char *k) {
    long s = hier_map_si_find(*m, k);
    if (s < 0) return;
    m->keys[s] = HIER_MAP_TOMB;
    m->len--;
}

/* deep copy (value semantics): independent tables + copied key bytes. Drops any
 * tombstones (only live entries are reinserted), so the copy is compact. */
HierMapSI hier_map_si_copy(Arena *a, HierMapSI src) {
    HierMapSI r = hier_map_si_with_cap(a, src.len ? src.len * 2 : 0);
    for (long i = 0; i < src.cap; i++)
        if (hier_map_live(src.keys[i])) hier_map_si_put(a, &r, src.keys[i], src.vals[i]);
    return r;
}

/* pure set: returns a NEW map (deep copy of m, then insert). */
HierMapSI hier_map_si_set(Arena *a, HierMapSI m, const char *k, long v) {
    HierMapSI r = hier_map_si_copy(a, m);
    hier_map_si_put(a, &r, k, v);
    return r;
}

/* pure delete: returns a NEW map (deep copy of m) without key k. */
HierMapSI hier_map_si_del_pure(Arena *a, HierMapSI m, const char *k) {
    HierMapSI r = hier_map_si_copy(a, m);
    hier_map_si_del(&r, k);
    return r;
}

long hier_map_si_get(HierMapSI m, const char *k, long dflt) {
    long s = hier_map_si_find(m, k);
    return s < 0 ? dflt : m.vals[s];
}

int hier_map_si_has(HierMapSI m, const char *k) {
    return hier_map_si_find(m, k) >= 0;
}

/* the live keys as a [string], in table order. Key bytes are copied into a so
 * the result owns them (value semantics; cf. hier_arr_str_copy). This is how a
 * map is iterated: keys(m) then index the array. */
HierArrStr hier_map_si_keys(Arena *a, HierMapSI m) {
    HierArrStr r = hier_arr_str_with_cap(a, m.len);
    for (long i = 0; i < m.cap; i++)
        if (hier_map_live(m.keys[i])) hier_arr_str_push(a, &r, m.keys[i]);
    return r;
}

/* structural equality (value semantics): same live entries, same values. */
int hier_map_si_eq(HierMapSI x, HierMapSI y) {
    if (x.len != y.len) return 0;
    for (long i = 0; i < x.cap; i++)
        if (hier_map_live(x.keys[i])) {
            long s = hier_map_si_find(y, x.keys[i]);
            if (s < 0 || y.vals[s] != x.vals[i]) return 0;
        }
    return 1;
}

/* ----------------------------------------------------------- Map(string,float)
 * Exactly HierMapSI with `double` values (string keys, float values) — the
 * same open-addressing / tombstone table, sharing hier_map_live, hier_si_hash,
 * and the tombstone sentinel. Only the value word's type differs. */
typedef struct { char **keys; double *vals; long len; long cap; long used; } HierMapSF;

HierMapSF hier_map_sf_with_cap(Arena *a, long cap) {
    HierMapSF m;
    long c = 8; while (c < cap) c *= 2;
    if (cap == 0) c = 0;
    m.cap = c; m.len = 0; m.used = 0;
    if (c == 0) { m.keys = NULL; m.vals = NULL; return m; }
    m.keys = (char **) arena_alloc(a, (size_t)c * sizeof(char *));
    m.vals = (double *)arena_alloc(a, (size_t)c * sizeof(double));
    for (long i = 0; i < c; i++) m.keys[i] = NULL;
    return m;
}

static long hier_map_sf_find(HierMapSF m, const char *k) {
    if (m.cap == 0) return -1;
    unsigned long mask = (unsigned long)m.cap - 1;
    long i = (long)(hier_si_hash(k) & mask);
    while (m.keys[i] != NULL) {
        if (m.keys[i] != HIER_MAP_TOMB && strcmp(m.keys[i], k) == 0) return i;
        i = (long)((i + 1) & mask);
    }
    return -1;
}

static long hier_map_sf_slot(HierMapSF m, const char *k) {
    unsigned long mask = (unsigned long)m.cap - 1;
    long i = (long)(hier_si_hash(k) & mask);
    long tomb = -1;
    while (m.keys[i] != NULL) {
        if (m.keys[i] == HIER_MAP_TOMB) { if (tomb < 0) tomb = i; }
        else if (strcmp(m.keys[i], k) == 0) return i;
        i = (long)((i + 1) & mask);
    }
    return tomb >= 0 ? tomb : i;
}

void hier_map_sf_put(Arena *a, HierMapSF *m, const char *k, double v) {
    if (m->cap == 0 || (m->used + 1) * 2 > m->cap) {
        long nc = ((m->len + 1) * 2 > m->cap) ? (m->cap ? m->cap * 2 : 8)
                                              : (m->cap ? m->cap : 8);
        HierMapSF n = hier_map_sf_with_cap(a, nc);
        for (long i = 0; i < m->cap; i++)
            if (hier_map_live(m->keys[i])) {
                long s = hier_map_sf_slot(n, m->keys[i]);
                n.keys[s] = m->keys[i]; n.vals[s] = m->vals[i]; n.len++; n.used++;
            }
        *m = n;
    }
    long s = hier_map_sf_slot(*m, k);
    if (!hier_map_live(m->keys[s])) {
        if (m->keys[s] == NULL) m->used++;
        m->keys[s] = hier_str_copy(a, k);
        m->len++;
    }
    m->vals[s] = v;
}

void hier_map_sf_del(HierMapSF *m, const char *k) {
    long s = hier_map_sf_find(*m, k);
    if (s < 0) return;
    m->keys[s] = HIER_MAP_TOMB;
    m->len--;
}

HierMapSF hier_map_sf_copy(Arena *a, HierMapSF src) {
    HierMapSF r = hier_map_sf_with_cap(a, src.len ? src.len * 2 : 0);
    for (long i = 0; i < src.cap; i++)
        if (hier_map_live(src.keys[i])) hier_map_sf_put(a, &r, src.keys[i], src.vals[i]);
    return r;
}

HierMapSF hier_map_sf_set(Arena *a, HierMapSF m, const char *k, double v) {
    HierMapSF r = hier_map_sf_copy(a, m);
    hier_map_sf_put(a, &r, k, v);
    return r;
}

HierMapSF hier_map_sf_del_pure(Arena *a, HierMapSF m, const char *k) {
    HierMapSF r = hier_map_sf_copy(a, m);
    hier_map_sf_del(&r, k);
    return r;
}

double hier_map_sf_get(HierMapSF m, const char *k, double dflt) {
    long s = hier_map_sf_find(m, k);
    return s < 0 ? dflt : m.vals[s];
}

int hier_map_sf_has(HierMapSF m, const char *k) {
    return hier_map_sf_find(m, k) >= 0;
}

HierArrStr hier_map_sf_keys(Arena *a, HierMapSF m) {
    HierArrStr r = hier_arr_str_with_cap(a, m.len);
    for (long i = 0; i < m.cap; i++)
        if (hier_map_live(m.keys[i])) hier_arr_str_push(a, &r, m.keys[i]);
    return r;
}

int hier_map_sf_eq(HierMapSF x, HierMapSF y) {
    if (x.len != y.len) return 0;
    for (long i = 0; i < x.cap; i++)
        if (hier_map_live(x.keys[i])) {
            long s = hier_map_sf_find(y, x.keys[i]);
            if (s < 0 || y.vals[s] != x.vals[i]) return 0;
        }
    return 1;
}
