#include <stdio.h>
#include <stdlib.h>
int main(void) {
    long n = 100000, m = 200, k = 1000003;
    long *xs = malloc(sizeof(long) * n);
    for (long i = 0; i < n; i++) xs[i] = i;
    long checksum = 0;
    for (long j = 0; j < m; j++) {
        long *ys = malloc(sizeof(long) * n);
        for (long i = 0; i < n; i++) { long t = xs[i] * 1103515245L + 12345 + j; ys[i] = t % k; }
        long s = 0;
        for (long i = 0; i < n; i++) s += ys[i];
        checksum = (checksum + s) % k;
        free(ys);
    }
    printf("%ld\n", checksum);
    return 0;
}
