/* maptree, C -- expert arena control: persistent tree plus per-rewrite arena. */
#include <stdio.h>
#include <stdlib.h>
typedef struct Tree { int leaf; long n; struct Tree *l, *r; } Tree;
typedef struct { Tree *nodes; size_t used, cap; } Arena;
static void arena_init(Arena *a, size_t cap) {
    a->nodes = (Tree *)malloc(cap * sizeof(Tree));
    a->used = 0; a->cap = cap;
}
static Tree *alloc(Arena *a) {
    if (a->used == a->cap) { fprintf(stderr, "arena exhausted\n"); exit(1); }
    return &a->nodes[a->used++];
}
static Tree *leaf(Arena *a, long n) { Tree *t = alloc(a); t->leaf = 1; t->n = n; t->l = t->r = 0; return t; }
static Tree *node(Arena *a, Tree *l, Tree *r) { Tree *t = alloc(a); t->leaf = 0; t->n = 0; t->l = l; t->r = r; return t; }
static Tree *build(Arena *a, int d) { return d == 0 ? leaf(a, 1) : node(a, build(a, d - 1), build(a, d - 1)); }
static Tree *maptree(Arena *a, Tree *t) { return t->leaf ? leaf(a, t->n + 1) : node(a, maptree(a, t->l), maptree(a, t->r)); }
static long checksum(Tree *t) { return t->leaf ? t->n : checksum(t->l) + checksum(t->r); }
int main(void) {
    Arena live, scratch;
    arena_init(&live, (size_t)1 << 20);
    arena_init(&scratch, (size_t)1 << 20);
    Tree *t = build(&live, 16);
    long sum = 0;
    for (int i = 0; i < 200; i++) {
        Tree *m = maptree(&scratch, t);
        sum += checksum(m);
        scratch.used = 0;
    }
    printf("%ld\n", sum);
    free(live.nodes); free(scratch.nodes);
    return 0;
}
