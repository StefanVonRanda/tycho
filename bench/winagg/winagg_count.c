/* Windowed group-by, expert C: count first, allocate exact lists, replay. */
#include <stdio.h>
#include <stdlib.h>
typedef struct { long key; long *docs; long n, cap; int used; } Slot;
int main(void) {
    long nw=40, w=250000, g=8000;
    long seed=1, checksum=0;
    for (long win=0; win<nw; win++) {
        long cap=16384; while (cap < g*2) cap*=2;
        Slot *tab = (Slot*)calloc(cap, sizeof(Slot));
        unsigned long mask = (unsigned long)cap-1;
        long seed0=seed;
        for (long e=0;e<w;e++) {
            seed=(seed*1103515245+12345)%2147483647;
            long gid=seed%g;
            unsigned long j=((unsigned long)gid*2654435761UL)&mask;
            while (tab[j].used && tab[j].key != gid) j=(j+1)&mask;
            if (!tab[j].used) { tab[j].used=1; tab[j].key=gid; }
            tab[j].n++;
        }
        for (long i=0;i<cap;i++) if (tab[i].used) {
            tab[i].docs=(long*)malloc((size_t)tab[i].n*sizeof(long));
            tab[i].cap=tab[i].n; tab[i].n=0;
        }
        seed=seed0;
        for (long e=0;e<w;e++) {
            seed=(seed*1103515245+12345)%2147483647;
            long gid=seed%g;
            long v=(seed/g)%100;
            unsigned long j=((unsigned long)gid*2654435761UL)&mask;
            while (tab[j].used && tab[j].key != gid) j=(j+1)&mask;
            tab[j].docs[tab[j].n++]=v;
        }
        for (long i=0;i<cap;i++) if (tab[i].used) {
            checksum += tab[i].n;
            for (long k=0;k<tab[i].n;k++) checksum += tab[i].docs[k];
            free(tab[i].docs);
        }
        free(tab);
    }
    printf("windows=%ld checksum=%ld\n", nw, checksum);
    return 0;
}
