/* Latency angle, C: malloc/free the working set each round -> no GC, predictable.
 * Same checksum as the hier port. */
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    long iters = 2000, k = 100000, acc = 0;
    for (long it = 0; it < iters; it++) {
        long *xs = malloc((size_t)k * sizeof(long));
        for (long j = 0; j < k; j++) xs[j] = (it + j) % 997;
        long s = 0;
        for (long m = 0; m < k; m++) s += xs[m];
        acc = (acc + s) % 1000000007L;
        free(xs);
    }
    printf("%ld\n", acc);
    return 0;
}
