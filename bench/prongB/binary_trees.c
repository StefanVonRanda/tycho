/* binary-trees, C — manual malloc/free per node (the no-arena baseline). */
#include <stdio.h>
#include <stdlib.h>
typedef struct Tree { struct Tree *l, *r; } Tree;
static Tree *make(int d) {
    Tree *t = malloc(sizeof(Tree));
    if (d == 0) { t->l = t->r = NULL; }
    else { t->l = make(d - 1); t->r = make(d - 1); }
    return t;
}
static long check(Tree *t) {
    if (!t->l) return 1;
    return 1 + check(t->l) + check(t->r);
}
static void freetree(Tree *t) {
    if (t->l) { freetree(t->l); freetree(t->r); }
    free(t);
}
int main(void) {
    int mind = 4, maxd = 18, stretch = maxd + 1;
    Tree *s = make(stretch);
    printf("stretch tree of depth %d check: %ld\n", stretch, check(s)); freetree(s);
    Tree *longlived = make(maxd);
    for (int d = mind; d <= maxd; d += 2) {
        long iters = 1; for (int k = 0; k < maxd - d + mind; k++) iters *= 2;
        long sum = 0;
        for (long i = 0; i < iters; i++) { Tree *t = make(d); sum += check(t); freetree(t); }
        printf("%ld trees of depth %d check: %ld\n", iters, d, sum);
    }
    printf("long-lived tree of depth %d check: %ld\n", maxd, check(longlived));
    freetree(longlived);
    return 0;
}
