/* core:xz shim -- xz container compress/decompress over liblzma.
 * The deps manifest names `liblzma`; pkg-config supplies the header and -llzma.
 * Both directions cross the FFI as bytes -> bytes plus a status out-param.
 */
#include <lzma.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "../tycho.h"

#define XZ_OK        0
#define XZ_CORRUPT   1
#define XZ_TRUNCATED 2
#define XZ_TOOBIG    3
#define XZ_FAILED    4

#ifndef XZ_MAX_OUT
#define XZ_MAX_OUT ((size_t)1024 * 1024 * 1024)
#endif

void xzx_compress(const unsigned char *data, tycho_int len,
                  tycho_int *status, unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    *status = XZ_FAILED;
    if (len < 0) return;
    size_t n = (size_t)len;
    size_t cap = lzma_stream_buffer_bound(n);
    if (cap == 0) cap = 1;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) return;
    size_t out_pos = 0;
    lzma_ret r = lzma_easy_buffer_encode(6, LZMA_CHECK_CRC64, NULL,
                                        data, n, buf, &out_pos, cap);
    if (r != LZMA_OK) {
        free(buf);
        *status = XZ_FAILED;
        return;
    }
    *out = buf;
    *outlen = (tycho_int)out_pos;
    *status = XZ_OK;
}

/* Classification is ours, not liblzma's: lzma_stream_buffer_decode reports a
 * truncated stream as LZMA_BUF_ERROR on some liblzma releases and as
 * LZMA_DATA_ERROR on others (5.8 does the latter), so the same input came back
 * Truncated on one host and Corrupt on another. The streaming decoder under
 * LZMA_FINISH is stable: input that ends early is LZMA_BUF_ERROR with all input
 * consumed. Input too short for the 12-byte header can only be told apart by
 * its magic prefix -- a mismatch is Corrupt, a match is Truncated. */
void xzx_decompress(const unsigned char *data, tycho_int len,
                    tycho_int *status, unsigned char **out, tycho_int *outlen) {
    static const unsigned char magic[6] = {0xFD, '7', 'z', 'X', 'Z', 0x00};
    *out = NULL;
    *outlen = 0;
    *status = XZ_FAILED;
    if (len < 0) return;
    size_t in_len = (size_t)len;
    if (in_len && memcmp(data, magic, in_len < 6 ? in_len : 6) != 0) {
        *status = XZ_CORRUPT;
        return;
    }
    size_t limit = XZ_MAX_OUT;
    size_t cap = in_len && in_len <= ((size_t)-1 - 64) / 4 ? in_len * 4 + 64 : limit;
    if (cap < 1024) cap = 1024;
    if (cap > limit) cap = limit;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) return;
    lzma_stream s = LZMA_STREAM_INIT;
    if (lzma_stream_decoder(&s, UINT64_MAX, 0) != LZMA_OK) { free(buf); return; }
    s.next_in = data;
    s.avail_in = in_len;
    s.next_out = buf;
    s.avail_out = cap;
    tycho_int st = XZ_FAILED;
    for (;;) {
        lzma_ret r = lzma_code(&s, LZMA_FINISH);
        if (r == LZMA_STREAM_END) { st = XZ_OK; break; }
        if ((r == LZMA_OK || r == LZMA_BUF_ERROR) && s.avail_out == 0) {
            if (cap >= limit) { st = XZ_TOOBIG; break; }
            size_t next = cap > limit / 2 ? limit : cap * 2;
            unsigned char *nb = (unsigned char *)realloc(buf, next);
            if (!nb) { st = XZ_FAILED; break; }
            buf = nb;
            s.next_out = buf + cap;
            s.avail_out = next - cap;
            cap = next;
            continue;
        }
        if (r == LZMA_OK) continue;
        if (r == LZMA_BUF_ERROR) st = XZ_TRUNCATED;
        else if (r == LZMA_FORMAT_ERROR || r == LZMA_DATA_ERROR || r == LZMA_OPTIONS_ERROR) st = XZ_CORRUPT;
        else st = XZ_FAILED;
        break;
    }
    size_t produced = cap - s.avail_out;
    lzma_end(&s);
    if (st != XZ_OK) { free(buf); *status = st; return; }
    *out = buf;
    *outlen = (tycho_int)produced;
    *status = XZ_OK;
}
