/* GC-scan-cost, C: 2M strings held in an array (no GC, never scanned) + churn. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    long m = 2000000;
    char **live = malloc((size_t)m * sizeof(char *));
    for (long i = 0; i < m; i++) { char b[16]; snprintf(b, sizeof b, "x%ld", i % 1000); live[i] = strdup(b); }
    long acc = 0;
    for (long r = 0; r < 300; r++) {
        long *xs = malloc((size_t)50000 * sizeof(long));
        for (long j = 0; j < 50000; j++) xs[j] = (r + j) % 997;
        long s = 0; for (long t = 0; t < 50000; t++) s += xs[t];
        acc = (acc + s) % 1000000007L;
        free(xs);
    }
    long lacc = 0; for (long i = 0; i < m; i++) lacc += (long)strlen(live[i]);
    printf("%ld %ld\n", acc, lacc);
    return 0;
}
