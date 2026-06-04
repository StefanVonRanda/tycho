/* Inverted-index build, map-native COUNT-FILL — C. Same as invindex_map.c but each
 * posting list is malloc'd ONCE at its exact length (a first counting pass), no realloc
 * growth. The analogue of invindex_map_exact.hi's reserve(idx[term], count). Same LCG
 * corpus and checksum (540001838890). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *key; long *docs; long n, cap; } Slot;
static Slot *tab; static long tcap, tlen;
static unsigned long fnv(const char *s){ unsigned long h=1469598103934665603UL; for(;*s;s++){ h^=(unsigned char)*s; h*=1099511628211UL; } return h; }
static void tgrow(void){
    long nc = tcap ? tcap*2 : 16;
    Slot *nt = (Slot*)calloc(nc, sizeof(Slot));
    for (long i=0;i<tcap;i++) if (tab[i].key) { long j=fnv(tab[i].key)&(nc-1); while(nt[j].key) j=(j+1)&(nc-1); nt[j]=tab[i]; }
    free(tab); tab=nt; tcap=nc;
}
static Slot *slot(const char *term){
    if ((tlen+1)*2 > tcap) tgrow();
    long j = fnv(term)&(tcap-1);
    while (tab[j].key) { if (!strcmp(tab[j].key, term)) return &tab[j]; j=(j+1)&(tcap-1); }
    tab[j].key = strdup(term); tab[j].docs=NULL; tab[j].n=0; tab[j].cap=0; tlen++;
    return &tab[j];
}

int main(void){
    long n=300000, w=12, v=8000;
    long seed; char buf[32];
    /* pass 1: count postings per term (cap field doubles as the counter) */
    seed=1;
    for (long d=0; d<n; d++)
        for (long j=0; j<w; j++) {
            seed = (seed*1103515245 + 12345) % 2147483647;
            snprintf(buf, sizeof buf, "t%ld", seed % v);
            slot(buf)->cap++;
        }
    /* size each list once at its exact count */
    for (long i=0;i<tcap;i++) if (tab[i].key) tab[i].docs = (long*)malloc(tab[i].cap*sizeof(long));
    /* pass 2: replay the LCG, fill (n is the running fill index) */
    seed=1;
    for (long d=0; d<n; d++)
        for (long j=0; j<w; j++) {
            seed = (seed*1103515245 + 12345) % 2147483647;
            snprintf(buf, sizeof buf, "t%ld", seed % v);
            Slot *s = slot(buf);
            s->docs[s->n++] = d;
        }
    long sum=0, vocab=0;
    for (long i=0;i<tcap;i++) if (tab[i].key) {
        vocab++;
        sum += (long)strlen(tab[i].key) + tab[i].n;
        for (long k=0;k<tab[i].n;k++) sum += tab[i].docs[k];
    }
    printf("vocab=%ld checksum=%ld\n", vocab, sum);
    return 0;
}
