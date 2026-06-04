/* Inverted-index build, at scale — C, manual malloc/free/realloc. Same LCG,
 * same add logic, same checksum as invindex.hi (the cross-language oracle).
 * A hand-written open-addressing string->slot hash map + a growing Posting
 * array whose elements own realloc'd doc/freq arrays. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *term; long *docs; long *freqs; long n, cap; } Posting;
static Posting *post; static long npost, postcap;

/* string -> slot+1 (0 = absent), open addressing, FNV-1a, power-of-two cap */
static char **mkeys; static long *mvals; static long mcap, mlen;
static unsigned long fnv(const char *s){ unsigned long h=1469598103934665603UL; for(;*s;s++){ h^=(unsigned char)*s; h*=1099511628211UL; } return h; }
static void mgrow(void){
    long nc = mcap ? mcap*2 : 16;
    char **nk = (char**)calloc(nc, sizeof(char*)); long *nv = (long*)calloc(nc, sizeof(long));
    for (long i=0;i<mcap;i++) if (mkeys[i]) { long j=fnv(mkeys[i])&(nc-1); while(nk[j]) j=(j+1)&(nc-1); nk[j]=mkeys[i]; nv[j]=mvals[i]; }
    free(mkeys); free(mvals); mkeys=nk; mvals=nv; mcap=nc;
}
static long mget(const char *k){ if(!mcap) return 0; long j=fnv(k)&(mcap-1); while(mkeys[j]){ if(!strcmp(mkeys[j],k)) return mvals[j]; j=(j+1)&(mcap-1);} return 0; }
static void mset(char *k, long v){ if((mlen+1)*2>mcap) mgrow(); long j=fnv(k)&(mcap-1); while(mkeys[j]){ if(!strcmp(mkeys[j],k)){mvals[j]=v;return;} j=(j+1)&(mcap-1);} mkeys[j]=k; mvals[j]=v; mlen++; }

static void padd(Posting *p, long doc){
    if (p->n && p->docs[p->n-1]==doc) { p->freqs[p->n-1]++; return; }
    if (p->n >= p->cap) { p->cap = p->cap ? p->cap*2 : 4; p->docs=(long*)realloc(p->docs,p->cap*sizeof(long)); p->freqs=(long*)realloc(p->freqs,p->cap*sizeof(long)); }
    p->docs[p->n]=doc; p->freqs[p->n]=1; p->n++;
}
static void add(const char *term, long doc){
    long slot = mget(term);
    if (!slot) {
        if (npost >= postcap) { postcap = postcap ? postcap*2 : 16; post=(Posting*)realloc(post, postcap*sizeof(Posting)); }
        Posting *p=&post[npost]; p->term=strdup(term); p->docs=NULL; p->freqs=NULL; p->n=0; p->cap=0;
        padd(p, doc);
        mset(p->term, npost+1);
        npost++;
    } else {
        padd(&post[slot-1], doc);
    }
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
    long sum=0;
    for (long i=0;i<npost;i++) { sum += (long)strlen(post[i].term) + post[i].n; for (long k=0;k<post[i].n;k++) sum += post[i].freqs[k]; }
    long hits=0, s0=mget("t0"), s1=mget("t1");
    if (s0) { Posting *p=&post[s0-1];
        for (long i=0;i<p->n;i++) { long doc=p->docs[i]; int ok=1;
            if (!s1) ok=0; else { Posting *q=&post[s1-1]; int f=0; for (long k=0;k<q->n;k++) if (q->docs[k]==doc){f=1;break;} if(!f) ok=0; }
            if (ok) hits++; } }
    printf("vocab=%ld checksum=%ld and01=%ld\n", npost, sum, hits);
    return 0;
}
