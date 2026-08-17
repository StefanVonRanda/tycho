#include <png.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifndef TYCHO_INT_T
#define TYCHO_INT_T
typedef int64_t tycho_int;
#endif

typedef struct { tycho_int w, h; unsigned char *rgba; size_t nbytes; } Img;

/* Status codes, mirrored by the constants in image.ty. */
#define IMG_OK       0
#define IMG_EMPTY    1
#define IMG_NOTPNG   2
#define IMG_CORRUPT  3
#define IMG_BADDIMS  4
#define IMG_SHORTPX  5
#define IMG_FAILED   6
#define IMG_TOOBIG   7   /* the decoded image passes IMG_MAX_OUT */
/* A ceiling on ONE decoded image. libpng's own limits allow dimensions whose
 * RGBA buffer is tens of gigabytes, and the header declaring them costs the
 * attacker nothing. Unlike the compress bomb this fails SAFELY today -- malloc
 * returns NULL and that branch is handled -- so this is defence in depth, and it
 * buys a NAMED error instead of a generic allocation failure. Overridable at
 * compile time so a gate can prove the ceiling fires without allocating 512 MiB.
 * 512 MiB of RGBA is an 11600x11600 image. */
#ifndef IMG_MAX_OUT
#define IMG_MAX_OUT  ((size_t)512 * 1024 * 1024)
#endif

void *imgx_decode(const unsigned char *data, tycho_int len, tycho_int *status) {
    *status = IMG_FAILED;
    if (len <= 0) { *status = IMG_EMPTY; return NULL; }
    png_image image;
    memset(&image, 0, sizeof image);
    image.version = PNG_IMAGE_VERSION;
    /* The header did not read: wrong signature, or the data stops inside it. */
    if (!png_image_begin_read_from_memory(&image, data, (size_t)len)) {
        *status = IMG_NOTPNG; return NULL;
    }
    image.format = PNG_FORMAT_RGBA;                 /* always hand back 8-bit RGBA */
    size_t nbytes = PNG_IMAGE_SIZE(image);
    if (nbytes > IMG_MAX_OUT) {
        png_image_free(&image); *status = IMG_TOOBIG; return NULL;
    }
    unsigned char *buf = (unsigned char *)malloc(nbytes ? nbytes : 1);
    if (!buf) { png_image_free(&image); return NULL; }
    /* The header read, the pixel data did not: truncated or damaged. */
    if (!png_image_finish_read(&image, NULL, buf, 0, NULL)) {
        free(buf); png_image_free(&image); *status = IMG_CORRUPT; return NULL;
    }
    Img *im = (Img *)malloc(sizeof *im);
    if (!im) { free(buf); return NULL; }
    *status = IMG_OK;
    im->w = (tycho_int)image.width;
    im->h = (tycho_int)image.height;
    im->rgba = buf;
    im->nbytes = nbytes;
    return im;                                       /* png_image_free not needed after finish_read */
}

tycho_int imgx_width(void *p)  { return p ? ((Img *)p)->w : 0; }
tycho_int imgx_height(void *p) { return p ? ((Img *)p)->h : 0; }

/* Copy the decoded RGBA out as `bytes` (out-param convention). */
void imgx_pixels(void *p, unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    if (!p) return;
    Img *im = (Img *)p;
    unsigned char *cp = (unsigned char *)malloc(im->nbytes ? im->nbytes : 1);
    if (!cp) return;
    memcpy(cp, im->rgba, im->nbytes);
    *out = cp;
    *outlen = (tycho_int)im->nbytes;
}

void imgx_free(void *p) {
    if (p) { Img *im = (Img *)p; free(im->rgba); free(im); }
}

/* Encode w*h RGBA pixels (plen must be >= w*h*4) to a PNG in memory. */
void imgx_encode(const unsigned char *pixels, tycho_int plen, tycho_int w, tycho_int h,
                 tycho_int *status, unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    *status = IMG_FAILED;
    if (w <= 0 || h <= 0 || w > 100000 || h > 100000) {         /* sane bounds, no overflow */
        *status = IMG_BADDIMS; return;
    }
    tycho_int need = w * h * 4;
    if (plen < need) { *status = IMG_SHORTPX; return; }         /* fail closed: not enough pixels */
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
    *outlen = (tycho_int)nbytes;
    *status = IMG_OK;
}
