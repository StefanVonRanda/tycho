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

void xzx_decompress(const unsigned char *data, tycho_int len,
                    tycho_int *status, unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    *status = XZ_FAILED;
    if (len < 0) return;
    size_t in_len = (size_t)len;
    size_t limit = XZ_MAX_OUT;
    size_t cap = in_len && in_len <= ((size_t)-1 - 64) / 4 ? in_len * 4 + 64 : limit;
    if (cap < 1024) cap = 1024;
    if (cap > limit) cap = limit;
    for (;;) {
        unsigned char *buf = (unsigned char *)malloc(cap);
        if (!buf) return;
        uint64_t memlimit = UINT64_MAX;
        size_t in_pos = 0;
        size_t out_pos = 0;
        lzma_ret r = lzma_stream_buffer_decode(&memlimit, 0, NULL,
                                               data, &in_pos, in_len,
                                               buf, &out_pos, cap);
        if (r == LZMA_OK) {
            *out = buf;
            *outlen = (tycho_int)out_pos;
            *status = XZ_OK;
            return;
        }
        free(buf);
        if (r == LZMA_FORMAT_ERROR || r == LZMA_DATA_ERROR || r == LZMA_OPTIONS_ERROR) {
            *status = XZ_CORRUPT;
            return;
        }
        if (r == LZMA_MEM_ERROR || r == LZMA_MEMLIMIT_ERROR) {
            *status = XZ_FAILED;
            return;
        }
        if (r == LZMA_BUF_ERROR) {
            if (out_pos >= cap) {
                if (cap >= limit) {
                    *status = XZ_TOOBIG;
                    return;
                }
                size_t next = cap > limit / 2 ? limit : cap * 2;
                if (next <= cap) {
                    *status = XZ_TOOBIG;
                    return;
                }
                cap = next;
                continue;
            }
            *status = XZ_TRUNCATED;
            return;
        }
        *status = XZ_FAILED;
        return;
    }
}
