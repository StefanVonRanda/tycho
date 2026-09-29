/* core:compress shim -- gzip (RFC 1952) compress/decompress over zlib. The deps
 * manifest names `zlib`; pkg-config supplies the headers + -lz, so this is turnkey
 * where zlib is installed and its test is skipped where it is not.
 *
 * Both cross the FFI as `bytes -> bytes` via the out-param convention
 * (unsigned char **out, int64_t *outlen): the shim mallocs *out and tycho_bytes_from_c
 * copies it into the caller's arena and frees it. Fail closed -- any zlib error,
 * truncated input, or allocation failure yields *out=NULL / *outlen=0 (empty bytes
 * to tycho), never a partial or uninitialized buffer. Length-carried, so binary
 * data with interior NUL bytes round-trips intact.
 */
#include <zlib.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "../tycho.h"

/* windowBits 15 | 16 selects zlib's gzip wrapper (header + CRC32 + length). */
#define GZIP_WBITS (15 + 16)

void zx_compress(const unsigned char *data, tycho_int len, unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    if (len < 0 || (uint64_t)len > UINT_MAX) return;
    z_stream s;
    memset(&s, 0, sizeof s);
    if (deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, GZIP_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return;
    /* One-shot output must fit zlib's uInt avail_out as well as size_t. */
    uLong bound = deflateBound(&s, (uLong)len);
    if (bound > (uLong)UINT_MAX - 64u) { deflateEnd(&s); return; }
    uLong cap = bound + 64u;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) { deflateEnd(&s); return; }
    s.next_in   = (Bytef *)data;
    s.avail_in  = (uInt)len;
    s.next_out  = buf;
    s.avail_out = (uInt)cap;
    int rc = deflate(&s, Z_FINISH);
    if (rc != Z_STREAM_END) { free(buf); deflateEnd(&s); return; }   /* fail closed */
    *outlen = (tycho_int)s.total_out;
    *out = buf;
    deflateEnd(&s);
}

#define ZD_OK        0
#define ZD_CORRUPT   1   /* inflate refused the data: bad wrapper, bad checksum, needs a dict */
#define ZD_TOOBIG    3   /* the output passed ZD_MAX_OUT -- a decompression bomb */
/* 1 GiB ceiling on one decompress. Overridable at COMPILE time so a gate can
 * prove the ceiling fires without allocating a gigabyte: the lane rebuilds this
 * shim with -DZD_MAX_OUT=65536 and feeds it an input that expands past that. A
 * ceiling nothing ever crosses is a ceiling nobody has tested. */
#ifndef ZD_MAX_OUT
#define ZD_MAX_OUT   ((size_t)1024 * 1024 * 1024)
#endif
#define ZD_TRUNCATED 2   /* room left in the output and no progress: the input stops mid-stream */
#define ZD_FAILED    4   /* init, allocation, or unsupported input size */

static size_t zd_limit(void) {
    return ZD_MAX_OUT < (size_t)UINT_MAX ? ZD_MAX_OUT : (size_t)UINT_MAX;
}

static size_t zd_initial_cap(tycho_int len) {
    size_t limit = zd_limit();
    if (!limit) return 0;
    size_t n = (size_t)len;
    size_t cap = n && n <= ((size_t)-1 - 64) / 4 ? n * 4 + 64 : limit;
    if (cap > (size_t)1024 * 1024) cap = (size_t)1024 * 1024;
    if (cap > limit) cap = limit;
    return cap ? cap : 1;
}

static int zd_grow_cap(size_t cap, size_t limit, size_t *next) {
    if (cap >= limit) return 0;
    size_t n = cap > limit / 2 ? limit : cap * 2;
    if (n <= cap) return 0;
    *next = n;
    return 1;
}

void zx_decompress(const unsigned char *data, tycho_int len, tycho_int *status,
                   unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    *status = ZD_FAILED;
    if (len < 0 || (uint64_t)len > UINT_MAX) return;
    z_stream s;
    memset(&s, 0, sizeof s);
    /* 15 | 32: auto-detect a gzip OR zlib wrapper on the input. */
    if (inflateInit2(&s, 15 + 32) != Z_OK) return;
    size_t limit = zd_limit();
    size_t cap = zd_initial_cap(len);
    if (!cap) { inflateEnd(&s); return; }
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) { inflateEnd(&s); return; }
    s.next_in   = (Bytef *)data;
    s.avail_in  = (uInt)len;
    s.next_out  = buf;
    s.avail_out = (uInt)cap;
    size_t used = 0;
    int gzip_input = len >= 2 && data[0] == 0x1f && data[1] == 0x8b;
    unsigned char extra_byte = 0;
    int overflow_probe = 0;
    for (;;) {
        uInt before = s.avail_out;
        int rc = inflate(&s, Z_NO_FLUSH);
        size_t produced = (size_t)(before - s.avail_out);
        if (overflow_probe) {
            if (produced) { *status = ZD_TOOBIG; free(buf); inflateEnd(&s); return; }
        } else {
            used += produced;
        }
        if (rc == Z_STREAM_END) {
            if (s.avail_in == 0) break;
            if (!gzip_input || s.avail_in < 2 || s.next_in[0] != 0x1f || s.next_in[1] != 0x8b) {
                *status = ZD_CORRUPT; free(buf); inflateEnd(&s); return;
            }
            Bytef *next_in = s.next_in;
            uInt remaining = s.avail_in;
            if (inflateReset2(&s, 15 + 32) != Z_OK) {
                *status = ZD_FAILED; free(buf); inflateEnd(&s); return;
            }
            s.next_in = next_in;
            s.avail_in = remaining;
            if (used < cap) {
                overflow_probe = 0;
                s.next_out = buf + used;
                s.avail_out = (uInt)(cap - used);
            } else if (used < limit) {
                size_t ncap;
                if (!zd_grow_cap(cap, limit, &ncap)) { *status = ZD_TOOBIG; free(buf); inflateEnd(&s); return; }
                unsigned char *nb = (unsigned char *)realloc(buf, ncap);
                if (!nb) { free(buf); inflateEnd(&s); return; }
                buf = nb; cap = ncap; overflow_probe = 0;
                s.next_out = buf + used; s.avail_out = (uInt)(cap - used);
            } else {
                overflow_probe = 1;
                s.next_out = &extra_byte;
                s.avail_out = 1;
            }
            continue;
        }
        if (rc != Z_OK && rc != Z_BUF_ERROR) {
            *status = ZD_CORRUPT;
            free(buf); inflateEnd(&s); return;
        }
        if (s.avail_out == 0) {
            size_t ncap;
            if (overflow_probe) { *status = ZD_TOOBIG; free(buf); inflateEnd(&s); return; }
            if (!zd_grow_cap(cap, limit, &ncap)) {
                overflow_probe = 1;
                s.next_out = &extra_byte;
                s.avail_out = 1;
                continue;
            }
            unsigned char *nb = (unsigned char *)realloc(buf, ncap);
            if (!nb) { free(buf); inflateEnd(&s); return; }
            buf = nb;
            cap = ncap;
            s.next_out = buf + used;
            s.avail_out = (uInt)(cap - used);
        } else if (rc == Z_BUF_ERROR) {
            *status = ZD_TRUNCATED;
            free(buf); inflateEnd(&s); return;
        }
    }
    *outlen = (tycho_int)used;
    *out = buf;
    *status = ZD_OK;
    inflateEnd(&s);
}

/* Raw DEFLATE (RFC 1951) -- no gzip/zlib wrapper. windowBits -15. Used by
 * core:zip, whose entries are raw deflate streams. Same fail-closed shape. */
void zx_raw_deflate(const unsigned char *data, tycho_int len, unsigned char **out, tycho_int *outlen) {
    *out = NULL; *outlen = 0;
    if (len < 0 || (uint64_t)len > UINT_MAX) return;
    z_stream s; memset(&s, 0, sizeof s);
    if (deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return;
    uLong bound = deflateBound(&s, (uLong)len);
    if (bound > (uLong)UINT_MAX - 64u) { deflateEnd(&s); return; }
    uLong cap = bound + 64u;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) { deflateEnd(&s); return; }
    s.next_in = (Bytef *)data; s.avail_in = (uInt)len;
    s.next_out = buf; s.avail_out = (uInt)cap;
    if (deflate(&s, Z_FINISH) != Z_STREAM_END) { free(buf); deflateEnd(&s); return; }
    *outlen = (tycho_int)s.total_out; *out = buf; deflateEnd(&s);
}

void zx_raw_inflate(const unsigned char *data, tycho_int len, tycho_int *status,
                    unsigned char **out, tycho_int *outlen) {
    *out = NULL; *outlen = 0; *status = ZD_FAILED;
    if (len < 0 || (uint64_t)len > UINT_MAX) return;
    z_stream s; memset(&s, 0, sizeof s);
    if (inflateInit2(&s, -15) != Z_OK) return;
    size_t cap = zd_initial_cap(len);
    if (!cap) { inflateEnd(&s); return; }
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) { inflateEnd(&s); return; }
    s.next_in = (Bytef *)data; s.avail_in = (uInt)len;
    s.next_out = buf; s.avail_out = (uInt)cap;
    unsigned char extra_byte = 0;
    int overflow_probe = 0;
    for (;;) {
        uInt before = s.avail_out;
        int rc = inflate(&s, Z_NO_FLUSH);
        if (overflow_probe && before != s.avail_out) { *status = ZD_TOOBIG; free(buf); inflateEnd(&s); return; }
        if (rc == Z_STREAM_END) break;
        if (rc != Z_OK) {   /* one branch before; Z_BUF_ERROR is the truncated case */
            *status = (rc == Z_BUF_ERROR) ? ZD_TRUNCATED : ZD_CORRUPT;
            free(buf); inflateEnd(&s); return;
        }
        if (s.avail_out == 0) {
            /* Same ZD_MAX_OUT ceiling the gzip path enforces: core:zip feeds raw
             * deflate streams straight in and only compares the declared size
             * AFTER the inflate, so without this a 1.1 MB zip reaches 1.1 GB. */
            size_t ncap;
            if (overflow_probe) { *status = ZD_TOOBIG; free(buf); inflateEnd(&s); return; }
            if (!zd_grow_cap(cap, zd_limit(), &ncap)) {
                overflow_probe = 1; s.next_out = &extra_byte; s.avail_out = 1; continue;
            }
            unsigned char *nb = (unsigned char *)realloc(buf, ncap);
            if (!nb) { free(buf); inflateEnd(&s); return; }
            buf = nb; s.next_out = buf + s.total_out; s.avail_out = (uInt)(ncap - s.total_out); cap = ncap;
        }
    }
    *outlen = (tycho_int)s.total_out; *out = buf; *status = ZD_OK; inflateEnd(&s);
}
