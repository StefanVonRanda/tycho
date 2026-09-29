#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "../corelib/tycho.h"

void zx_compress(const unsigned char *, tycho_int, unsigned char **, tycho_int *);
void zx_decompress(const unsigned char *, tycho_int, tycho_int *, unsigned char **, tycho_int *);
void zx_raw_deflate(const unsigned char *, tycho_int, unsigned char **, tycho_int *);
void zx_raw_inflate(const unsigned char *, tycho_int, tycho_int *, unsigned char **, tycho_int *);

#define TEST_LIMIT 65536
#define ZD_OK 0
#define ZD_TOOBIG 3
#define ZD_FAILED 4

static unsigned char *gzip_data(size_t n, size_t *outlen) {
    z_stream s = {0};
    if (deflateInit2(&s, Z_BEST_SPEED, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return NULL;
    uLong cap = deflateBound(&s, (uLong)n) + 32;
    unsigned char *input = malloc(n ? n : 1);
    unsigned char *output = malloc((size_t)cap);
    if (!input || !output) { free(input); free(output); deflateEnd(&s); return NULL; }
    memset(input, 'x', n);
    s.next_in = input; s.avail_in = (uInt)n;
    s.next_out = output; s.avail_out = (uInt)cap;
    int rc = deflate(&s, Z_FINISH);
    if (rc != Z_STREAM_END) { free(input); free(output); deflateEnd(&s); return NULL; }
    *outlen = (size_t)s.total_out;
    free(input);
    deflateEnd(&s);
    return output;
}

static int decode_size(size_t n, tycho_int expected_status, tycho_int expected_len) {
    size_t compressed_len = 0;
    unsigned char *compressed = gzip_data(n, &compressed_len);
    if (!compressed) return 0;
    tycho_int status = -1, outlen = -1;
    unsigned char *out = NULL;
    zx_decompress(compressed, (tycho_int)compressed_len, &status, &out, &outlen);
    int ok = status == expected_status && outlen == expected_len;
    if (expected_status == ZD_OK) ok = ok && out && out[0] == 'x' && out[outlen - 1] == 'x';
    else ok = ok && out == NULL;
    free(out);
    free(compressed);
    return ok;
}

int main(void) {
    if (!decode_size(TEST_LIMIT, ZD_OK, TEST_LIMIT) ||
        !decode_size(TEST_LIMIT + 1, ZD_TOOBIG, 0)) {
        fputs("compress-limits: output ceiling failed\n", stderr);
        return 1;
    }

    unsigned char byte = 0;
    tycho_int too_long = (tycho_int)((uint64_t)UINT_MAX + 1);
    unsigned char *out = NULL;
    tycho_int outlen = -1, status = -1;
    zx_compress(&byte, too_long, &out, &outlen);
    if (out || outlen != 0) return 2;
    zx_decompress(&byte, too_long, &status, &out, &outlen);
    if (out || outlen != 0 || status != ZD_FAILED) return 3;
    zx_raw_deflate(&byte, too_long, &out, &outlen);
    if (out || outlen != 0) return 4;
    zx_raw_inflate(&byte, too_long, &status, &out, &outlen);
    if (out || outlen != 0 || status != ZD_FAILED) return 5;

    puts("compress-limits: green (inclusive output cap; overflow rejected; oversized zlib inputs refused)");
    return 0;
}
