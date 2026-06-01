#include <stdio.h>
#include <stdlib.h>
int main(void) {
    long m = 4000, kk = 256, p = 1000003;
    long checksum = 0;
    for (long j = 0; j < m; j++) {
        long cap = 16, len = 0;
        char *s = malloc(cap);
        for (long i = 0; i < kk; i++) {
            if (len + 2 > cap) { cap *= 2; s = realloc(s, cap); }
            s[len++] = (char)('0' + (i + j) % 10);
        }
        long h = 0;
        for (long i = 0; i < len; i++) h += (unsigned char)s[i];
        checksum = (checksum + h) % p;
        free(s);
    }
    printf("%ld\n", checksum);
    return 0;
}
