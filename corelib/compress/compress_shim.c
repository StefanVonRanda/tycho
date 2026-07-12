/* core:compress shim -- gzip (RFC 1952) compress/decompress over zlib. The deps
 * manifest names `zlib`; pkg-config supplies the headers + -lz, so this is turnkey
 * where zlib is installed and its test is skipped where it is not.
 *
 * Both cross the FFI as `bytes -> bytes` via the out-param convention
 * (unsigned char **out, long *outlen): the shim mallocs *out and tycho_bytes_from_c
 * copies it into the caller's arena and frees it. Fail closed -- any zlib error,
 * truncated input, or allocation failure yields *out=NULL / *outlen=0 (empty bytes
 * to tycho), never a partial or uninitialized buffer. Length-carried, so binary
 * data with interior NUL bytes round-trips intact.
 */
#include <zlib.h>
#include <stdlib.h>
#include <string.h>

/* windowBits 15 | 16 selects zlib's gzip wrapper (header + CRC32 + length). */
#define GZIP_WBITS (15 + 16)

void zx_compress(const unsigned char *data, long len, unsigned char **out, long *outlen) {
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
    *outlen = (long)s.total_out;
    *out = buf;
    deflateEnd(&s);
}

void zx_decompress(const unsigned char *data, long len, unsigned char **out, long *outlen) {
    *out = NULL;
    *outlen = 0;
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
        if (rc != Z_OK && rc != Z_BUF_ERROR) {          /* corrupt / needs dict / OOM -> fail closed */
            free(buf); inflateEnd(&s); return;
        }
        if (s.avail_out == 0) {                          /* output full -> grow and keep going */
            size_t used = cap;
            size_t ncap = cap * 2u;
            unsigned char *nb = (unsigned char *)realloc(buf, ncap);
            if (!nb) { free(buf); inflateEnd(&s); return; }
            buf = nb;
            s.next_out  = buf + used;
            s.avail_out = (uInt)(ncap - used);
            cap = ncap;
        } else if (rc == Z_BUF_ERROR) {                  /* room left but no progress -> truncated input */
            free(buf); inflateEnd(&s); return;
        }
        /* rc == Z_OK with room remaining: inflate made progress, call it again */
    }
    *outlen = (long)s.total_out;
    *out = buf;
    inflateEnd(&s);
}
