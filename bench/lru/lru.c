/* LRU cache head-to-head — C reference. A fixed-capacity LRU driven by the same
 * shared LCG op-stream as lru.ty / lru.go, folding hits + returned values into a
 * checksum. The cache is a hash map (key -> node) + a doubly-linked recency list,
 * the textbook pointer design tycho cannot express (it uses an index pool). The
 * map is open-addressing with linear-probe BACKWARD-SHIFT deletion (no tombstones,
 * so a delete-heavy churn stays O(1) per op). Same op stream + same LRU policy =>
 * byte-identical `hits sum` checksum across all three ports. */
#include <stdio.h>

#define CAP      200000L
#define KEYSPACE 600000L
#define NOPS     5000000L
#define HT       (1L << 19)        /* 524288 > 2*CAP, load < 0.4 */
#define HMASK    (HT - 1)

typedef struct { long key, val, prev, next; } Node;

static Node pool[CAP];
static long ht[HT];                /* pool index in each slot, or -1 = empty */
static long head = -1, tail = -1, size = 0;

static inline unsigned long mix(long k) {   /* SplitMix64 finalizer (unseeded; a fixed hash is fine for a benchmark) */
    unsigned long x = (unsigned long)k + 0x9e3779b97f4a7c15UL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9UL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebUL;
    return x ^ (x >> 31);
}

static long ht_find(long key) {             /* ht index holding key, or -1 */
    long i = (long)(mix(key) & HMASK);
    while (ht[i] != -1) {
        if (pool[ht[i]].key == key) return i;
        i = (i + 1) & HMASK;
    }
    return -1;
}
static void ht_insert(long key, long slot) {
    long i = (long)(mix(key) & HMASK);
    while (ht[i] != -1) i = (i + 1) & HMASK;
    ht[i] = slot;
}
static void ht_remove(long key) {           /* linear-probe backward-shift delete (Knuth 6.4 alg. R) */
    long i = ht_find(key);
    if (i < 0) return;
    long j = i;
    for (;;) {
        ht[i] = -1;
    r2:
        j = (j + 1) & HMASK;
        if (ht[j] == -1) return;
        long k = (long)(mix(pool[ht[j]].key) & HMASK);
        if (i <= j) { if (i < k && k <= j) goto r2; }   /* k cyclically in (i,j]: must stay */
        else        { if (i < k || k <= j) goto r2; }
        ht[i] = ht[j];
        i = j;
    }
}

static void unlink_node(long i) {
    long p = pool[i].prev, nx = pool[i].next;
    if (p == -1) head = nx; else pool[p].next = nx;
    if (nx == -1) tail = p; else pool[nx].prev = p;
}
static void push_front(long i) {
    pool[i].prev = -1; pool[i].next = head;
    if (head == -1) tail = i; else pool[head].prev = i;
    head = i;
}

int main(void) {
    for (long i = 0; i < HT; i++) ht[i] = -1;
    unsigned long long state = 88172645463325252ULL;
    long hits = 0, sum = 0;
    for (long op = 0; op < NOPS; op++) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        long key = (long)(state & 1073741823ULL) % KEYSPACE;
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        long r = (long)(state & 1073741823ULL);
        if (r % 100 < 70) {                                 /* GET */
            long hi = ht_find(key);
            if (hi >= 0) {
                long s = ht[hi];
                unlink_node(s); push_front(s);
                hits++; sum += pool[s].val;
            }
        } else {                                            /* PUT */
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            long val = (long)(state & 1073741823ULL) % 1000000;
            long hi = ht_find(key);
            if (hi >= 0) {
                long s = ht[hi];
                pool[s].val = val;
                unlink_node(s); push_front(s);
            } else if (size >= CAP) {                       /* full: recycle the LRU tail slot in place */
                long t = tail;
                unlink_node(t);
                ht_remove(pool[t].key);
                pool[t].key = key; pool[t].val = val;
                ht_insert(key, t);
                push_front(t);
            } else {
                long s = size++;
                pool[s].key = key; pool[s].val = val;
                ht_insert(key, s);
                push_front(s);
            }
        }
    }
    printf("%ld %ld\n", hits, sum);
    return 0;
}
