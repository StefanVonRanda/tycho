/* Dijkstra head-to-head, C port. Same LCG graph + checksum as the tycho/Go ports.
 * Adjacency list as a per-node grown Edge array (Edge** + lengths) -- the C analogue
 * of tycho's `[[Edge]]`. A binary min-heap with lazy deletion. Distances are
 * tie-break independent, so any correct shortest-path reproduces the checksum. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct { int to; long w; } Edge;

typedef struct { long d; int node; } HItem;
typedef struct { HItem *a; long len, cap; } Heap;

static void hpush(Heap *h, long d, int node) {
    if (h->len == h->cap) { h->cap = h->cap ? h->cap * 2 : 1024; h->a = realloc(h->a, (size_t)h->cap * sizeof(HItem)); }
    long i = h->len++;
    h->a[i].d = d; h->a[i].node = node;
    while (i > 0) { long p = (i - 1) / 2; if (h->a[p].d <= h->a[i].d) break; HItem t = h->a[p]; h->a[p] = h->a[i]; h->a[i] = t; i = p; }
}
static HItem hpop(Heap *h) {
    HItem top = h->a[0];
    h->a[0] = h->a[--h->len];
    long i = 0;
    for (;;) {
        long l = 2 * i + 1, r = 2 * i + 2, s = i;
        if (l < h->len && h->a[l].d < h->a[s].d) s = l;
        if (r < h->len && h->a[r].d < h->a[s].d) s = r;
        if (s == i) break;
        HItem t = h->a[i]; h->a[i] = h->a[s]; h->a[s] = t; i = s;
    }
    return top;
}

int main(void) {
    int n = 300000, deg = 4;
    uint64_t state = 88172645463325252ULL;
    Edge **adj = calloc((size_t)n, sizeof(Edge *));
    int *cnt = calloc((size_t)n, sizeof(int));
    for (int u = 0; u < n; u++) {
        adj[u] = malloc((size_t)deg * sizeof(Edge));
        for (int k = 0; k < deg; k++) {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            int to = (int)((state & 1073741823ULL) % (uint64_t)n);
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            long w = (long)((state & 1073741823ULL) % 100ULL) + 1;
            adj[u][k].to = to; adj[u][k].w = w;
        }
        cnt[u] = deg;
    }
    const long INF = 4000000000000000000L;
    long *dist = malloc((size_t)n * sizeof(long));
    for (int i = 0; i < n; i++) dist[i] = INF;
    dist[0] = 0;
    Heap h = {0};
    hpush(&h, 0, 0);
    while (h.len > 0) {
        HItem c = hpop(&h);
        long d = c.d; int u = c.node;
        if (d > dist[u]) continue;
        for (int j = 0; j < cnt[u]; j++) {
            Edge e = adj[u][j];
            long nd = d + e.w;
            if (nd < dist[e.to]) { dist[e.to] = nd; hpush(&h, nd, e.to); }
        }
    }
    long sum = 0, reach = 0;
    for (int i = 0; i < n; i++) if (dist[i] < INF) { sum += dist[i]; reach++; }
    printf("%ld %ld\n", reach, sum);
    return 0;
}
