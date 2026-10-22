/* binary-trees, C -- expert arena control: bump-allocate nodes, reset per tree.
 * Same output and depth schedule as binary_trees.c. */
#include <stdio.h>
#include <stdlib.h>
typedef struct Tree { struct Tree *l, *r; } Tree;
typedef struct { Tree *nodes; size_t used, cap; } Arena;
static void arena_init(Arena *a, size_t cap) {
    a->nodes = (Tree *)malloc(cap * sizeof(Tree));
    a->used = 0; a->cap = cap;
}
static Tree *alloc(Arena *a) {
    if (a->used == a->cap) { fprintf(stderr, "arena exhausted\n"); exit(1); }
    return &a->nodes[a->used++];
}
static Tree *make(Arena *a, int d) {
    Tree *t = alloc(a);
    if (d == 0) t->l = t->r = NULL;
    else { t->l = make(a, d - 1); t->r = make(a, d - 1); }
    return t;
}
static long check(Tree *t) {
    if (!t->l) return 1;
    return 1 + check(t->l) + check(t->r);
}
int main(void) {
    int mind = 4, maxd = 18, stretch = maxd + 1;
    Arena scratch, live;
    arena_init(&scratch, (size_t)1 << (maxd + 2));
    arena_init(&live, (size_t)1 << (maxd + 2));
    Tree *s = make(&scratch, stretch);
    printf("stretch tree of depth %d check: %ld\n", stretch, check(s));
    scratch.used = 0;
    Tree *longlived = make(&live, maxd);
    for (int d = mind; d <= maxd; d += 2) {
        long iters = 1; for (int k = 0; k < maxd - d + mind; k++) iters *= 2;
        long sum = 0;
        for (long i = 0; i < iters; i++) {
            make(&scratch, d);
            sum += check(&scratch.nodes[0]);
            scratch.used = 0;
        }
        printf("%ld trees of depth %d check: %ld\n", iters, d, sum);
    }
    printf("long-lived tree of depth %d check: %ld\n", maxd, check(longlived));
    free(scratch.nodes); free(live.nodes);
    return 0;
}
