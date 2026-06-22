/* Inverted-index build, count-then-fill (exact preallocation) — C. The fair
 * architecture-matched baseline for invindex_exact.ty: pass 1 counts each term's
 * occurrences, each posting list is malloc'd ONCE at its exact size, pass 2 fills.
 * Same LCG / checksum as the others. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *term; long *docs; long *freqs; long n, cap; } Posting;
static Posting *post; static long npost, postcap;
static char **mkeys; static long *mvals; static long mcap, mlen;
static unsigned long fnv(const char *s){ unsigned long h=1469598103934665603UL; for(;*s;s++){ h^=(unsigned char)*s; h*=1099511628211UL; } return h; }
static void mgrow(void){
    long nc = mcap ? mcap*2 : 16;
    char **nk=(char**)calloc(nc,sizeof(char*)); long *nv=(long*)calloc(nc,sizeof(long));
    for (long i=0;i<mcap;i++) if (mkeys[i]){ long j=fnv(mkeys[i])&(nc-1); while(nk[j]) j=(j+1)&(nc-1); nk[j]=mkeys[i]; nv[j]=mvals[i]; }
    free(mkeys); free(mvals); mkeys=nk; mvals=nv; mcap=nc;
}
static long mget(const char *k){ if(!mcap) return 0; long j=fnv(k)&(mcap-1); while(mkeys[j]){ if(!strcmp(mkeys[j],k)) return mvals[j]; j=(j+1)&(mcap-1);} return 0; }
static void mset(char *k, long v){ if((mlen+1)*2>mcap) mgrow(); long j=fnv(k)&(mcap-1); while(mkeys[j]){ if(!strcmp(mkeys[j],k)){mvals[j]=v;return;} j=(j+1)&(mcap-1);} mkeys[j]=k; mvals[j]=v; mlen++; }

int main(void){
    long n=300000, w=12, v=8000;
    long seed=1; char buf[32];

    /* pass 1: vocabulary + exact occurrence count per term (slot+1 in map) */
    for (long d=0; d<n; d++)
        for (long j=0; j<w; j++) {
            seed=(seed*1103515245+12345)%2147483647;
            snprintf(buf,sizeof buf,"t%ld",seed%v);
            long slot=mget(buf);
            if (!slot) {
                if (npost>=postcap){ postcap=postcap?postcap*2:16; post=(Posting*)realloc(post,postcap*sizeof(Posting)); }
                Posting *p=&post[npost]; p->term=strdup(buf); p->docs=NULL; p->freqs=NULL; p->n=0; p->cap=1;
                npost++; mset(p->term, npost);   /* slot = npost (1-based) */
            } else post[slot-1].cap++;
        }
    /* preallocate each list to its exact upper bound, reset n for the fill pass */
    for (long i=0;i<npost;i++){ post[i].docs=(long*)malloc(post[i].cap*sizeof(long)); post[i].freqs=(long*)malloc(post[i].cap*sizeof(long)); post[i].n=0; }

    /* pass 2: fill (regenerate; no growth) */
    seed=1;
    for (long d=0; d<n; d++)
        for (long j=0; j<w; j++) {
            seed=(seed*1103515245+12345)%2147483647;
            snprintf(buf,sizeof buf,"t%ld",seed%v);
            Posting *p=&post[mget(buf)-1];
            if (p->n && p->docs[p->n-1]==d) p->freqs[p->n-1]++;
            else { p->docs[p->n]=d; p->freqs[p->n]=1; p->n++; }
        }

    long sum=0;
    for (long i=0;i<npost;i++){ sum+=(long)strlen(post[i].term)+post[i].n; for(long k=0;k<post[i].n;k++) sum+=post[i].freqs[k]; }
    long hits=0, s0=mget("t0"), s1=mget("t1");
    if (s0){ Posting *p=&post[s0-1];
        for(long i=0;i<p->n;i++){ long doc=p->docs[i]; int ok=1;
            if(!s1) ok=0; else { Posting *q=&post[s1-1]; int f=0; for(long k=0;k<q->n;k++) if(q->docs[k]==doc){f=1;break;} if(!f) ok=0; }
            if(ok) hits++; } }
    printf("vocab=%ld checksum=%ld and01=%ld\n", npost, sum, hits);
    return 0;
}
