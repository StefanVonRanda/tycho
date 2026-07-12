/* core:image shim -- PNG decode/encode via libpng's simplified png_image API
 * (libpng >= 1.6). The deps manifest names `libpng`; pkg-config supplies the
 * headers + -lpng, so it is turnkey where libpng is installed and its test is
 * skipped where it is not.
 *
 * decode: PNG bytes -> an opaque handle holding RGBA pixels + dimensions (the
 * os.run handle pattern), read out via imgx_width / imgx_height / imgx_pixels and
 * released with imgx_free. encode: RGBA pixels + w + h -> PNG bytes (the bytes
 * out-param convention: the shim mallocs *out, tycho_bytes_from_c copies it into
 * the arena and frees it). Fail closed on any libpng error or bad input -- a null
 * handle from decode, empty bytes from encode/pixels.
 */
#include <png.h>
#include <stdlib.h>
#include <string.h>

typedef struct { long w, h; unsigned char *rgba; size_t nbytes; } Img;

void *imgx_decode(const unsigned char *data, long len) {
    if (len <= 0) return NULL;
    png_image image;
    memset(&image, 0, sizeof image);
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, data, (size_t)len)) return NULL;
    image.format = PNG_FORMAT_RGBA;                 /* always hand back 8-bit RGBA */
    size_t nbytes = PNG_IMAGE_SIZE(image);
    unsigned char *buf = (unsigned char *)malloc(nbytes ? nbytes : 1);
    if (!buf) { png_image_free(&image); return NULL; }
    if (!png_image_finish_read(&image, NULL, buf, 0, NULL)) {
        free(buf); png_image_free(&image); return NULL;
    }
    Img *im = (Img *)malloc(sizeof *im);
    if (!im) { free(buf); return NULL; }
    im->w = (long)image.width;
    im->h = (long)image.height;
    im->rgba = buf;
    im->nbytes = nbytes;
    return im;                                       /* png_image_free not needed after finish_read */
}

long imgx_width(void *p)  { return p ? ((Img *)p)->w : 0; }
long imgx_height(void *p) { return p ? ((Img *)p)->h : 0; }

/* Copy the decoded RGBA out as `bytes` (out-param convention). */
void imgx_pixels(void *p, unsigned char **out, long *outlen) {
    *out = NULL;
    *outlen = 0;
    if (!p) return;
    Img *im = (Img *)p;
    unsigned char *cp = (unsigned char *)malloc(im->nbytes ? im->nbytes : 1);
    if (!cp) return;
    memcpy(cp, im->rgba, im->nbytes);
    *out = cp;
    *outlen = (long)im->nbytes;
}

void imgx_free(void *p) {
    if (p) { Img *im = (Img *)p; free(im->rgba); free(im); }
}

/* Encode w*h RGBA pixels (plen must be >= w*h*4) to a PNG in memory. */
void imgx_encode(const unsigned char *pixels, long plen, long w, long h,
                 unsigned char **out, long *outlen) {
    *out = NULL;
    *outlen = 0;
    if (w <= 0 || h <= 0 || w > 100000 || h > 100000) return;   /* sane bounds, no overflow */
    long need = w * h * 4;
    if (plen < need) return;                                    /* fail closed: not enough pixels */
    png_image image;
    memset(&image, 0, sizeof image);
    image.version = PNG_IMAGE_VERSION;
    image.width   = (png_uint_32)w;
    image.height  = (png_uint_32)h;
    image.format  = PNG_FORMAT_RGBA;
    png_alloc_size_t nbytes = 0;
    if (!png_image_write_to_memory(&image, NULL, &nbytes, 0, pixels, 0, NULL)) return;  /* sizing pass */
    unsigned char *buf = (unsigned char *)malloc(nbytes ? nbytes : 1);
    if (!buf) return;
    if (!png_image_write_to_memory(&image, buf, &nbytes, 0, pixels, 0, NULL)) {
        free(buf); return;
    }
    *out = buf;
    *outlen = (long)nbytes;
}
