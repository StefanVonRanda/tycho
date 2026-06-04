/* Inverted-index build, map-native form — C, manual malloc/realloc. The map value
 * IS the growing posting list (string -> {long* docs, n, cap}), mirroring
 * invindex_map.hi's [string: [int]] with push(idx[term], d). Same LCG corpus and
 * same checksum as invindex_map.hi (the cross-language oracle): every (term, doc)
 * occurrence is appended (no freq dedup). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* string -> posting list, open addressing, FNV-1a, power-of-two cap. The value
 * (docs/n/cap) lives inline in the table, grown in place by realloc -- the direct
 * analogue of push(idx[term], d). */
typedef struct { char *key; long *docs; long n, cap; } Slot;
static Slot *tab; static long tcap, tlen;
static unsigned long fnv(const char *s){ unsigned long h=1469598103934665603UL; for(;*s;s++){ h^=(unsigned char)*s; h*=1099511628211UL; } return h; }
static void tgrow(void){
    long nc = tcap ? tcap*2 : 16;
    Slot *nt = (Slot*)calloc(nc, sizeof(Slot));
    for (long i=0;i<tcap;i++) if (tab[i].key) { long j=fnv(tab[i].key)&(nc-1); while(nt[j].key) j=(j+1)&(nc-1); nt[j]=tab[i]; }
    free(tab); tab=nt; tcap=nc;
}
/* find-or-insert the slot for k, then append doc to its list (grow in place). */
static void add(const char *term, long doc){
    if ((tlen+1)*2 > tcap) tgrow();
    long j = fnv(term)&(tcap-1);
    while (tab[j].key) { if (!strcmp(tab[j].key, term)) break; j=(j+1)&(tcap-1); }
    if (!tab[j].key) { tab[j].key = strdup(term); tab[j].docs=NULL; tab[j].n=0; tab[j].cap=0; tlen++; }
    Slot *s = &tab[j];
    if (s->n >= s->cap) { s->cap = s->cap ? s->cap*2 : 4; s->docs=(long*)realloc(s->docs, s->cap*sizeof(long)); }
    s->docs[s->n++] = doc;
}

int main(void){
    long n=300000, w=12, v=8000;
    long seed=1; char buf[32];
    for (long d=0; d<n; d++)
        for (long j=0; j<w; j++) {
            seed = (seed*1103515245 + 12345) % 2147483647;
            long tid = seed % v;
            snprintf(buf, sizeof buf, "t%ld", tid);
            add(buf, d);
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
