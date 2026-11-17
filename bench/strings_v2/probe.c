/* Phase 4.1 probe: three string layouts under the same bump arena.
 *
 *   -DLAYOUT=0  today: char * with an 8-byte length header before the data,
 *               NUL-terminated (runtime/tycho_rt.c tycho_str_alloc)
 *   -DLAYOUT=1  16-byte {ptr,len} value; owned buffers keep a trailing NUL
 *   -DLAYOUT=2  {ptr,len} + inline storage for <= 15 bytes (flag in byte 15)
 *   -DVIEWS=1   (layouts 1/2 only) split/substr return views into the source
 *
 * Usage: probe orders [N]   |   probe split [lines] [passes]
 * Every build prints the same checksum line; run.sh compares them.
 * ponytail: models only what the layouts change (value size, header, NUL,
 * inline), not the real runtime's recycling or interning. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LAYOUT
#define LAYOUT 0
#endif
#ifndef VIEWS
#define VIEWS 0
#endif
#if VIEWS && LAYOUT == 0
#error "today's layout has no storable view"
#endif

/* ---- arena: 64 KiB blocks, 8-byte aligned bump, like tycho_rt.c ---- */
typedef struct Blk { struct Blk *next; size_t cap; char mem[]; } Blk;
typedef struct { Blk *head, *cur; size_t off; } Arena;
#define BLK 65536
static void *aalloc(Arena *a, size_t n) {
    n = (n + 7) & ~(size_t)7;
    if (!a->cur || a->off + n > a->cur->cap) {
        Blk *b = a->cur ? a->cur->next : a->head;
        if (!b || b->cap < n) {                      /* need a fresh block */
            size_t cap = n > BLK ? n : BLK;
            Blk *nb = malloc(sizeof(Blk) + cap);
            if (!nb) { perror("malloc"); exit(1); }
            nb->cap = cap;
            nb->next = b;                            /* keep the reuse chain */
            if (a->cur) a->cur->next = nb; else a->head = nb;
            b = nb;
        }
        a->cur = b; a->off = 0;
    }
    void *p = a->cur->mem + a->off;
    a->off += n;
    return p;
}
static void areset(Arena *a) { a->cur = NULL; a->off = 0; }  /* reuse blocks */

/* ---- the three layouts ---- */
#if LAYOUT == 0
typedef char *S;
static inline const char *sd(const S *s) { return *s; }
static inline int64_t sl(const S *s) { return ((const int64_t *)*s)[-1]; }
static S snew(Arena *a, const char *src, int64_t n) {
    char *base = aalloc(a, (size_t)(8 + n + 1));
    *(int64_t *)base = n;
    memcpy(base + 8, src, (size_t)n);
    base[8 + n] = 0;
    return base + 8;
}
#elif LAYOUT == 1
typedef struct { const char *p; int64_t n; } S;
static inline const char *sd(const S *s) { return s->p; }
static inline int64_t sl(const S *s) { return s->n; }
static S snew(Arena *a, const char *src, int64_t n) {
    char *d = aalloc(a, (size_t)n + 1);
    memcpy(d, src, (size_t)n);
    d[n] = 0;
    return (S){d, n};
}
static inline S sview(const char *p, int64_t n) { return (S){p, n}; }
#else
/* heap: {ptr,len}, len < 2^56 so byte 15 (little-endian top) is < 0x80.
 * inline: bytes 0..14 data, byte 15 = 0x80 | len. */
typedef union { struct { const char *p; int64_t n; } h; unsigned char b[16]; } S;
static inline int sin_(const S *s) { return s->b[15] & 0x80; }
static inline const char *sd(const S *s) { return sin_(s) ? (const char *)s->b : s->h.p; }
static inline int64_t sl(const S *s) { return sin_(s) ? (s->b[15] & 0x7f) : s->h.n; }
static S snew(Arena *a, const char *src, int64_t n) {
    S s;
    if (n <= 15) {
        memset(&s, 0, sizeof s);
        memcpy(s.b, src, (size_t)n);
        s.b[15] = (unsigned char)(0x80 | n);
        return s;
    }
    char *d = aalloc(a, (size_t)n + 1);
    memcpy(d, src, (size_t)n);
    d[n] = 0;
    s.h.p = d; s.h.n = n;
    return s;
}
static inline S sview(const char *p, int64_t n) {
    if (n <= 15) return snew(NULL, p, n);            /* inline: no arena */
    S s; s.h.p = p; s.h.n = n; return s;
}
#endif

static int seq(S x, S y) {
    int64_t n = sl(&x);
    return n == sl(&y) && memcmp(sd(&x), sd(&y), (size_t)n) == 0;
}
static S ssub(Arena *a, S s, int64_t i, int64_t j) {
    int64_t n = sl(&s);
    if (i < 0) i = 0;
    if (j > n) j = n;
    if (j < i) j = i;
#if VIEWS
    (void)a;
    return sview(sd(&s) + i, j - i);
#else
    return snew(a, sd(&s) + i, j - i);
#endif
}
typedef struct { S *data; int64_t len, cap; } SArr;
static void spush(Arena *a, SArr *r, S v) {
    if (r->len == r->cap) {                          /* arena grow: copy, old stays */
        int64_t nc = r->cap ? r->cap * 2 : 4;
        S *nd = aalloc(a, sizeof(S) * (size_t)nc);
        if (r->len) memcpy(nd, r->data, sizeof(S) * (size_t)r->len);
        r->data = nd; r->cap = nc;
    }
    r->data[r->len++] = v;
}
static SArr ssplit(Arena *a, S s, char sep) {
    SArr r = {0};
    const char *d = sd(&s);
    int64_t n = sl(&s), st = 0;
    for (int64_t i = 0; i <= n; i++) {
        if (i == n || d[i] == sep) {
#if VIEWS
            spush(a, &r, sview(d + st, i - st));
#else
            spush(a, &r, snew(a, d + st, i - st));
#endif
            st = i + 1;
        }
    }
    return r;
}

static uint64_t rng = 88172645463325252ull;
static uint64_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static uint64_t fnv(uint64_t h, S s) {
    const unsigned char *d = (const unsigned char *)sd(&s);
    for (int64_t i = 0, n = sl(&s); i < n; i++) h = (h ^ d[i]) * 1099511628211ull;
    return h;
}

/* ---- workload 1: Goose's order records ---- */
typedef struct { S sku; int64_t qty, cents; } Item;
typedef struct { Item *data; int64_t len, cap; } ItemArr;
typedef struct { int64_t id; S customer; ItemArr items; } Order;

static void orders(int64_t N) {
    Arena a = {0};
    Order *os = aalloc(&a, sizeof(Order) * (size_t)N);
    char buf[32];
    for (int64_t i = 0; i < N; i++) {
        int L = 4 + (int)(rnd() % 21);               /* customer: 4..24 bytes */
        for (int k = 0; k < L; k++) buf[k] = (char)('a' + rnd() % 26);
        os[i].id = i;
        os[i].customer = snew(&a, buf, L);
        int m = 1 + (int)(rnd() % 4);
        os[i].items.data = aalloc(&a, sizeof(Item) * (size_t)m);
        os[i].items.len = os[i].items.cap = m;
        for (int k = 0; k < m; k++) {
            int n = snprintf(buf, sizeof buf, "SKU-%06d", (int)(rnd() % 1000000));
            Item *it = &os[i].items.data[k];
            it->sku = snew(&a, buf, n);
            it->qty = 1 + (int64_t)(rnd() % 9);
            it->cents = 100 + (int64_t)(rnd() % 99900);
        }
    }
    S key = snew(&a, "SKU-000042", 10);
    uint64_t h = 14695981039346656037ull;
    int64_t total = 0, hits = 0, ids = 0;
    for (int64_t i = 0; i < N; i++) {
        ids += os[i].id;
        h = fnv(h, os[i].customer);
        for (int64_t k = 0; k < os[i].items.len; k++) {
            Item *it = &os[i].items.data[k];
            h = fnv(h, it->sku);
            total += it->qty * it->cents;
            hits += seq(it->sku, key);
        }
    }
    printf("orders n=%lld ids=%lld total=%lld hits=%lld hash=%016llx\n",
           (long long)N, (long long)ids, (long long)total, (long long)hits,
           (unsigned long long)h);
}

/* ---- workload 2: split/substring-heavy loop with per-line scratch ---- */
static void split_loop(int64_t lines, int passes) {
    size_t cap = (size_t)lines * 8 * 21 + 1, n = 0;
    char *text = malloc(cap);
    for (int64_t i = 0; i < lines; i++) {
        for (int f = 0; f < 8; f++) {
            int L = 1 + (int)(rnd() % 20);           /* field: 1..20 bytes */
            for (int k = 0; k < L; k++) text[n++] = (char)('a' + rnd() % 4);
            text[n++] = f == 7 ? '\n' : ',';
        }
    }
    Arena scratch = {0}, keep = {0};
    S key = snew(&keep, "abca", 4);
    uint64_t h = 14695981039346656037ull;
    int64_t tot = 0, hits = 0;
    for (int p = 0; p < passes; p++) {
        size_t st = 0;
        for (size_t i = 0; i < n; i++) {
            if (text[i] != '\n') continue;
            S line = snew(&scratch, text + st, (int64_t)(i - st));   /* read_line */
            SArr parts = ssplit(&scratch, line, ',');
            for (int64_t k = 0; k < parts.len; k++) {
                S part = parts.data[k];
                tot += sl(&part);
                h = fnv(h, ssub(&scratch, part, 1, 4));
                hits += seq(part, key);
            }
            areset(&scratch);
            st = i + 1;
        }
    }
    printf("split lines=%lld passes=%d tot=%lld hits=%lld hash=%016llx\n",
           (long long)lines, passes, (long long)tot, (long long)hits,
           (unsigned long long)h);
    free(text);
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "orders") == 0) {
        orders(argc > 2 ? atoll(argv[2]) : 1000000);
    } else if (argc >= 2 && strcmp(argv[1], "split") == 0) {
        split_loop(argc > 2 ? atoll(argv[2]) : 200000, argc > 3 ? atoi(argv[3]) : 5);
    } else {
        fprintf(stderr, "usage: probe orders [N] | probe split [lines] [passes]\n");
        return 2;
    }
    return 0;
}
