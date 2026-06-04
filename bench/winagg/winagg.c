/* Windowed group-by aggregation — C, manual malloc/free. Per window: build an
 * int->posting-list hash map (the value is the growing list, realloc in place), reduce
 * it, then FREE the whole window (every list + the table) before the next. The explicit
 * per-window teardown is the churn cost the arena pays as one bulk reset. Same LCG and
 * checksum as winagg.hi (oracle: windows=40 checksum=505098931). */
#include <stdio.h>
#include <stdlib.h>

typedef struct { long key; long *docs; long n, cap; int used; } Slot;

int main(void){
    long nw=40, w=250000, g=8000;
    long seed=1, checksum=0;
    for (long win=0; win<nw; win++){
        /* fresh per-window open-addressing map: key -> growing list. */
        long cap=16384; while (cap < g*2) cap*=2;
        Slot *tab = (Slot*)calloc(cap, sizeof(Slot));
        unsigned long mask = (unsigned long)cap-1;
        for (long e=0; e<w; e++){
            seed = (seed*1103515245 + 12345) % 2147483647;
            long gid = seed % g;
            long v = (seed / g) % 100;
            unsigned long j = ((unsigned long)gid*2654435761UL) & mask;
            while (tab[j].used && tab[j].key != gid) j=(j+1)&mask;
            if (!tab[j].used) { tab[j].used=1; tab[j].key=gid; tab[j].docs=NULL; tab[j].n=0; tab[j].cap=0; }
            Slot *s=&tab[j];
            if (s->n >= s->cap) { s->cap = s->cap ? s->cap*2 : 4; s->docs=(long*)realloc(s->docs, s->cap*sizeof(long)); }
            s->docs[s->n++] = v;
        }
        /* reduce */
        for (long i=0;i<cap;i++) if (tab[i].used) {
            checksum += tab[i].n;
            for (long k=0;k<tab[i].n;k++) checksum += tab[i].docs[k];
        }
        /* discard the whole window (the churn teardown) */
        for (long i=0;i<cap;i++) if (tab[i].used) free(tab[i].docs);
        free(tab);
    }
    printf("windows=%ld checksum=%ld\n", nw, checksum);
    return 0;
}
