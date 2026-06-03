#include <stdio.h>
#include <stdlib.h>
/* iterated-transform: a long-lived value reassigned each step. Only the latest
 * array is live; manual free reclaims each old one -> flat memory. */
static long *step(const long *a, long n, long p) {
    long *b = malloc((size_t)n * sizeof(long));
    for (long i = 0; i < n; i++) { long t = a[i] * 1103515245L + 12345L; b[i] = t - (t / p) * p; }
    return b;
}
int main(void) {
    long n = 100000, m = 2000, p = 2147483647L;
    long *a = malloc((size_t)n * sizeof(long));
    for (long i = 0; i < n; i++) a[i] = i;
    for (long j = 0; j < m; j++) { long *b = step(a, n, p); free(a); a = b; }
    long s = 0;
    for (long i = 0; i < n; i++) s = s + a[i];
    free(a);
    printf("%ld\n", s - (s / 1000003L) * 1000003L);
    return 0;
}
