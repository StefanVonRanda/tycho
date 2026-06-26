/* Tree-walking interpreter — C reference. Build the SAME deterministic expression
 * AST as interp.ty (an LCG-threaded recursive generator), eval it, constant-fold it
 * (a rewrite pass allocating a new AST), count nodes, and deep-compare original vs
 * folded -- folding results into a `n eval foldedN foldedEval same` checksum. The AST
 * is the memory under test: tycho's value-semantic recursive enum vs C's tagged union
 * + malloc'd nodes vs Go's struct pointers. Same generator + same eval => byte-
 * identical checksum across the three ports. Compiled -fwrapv so signed overflow wraps
 * (two's complement), matching tycho's integer contract. No frees: build-and-hold. */
#include <stdio.h>
#include <stdlib.h>

#define DEPTH 20

typedef struct Expr Expr;
struct Expr {
    int tag;                 /* 0=Lit 1=Bin 2=If */
    union {
        long lit;
        struct { int op; Expr *l, *r; } bin;            /* op: 0=+ 1=- 2=* */
        struct { Expr *cond, *then, *els; } iff;
    } u;
};

static Expr *mk(int tag) { Expr *e = (Expr *)malloc(sizeof(Expr)); e->tag = tag; return e; }

static Expr *gen(int depth, unsigned long long *seed) {
    *seed = *seed * 6364136223846793005ULL + 1442695040888963407ULL;
    if (depth <= 0 || (*seed & 7) == 0) { Expr *e = mk(0); e->u.lit = (long)((*seed >> 10) & 63); return e; }
    int k = (int)((*seed >> 18) & 3);
    Expr *l = gen(depth - 1, seed);
    Expr *r = gen(depth - 1, seed);
    if (k == 3) { Expr *c = gen(depth - 1, seed); Expr *e = mk(2); e->u.iff.cond = l; e->u.iff.then = r; e->u.iff.els = c; return e; }
    Expr *e = mk(1); e->u.bin.op = k; e->u.bin.l = l; e->u.bin.r = r; return e;
}

static long eval(Expr *e) {
    if (e->tag == 0) return e->u.lit;
    if (e->tag == 1) {
        long a = eval(e->u.bin.l), b = eval(e->u.bin.r);
        if (e->u.bin.op == 0) return a + b;
        if (e->u.bin.op == 1) return a - b;
        return a * b;
    }
    if (eval(e->u.iff.cond) != 0) return eval(e->u.iff.then);
    return eval(e->u.iff.els);
}

static long count(Expr *e) {
    if (e->tag == 0) return 1;
    if (e->tag == 1) return 1 + count(e->u.bin.l) + count(e->u.bin.r);
    return 1 + count(e->u.iff.cond) + count(e->u.iff.then) + count(e->u.iff.els);
}

static Expr *fold(Expr *e) {
    if (e->tag == 0) { Expr *n = mk(0); n->u.lit = e->u.lit; return n; }
    if (e->tag == 1) {
        Expr *fl = fold(e->u.bin.l), *fr = fold(e->u.bin.r);
        if (fl->tag == 0 && fr->tag == 0) {
            long a = fl->u.lit, b = fr->u.lit, v;
            if (e->u.bin.op == 0) v = a + b; else if (e->u.bin.op == 1) v = a - b; else v = a * b;
            Expr *n = mk(0); n->u.lit = v; return n;
        }
        Expr *n = mk(1); n->u.bin.op = e->u.bin.op; n->u.bin.l = fl; n->u.bin.r = fr; return n;
    }
    Expr *n = mk(2); n->u.iff.cond = fold(e->u.iff.cond); n->u.iff.then = fold(e->u.iff.then); n->u.iff.els = fold(e->u.iff.els); return n;
}

static int eq(Expr *a, Expr *b) {
    if (a->tag != b->tag) return 0;
    if (a->tag == 0) return a->u.lit == b->u.lit;
    if (a->tag == 1) return a->u.bin.op == b->u.bin.op && eq(a->u.bin.l, b->u.bin.l) && eq(a->u.bin.r, b->u.bin.r);
    return eq(a->u.iff.cond, b->u.iff.cond) && eq(a->u.iff.then, b->u.iff.then) && eq(a->u.iff.els, b->u.iff.els);
}

int main(void) {
    unsigned long long seed = 88172645463325252ULL;
    Expr *t = gen(DEPTH, &seed);
    long n = count(t);
    long ev = eval(t);
    Expr *ft = fold(t);
    long fn2 = count(ft);
    long fev = eval(ft);
    int same = eq(t, ft);
    printf("%ld %ld %ld %ld %s\n", n, ev, fn2, fev, same ? "true" : "false");
    return 0;
}
