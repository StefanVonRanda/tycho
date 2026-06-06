/* Sliding-window, C: a ring of strdup'd strings; the evicted slot is freed, so
 * peak RSS tracks the WINDOW (W), not the stream (N). Same checksum as the hier port. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    long n = 2000000, w = 50000, acc = 0;
    char **ring = calloc((size_t)w, sizeof(char *));
    for (long i = 0; i < n; i++) {
        char buf[32];
        snprintf(buf, sizeof buf, "rec%ld", i % 100000);
        long s = i % w;
        free(ring[s]);
        ring[s] = strdup(buf);
        acc += (long)strlen(ring[s]);
    }
    printf("%ld\n", acc);
    return 0;
}
