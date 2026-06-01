#include <stdio.h>
#include <stdlib.h>
typedef struct Tree { int leaf; long n; struct Tree *l, *r; } Tree;
static Tree *leaf(long n){ Tree*t=malloc(sizeof(Tree)); t->leaf=1; t->n=n; t->l=t->r=0; return t; }
static Tree *node(Tree*l,Tree*r){ Tree*t=malloc(sizeof(Tree)); t->leaf=0; t->l=l; t->r=r; return t; }
static Tree *build(int d){ return d==0 ? leaf(1) : node(build(d-1),build(d-1)); }
static Tree *maptree(Tree*t){ return t->leaf ? leaf(t->n+1) : node(maptree(t->l),maptree(t->r)); }
static long checksum(Tree*t){ return t->leaf ? t->n : checksum(t->l)+checksum(t->r); }
static void freetree(Tree*t){ if(!t->leaf){ freetree(t->l); freetree(t->r);} free(t); }
int main(void){ Tree*t=build(16); long sum=0; for(int i=0;i<200;i++){ Tree*m=maptree(t); sum+=checksum(m); freetree(m);} printf("%ld\n",sum); freetree(t); return 0; }
