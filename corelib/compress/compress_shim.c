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
/* int64-migration (Phase 3): Tycho `int` lowers to tycho_int (int64_t) in the
 * emitted program; this shim is a separate translation unit, so it defines the
 * same type to match the FFI ABI on ILP32/LLP64, not just LP64. */
#ifndef TYCHO_INT_T
#define TYCHO_INT_T
typedef int64_t tycho_int;
#endif

/* windowBits 15 | 16 selects zlib's gzip wrapper (header + CRC32 + length). */
#define GZIP_WBITS (15 + 16)

void zx_compress(const unsigned char *data, tycho_int len, unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    if (len < 0) return;
    z_stream s;
    memset(&s, 0, sizeof s);
    if (deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, GZIP_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return;
    /* deflateBound is an upper bound on the compressed size, so one shot suffices. */
    uLong cap = deflateBound(&s, (uLong)len) + 64;
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

/* ZD_* status codes, mirrored in compress.ty's decode of them. Every failure
 * branch below used to `return` with *outlen = 0 and nothing else, which made a
 * corrupt stream, a truncated one and a legitimately empty payload the SAME
 * answer -- data loss that looks like data (FRICTION.md #3). The branch already
 * knew which it was; it discarded that on the way out. */
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
#define ZD_FAILED    3   /* init or allocation failed -- nothing to do with the payload */

void zx_decompress(const unsigned char *data, tycho_int len, tycho_int *status,
                   unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    *status = ZD_FAILED;
    if (len < 0) return;
    z_stream s;
    memset(&s, 0, sizeof s);
    /* 15 | 32: auto-detect a gzip OR zlib wrapper on the input. */
    if (inflateInit2(&s, 15 + 32) != Z_OK) return;
    size_t cap = (len > 0 ? (size_t)len * 4u : 64u) + 64u;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) { inflateEnd(&s); return; }
    s.next_in   = (Bytef *)data;
    s.avail_in  = (uInt)len;
    s.next_out  = buf;
    s.avail_out = (uInt)cap;
    for (;;) {
        int rc = inflate(&s, Z_NO_FLUSH);
        if (rc == Z_STREAM_END) break;
        if (rc != Z_OK && rc != Z_BUF_ERROR) {          /* corrupt / needs dict / OOM */
            *status = ZD_CORRUPT;
            free(buf); inflateEnd(&s); return;
        }
        if (s.avail_out == 0) {                          /* output full -> grow and keep going */
            size_t used = cap;
            size_t ncap = cap * 2u;
            /* DECOMPRESSION BOMB. This loop doubled without a ceiling, so a small
             * archive that expands enormously was attempted until realloc failed or
             * the machine did -- measured 2026-08-14: a 199 KB gzip decompressed to
             * 200 MB with no complaint, and a real bomb reaches petabytes at the
             * same 1000:1. inflate cannot know the output size in advance, so the
             * ceiling has to live here. 1 GiB is far above anything corelib reads
             * (tycho-ar's archives are kilobytes) and far below hurting the host. */
            if (ncap > ZD_MAX_OUT) {
                *status = ZD_TOOBIG;
                free(buf); inflateEnd(&s); return;
            }
            unsigned char *nb = (unsigned char *)realloc(buf, ncap);
            if (!nb) { free(buf); inflateEnd(&s); return; }
            buf = nb;
            s.next_out  = buf + used;
            s.avail_out = (uInt)(ncap - used);
            cap = ncap;
        } else if (rc == Z_BUF_ERROR) {                  /* room left but no progress -> truncated input */
            *status = ZD_TRUNCATED;
            free(buf); inflateEnd(&s); return;
        }
        /* rc == Z_OK with room remaining: inflate made progress, call it again */
    }
    *outlen = (tycho_int)s.total_out;
    *out = buf;
    *status = ZD_OK;
    inflateEnd(&s);
}

/* Raw DEFLATE (RFC 1951) -- no gzip/zlib wrapper. windowBits -15. Used by
 * core:zip, whose entries are raw deflate streams. Same fail-closed shape. */
void zx_raw_deflate(const unsigned char *data, tycho_int len, unsigned char **out, tycho_int *outlen) {
    *out = NULL; *outlen = 0;
    if (len < 0) return;
    z_stream s; memset(&s, 0, sizeof s);
    if (deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return;
    uLong cap = deflateBound(&s, (uLong)len) + 64;
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
    if (len < 0) return;
    z_stream s; memset(&s, 0, sizeof s);
    if (inflateInit2(&s, -15) != Z_OK) return;
    size_t cap = (len > 0 ? (size_t)len * 4u : 64u) + 64u;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) { inflateEnd(&s); return; }
    s.next_in = (Bytef *)data; s.avail_in = (uInt)len;
    s.next_out = buf; s.avail_out = (uInt)cap;
    for (;;) {
        int rc = inflate(&s, Z_NO_FLUSH);
        if (rc == Z_STREAM_END) break;
        if (rc != Z_OK) {   /* one branch before; Z_BUF_ERROR is the truncated case */
            *status = (rc == Z_BUF_ERROR) ? ZD_TRUNCATED : ZD_CORRUPT;
            free(buf); inflateEnd(&s); return;
        }
        if (s.avail_out == 0) {
            size_t ncap = cap * 2u; unsigned char *nb = (unsigned char *)realloc(buf, ncap);
            if (!nb) { free(buf); inflateEnd(&s); return; }
            buf = nb; s.next_out = buf + s.total_out; s.avail_out = (uInt)(ncap - s.total_out); cap = ncap;
        }
    }
    *outlen = (tycho_int)s.total_out; *out = buf; *status = ZD_OK; inflateEnd(&s);
}
