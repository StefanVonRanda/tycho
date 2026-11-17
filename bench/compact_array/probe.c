/* Compact array descriptor probe (plan phase 3.1, measurement half).
 *
 * Models two layouts of a Tycho `[T]` value over the same arena:
 *   default     24-byte {data,len,cap} value (runtime/tycho_rt.c TychoArrInt)
 *   -DCOMPACT   8-byte pointer to a {len,cap}-prefixed buffer, NULL = empty
 *
 * The arena mirrors tycho_rt.c: 64 KiB bump blocks (TYCHO_BLOCK_DEFAULT), 8-byte
 * rounding, growth cap 0 -> 4 -> x2 with the old buffer recycled into an
 * uncapped per-8-byte-class bucket for sizes < 128 B (TYCHO_NBKT 16) and a
 * 32-entry best-fit list above that (TYCHO_FREECAP). Oversized requests get a
 * block of their own, as arena_alloc_slow does.
 *
 *   probe struct3|adj|orders      prints a checksum; run under bench/peakrss
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int64_t I;

/* ---- arena -------------------------------------------------------------- */
#define BLOCK (1u << 16)
#define NBKT 16
#define FREECAP 32
typedef struct FreeNode { size_t size; struct FreeNode *next; } FreeNode;
static char *blk; static size_t off = BLOCK, cap = BLOCK;
static FreeNode *bkt[NBKT], *freelist; static int nfree;

static void *aalloc(size_t n) {
    n = (n + 7u) & ~(size_t)7u;
    size_t k = n >> 3;
    if (k < NBKT && bkt[k]) { FreeNode *f = bkt[k]; bkt[k] = f->next; return f; }
    if (k >= NBKT && freelist) {                       /* best fit */
        FreeNode **best = NULL;
        for (FreeNode **p = &freelist; *p; p = &(*p)->next)
            if ((*p)->size >= n && (!best || (*p)->size < (*best)->size)) best = p;
        if (best) { FreeNode *f = *best; *best = f->next; nfree--; return f; }
    }
    if (n > BLOCK / 2) { void *p = malloc(n); if (!p) abort(); return p; }
    if (off + n > cap) { blk = malloc(BLOCK); if (!blk) abort(); off = 0; cap = BLOCK; }
    void *p = blk + off; off += n; return p;
}
static void arecycle(void *p, size_t n) {
    if (!p || n < sizeof(FreeNode)) return;
    n = (n + 7u) & ~(size_t)7u;
    FreeNode *f = p; f->size = n;
    if ((n >> 3) < NBKT) { f->next = bkt[n >> 3]; bkt[n >> 3] = f; return; }
    if (nfree >= FREECAP) return;
    f->next = freelist; freelist = f; nfree++;
}

/* ---- the two array representations ---------------------------------------- */
#ifndef COMPACT
typedef struct { void *data; I len, cap; } Arr;            /* 24 B */
#define AEMPTY ((Arr){0, 0, 0})
#define ALEN(a) ((a).len)
#define APLEN(a) ((a).len)
#define ADATA(T, a) ((T *)(a).data)
static void agrow(Arr *a, size_t es) {
    I nc = a->cap ? a->cap * 2 : 4;
    void *nd = aalloc((size_t)nc * es);
    if (a->len) memcpy(nd, a->data, (size_t)a->len * es);
    if (a->cap) arecycle(a->data, (size_t)a->cap * es);
    a->data = nd; a->cap = nc;
}
#define APUSH(T, a, v) do { if ((a).len == (a).cap) agrow(&(a), sizeof(T)); \
                            ((T *)(a).data)[(a).len++] = (v); } while (0)
#else
typedef struct { I len, cap; } Hdr;
typedef Hdr *Arr;                                          /* 8 B, NULL = empty */
#define AEMPTY ((Arr)NULL)
#define ALEN(a) ((a) ? (a)->len : 0)
#define APLEN(a) ((a)->len)                               /* non-empty only */
#define ADATA(T, a) ((T *)((a) + 1))
static void agrow(Arr *a, size_t es) {
    I oc = *a ? (*a)->cap : 0, nc = oc ? oc * 2 : 4;
    Hdr *nh = aalloc(sizeof(Hdr) + (size_t)nc * es);
    nh->len = 0;
    if (*a) { nh->len = (*a)->len; memcpy(nh + 1, *a + 1, (size_t)nh->len * es);
              arecycle(*a, sizeof(Hdr) + (size_t)oc * es); }
    nh->cap = nc; *a = nh;
}
#define APUSH(T, a, v) do { if (!(a) || (a)->len == (a)->cap) agrow(&(a), sizeof(T)); \
                            ((T *)((a) + 1))[(a)->len++] = (v); } while (0)
#endif

static uint64_t st = 88172645463325252ull;
static I rnd(I m) { st = st * 6364136223846793005ull + 1442695040888963407ull;
                    return (I)((st >> 33) % (uint64_t)m); }

/* ---- 1: struct with 3 array fields x 1M, mostly empty/short ----------------- */
typedef struct { I id; Arr tags, refs, hist; } Rec;
static I w_struct3(void) {
    Arr recs = AEMPTY;
    for (I i = 0; i < 1000000; i++) {
        Rec r = { i, AEMPTY, AEMPTY, AEMPTY };
        if (rnd(10) < 4) for (I k = rnd(3) + 1; k > 0; k--) APUSH(I, r.tags, rnd(1000)); /* 60% empty */
        if (rnd(10) < 2) for (I k = rnd(8) + 1; k > 0; k--) APUSH(I, r.refs, rnd(1000)); /* 80% empty */
        for (I k = rnd(3); k > 0; k--) APUSH(I, r.hist, rnd(1000));                    /* 0..2 */
        APUSH(Rec, recs, r);
    }
    I sum = 0;
    for (int pass = 0; pass < 5; pass++)
        for (I i = 0; i < ALEN(recs); i++) {
            Rec *r = &ADATA(Rec, recs)[i];
            sum += ALEN(r->tags) + 3 * ALEN(r->refs) + 7 * ALEN(r->hist);
            for (I k = 0; k < ALEN(r->hist); k++) sum += ADATA(I, r->hist)[k];
        }
    return sum;
}

/* ---- 2: [[Edge]] adjacency + Dijkstra, shaped like bench/dijkstra ---------- */
typedef struct { I to, w; } Edge;
typedef struct { I d, node; } HE;
static I w_adj(void) {
    const I n = 300000, deg = 4;
    Arr adj = AEMPTY;
    for (I u = 0; u < n; u++) APUSH(Arr, adj, AEMPTY);
    for (I u = 0; u < n; u++)
        for (I k = 0; k < deg; k++) { Edge e = { rnd(n), rnd(100) + 1 };
                                      APUSH(Edge, ADATA(Arr, adj)[u], e); }
    const I inf = 4000000000000000000ll;
    Arr dist = AEMPTY, heap = AEMPTY;
    for (I i = 0; i < n; i++) APUSH(I, dist, inf);
    I *d = ADATA(I, dist); d[0] = 0;
    APUSH(HE, heap, ((HE){0, 0}));
    while (ALEN(heap) > 0) {
        HE *h = ADATA(HE, heap), top = h[0];
        I m = --APLEN(heap);               /* pop */
        h[0] = h[m];
        for (I i = 0;;) { I l = 2*i+1, r = l+1, s = i;
            if (l < m && h[l].d < h[s].d) s = l;
            if (r < m && h[r].d < h[s].d) s = r;
            if (s == i) break; HE t = h[i]; h[i] = h[s]; h[s] = t; i = s; }
        if (top.d > d[top.node]) continue;
        Arr out = ADATA(Arr, adj)[top.node];
        for (I j = 0; j < ALEN(out); j++) {
            Edge e = ADATA(Edge, out)[j]; I nd = top.d + e.w;
            if (nd < d[e.to]) { d[e.to] = nd; APUSH(HE, heap, ((HE){nd, e.to}));
                h = ADATA(HE, heap);
                for (I i = ALEN(heap) - 1; i > 0;) { I p = (i - 1) / 2;
                    if (h[p].d <= h[i].d) break; HE t = h[i]; h[i] = h[p]; h[p] = t; i = p; } }
        }
    }
    I sum = 0, reach = 0;
    for (I i = 0; i < n; i++) if (d[i] < inf) { sum += d[i]; reach++; }
    return sum * 1000003 + reach;
}

/* ---- 3: [Order] each holding [Item], 20% empty, 1..5 items otherwise ------- */
typedef struct { I sku, qty, price; } Item;
typedef struct { I id; Arr items; I total; } Order;
static I w_orders(void) {
    Arr orders = AEMPTY;
    for (I i = 0; i < 1000000; i++) {
        Order o = { i, AEMPTY, 0 };
        if (rnd(10) >= 2)
            for (I k = rnd(5) + 1; k > 0; k--) { Item it = { rnd(50000), rnd(4) + 1, rnd(9999) };
                                                 APUSH(Item, o.items, it); }
        APUSH(Order, orders, o);
    }
    I grand = 0;
    for (int pass = 0; pass < 5; pass++)
        for (I i = 0; i < ALEN(orders); i++) {
            Order *o = &ADATA(Order, orders)[i];
            I t = 0;
            for (I k = 0; k < ALEN(o->items); k++) { Item *it = &ADATA(Item, o->items)[k];
                                                    t += it->qty * it->price; }
            o->total = t; grand += t;
        }
    return grand;
}

int main(int argc, char **argv) {
    const char *w = argc > 1 ? argv[1] : "";
    I r = !strcmp(w, "struct3") ? w_struct3() : !strcmp(w, "adj") ? w_adj()
        : !strcmp(w, "orders") ? w_orders() : (fprintf(stderr, "usage: probe struct3|adj|orders\n"), exit(2), 0);
    printf("%s %lld\n", w, (long long)r);
    return 0;
}
