/* Fixed-size sliding window, C: no heap record per element. */
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    long n = 2000000, w = 50000, acc = 0;
    long *ring = calloc((size_t)w, sizeof(long));
    for (long i = 0; i < n; i++) {
        long s = i % w;
        ring[s] = i % 100000;
        acc += ring[s];
    }
    printf("%ld\n", acc);
    return 0;
}
