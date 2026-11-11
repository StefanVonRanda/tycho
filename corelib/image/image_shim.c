#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "stb_image.h"
#pragma GCC diagnostic pop
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "stb_image_write.h"
#pragma GCC diagnostic pop
#include <png.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include "../tycho.h"
#include "bng_reference.h"

#include <stdio.h>

/* A RUN-TIME guard, not a compile-time rule: a `ptr` handle is not affine, so
   nothing stops a caller freeing one twice or using it afterwards. The handle
   is a monotonically numbered ID, not a heap pointer: freeing unlinks the
   entry, and a stale ID cannot alias a later image even when malloc reuses an
   address. The Img header is released with its pixels, so decoding is not a
   per-image leak. */
typedef struct Img {
    struct Img *next;
    uint64_t id;
    tycho_int w, h;
    unsigned char *rgba;
    size_t nbytes;
} Img;

static Img *g_images = NULL;
static uint64_t g_next_id = 1;
static atomic_flag g_images_lock = ATOMIC_FLAG_INIT;

static void imgx_acquire(void) {
    while (atomic_flag_test_and_set_explicit(&g_images_lock, memory_order_acquire)) {}
}

static void imgx_release(void) {
    atomic_flag_clear_explicit(&g_images_lock, memory_order_release);
}

static Img *imgx_find(void *p) {
    uintptr_t id = (uintptr_t)p;
    Img *found = NULL;
    if (id == 0) return NULL;
    imgx_acquire();
    for (Img *im = g_images; im; im = im->next) {
        if (im->id == id) { found = im; break; }
    }
    imgx_release();
    return found;
}

static void imgx_live(void *p, int freeing) {
    if (!p || imgx_find(p)) return;
    fputs(freeing ? "tycho: double free of image handle\n"
                  : "tycho: image handle used after free\n", stderr);
    exit(1);
}

static void imgx_add(Img *im) {
    imgx_acquire();
    im->id = g_next_id++;
    im->next = g_images;
    g_images = im;
    imgx_release();
}

static Img *imgx_take(void *p) {
    uintptr_t id = (uintptr_t)p;
    Img *found = NULL;
    if (id == 0) return NULL;
    imgx_acquire();
    Img **pp = &g_images;
    while (*pp) {
        if ((*pp)->id == id) {
            found = *pp;
            *pp = found->next;
            break;
        }
        pp = &(*pp)->next;
    }
    imgx_release();
    return found;
}

/* Status codes, mirrored by the constants in image.ty. */
#define IMG_OK       0
#define IMG_EMPTY    1
#define IMG_NOTPNG   2
#define IMG_CORRUPT  3
#define IMG_BADDIMS  4
#define IMG_SHORTPX  5
#define IMG_FAILED   6
#define IMG_TOOBIG   7   /* the decoded image passes IMG_MAX_OUT */
#define IMG_NOTBNG   8   /* the BNG signature or version did not read */
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
    if (!im) { free(buf); png_image_free(&image); return NULL; }
    *status = IMG_OK;
    im->w = (tycho_int)image.width;
    im->h = (tycho_int)image.height;
    im->rgba = buf;
    im->nbytes = nbytes;
    imgx_add(im);
    return (void *)(uintptr_t)im->id;                /* opaque ID, never dereferenced by Tycho */
}

tycho_int imgx_width(void *p) {
    if (!p) return 0;
    imgx_live(p, 0);
    Img *im = imgx_find(p);
    return im ? im->w : 0;
}

tycho_int imgx_height(void *p) {
    if (!p) return 0;
    imgx_live(p, 0);
    Img *im = imgx_find(p);
    return im ? im->h : 0;
}

/* Copy the decoded RGBA out as `bytes` (out-param convention). */
void imgx_pixels(void *p, unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    if (!p) return;
    imgx_live(p, 0);
    Img *im = imgx_find(p);
    if (!im) return;
    unsigned char *cp = (unsigned char *)malloc(im->nbytes ? im->nbytes : 1);
    if (!cp) return;
    memcpy(cp, im->rgba, im->nbytes);
    *out = cp;
    *outlen = (tycho_int)im->nbytes;
}

void imgx_free(void *p) {
    if (!p) return;
    imgx_live(p, 1);
    Img *im = imgx_take(p);
    if (!im) return;
    free(im->rgba);
    free(im);
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

/* Baseline JPEG decode through the vendored stb_image (JPEG only). */
void *imgx_jpeg_decode(const unsigned char *data, tycho_int len, tycho_int *status) {
    *status = IMG_CORRUPT;
    if (len <= 0) { *status = IMG_EMPTY; return NULL; }
    if (len > INT32_MAX) { *status = IMG_FAILED; return NULL; }
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load_from_memory(data, (int)len, &w, &h, &n, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        return NULL;
    }
    size_t nbytes = (size_t)w * (size_t)h * 4;
    if (nbytes > IMG_MAX_OUT) { stbi_image_free(px); *status = IMG_TOOBIG; return NULL; }
    Img *im = (Img *)calloc(1, sizeof *im);
    if (!im) { stbi_image_free(px); *status = IMG_FAILED; return NULL; }
    im->w = w;
    im->h = h;
    im->rgba = px;
    im->nbytes = nbytes;
    imgx_add(im);
    *status = IMG_OK;
    return (void *)(uintptr_t)im->id;
}

typedef struct { unsigned char *buf; size_t len, cap; int failed; } JpegOut;

static void jpeg_write(void *ctx, void *data, int size) {
    JpegOut *o = (JpegOut *)ctx;
    if (o->failed || size <= 0) return;
    size_t need = o->len + (size_t)size;
    if (need > o->cap) {
        size_t cap = o->cap ? o->cap : 4096;
        while (cap < need) cap *= 2;
        unsigned char *p = (unsigned char *)realloc(o->buf, cap);
        if (!p) { o->failed = 1; return; }
        o->buf = p;
        o->cap = cap;
    }
    memcpy(o->buf + o->len, data, (size_t)size);
    o->len += (size_t)size;
}

void imgx_jpeg_encode(const unsigned char *pixels, tycho_int plen, tycho_int w, tycho_int h,
                      tycho_int *status, unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    *status = IMG_FAILED;
    if (w <= 0 || h <= 0 || w > 100000 || h > 100000) { *status = IMG_BADDIMS; return; }
    tycho_int need = w * h * 4;
    if (plen < need) { *status = IMG_SHORTPX; return; }
    JpegOut o = {0};
    if (!stbi_write_jpg_to_func(jpeg_write, &o, (int)w, (int)h, 4, pixels, 90) || o.failed || !o.buf) {
        free(o.buf);
        return;
    }
    *out = o.buf;
    *outlen = (tycho_int)o.len;
    *status = IMG_OK;
}

/* ---- BNG (Better than PNG) ---------------------------------------------- */

void *imgx_bng_decode(const unsigned char *data, tycho_int len, tycho_int premultiplied,
                      tycho_int *status) {
    *status = IMG_FAILED;
    if (len <= 0) { *status = IMG_EMPTY; return NULL; }
    int w = 0, h = 0;
    if (!bng_ref_info(data, (size_t)len, &w, &h)) { *status = IMG_NOTBNG; return NULL; }
    size_t n = (size_t)w * (size_t)h;
    if (n > IMG_MAX_OUT / 4) { *status = IMG_TOOBIG; return NULL; }
    size_t nbytes = 4 * n;
    unsigned char *bgra = (unsigned char *)malloc(nbytes ? nbytes : 1);
    size_t scratch_len = bng_ref_decode_scratch(w, h);
    void *scratch = malloc(scratch_len ? scratch_len : 1);
    if (!bgra || !scratch) { free(bgra); free(scratch); return NULL; }
    if (!bng_ref_decode(data, (size_t)len, bgra, (size_t)w * 4, nbytes,
                        scratch, scratch_len)) {
        free(bgra); free(scratch); *status = IMG_CORRUPT; return NULL;
    }
    free(scratch);
    unsigned char *rgba = (unsigned char *)malloc(nbytes ? nbytes : 1);
    if (!rgba) { free(bgra); return NULL; }
    for (size_t i = 0; i < n; i++) {
        unsigned b = bgra[4 * i + 0];
        unsigned g = bgra[4 * i + 1];
        unsigned r = bgra[4 * i + 2];
        unsigned a = bgra[4 * i + 3];
        if (!premultiplied && a != 0 && a != 255) {
            r = (r * 255 + a / 2) / a; if (r > 255) r = 255;
            g = (g * 255 + a / 2) / a; if (g > 255) g = 255;
            b = (b * 255 + a / 2) / a; if (b > 255) b = 255;
        }
        rgba[4 * i + 0] = (unsigned char)r;
        rgba[4 * i + 1] = (unsigned char)g;
        rgba[4 * i + 2] = (unsigned char)b;
        rgba[4 * i + 3] = (unsigned char)a;
    }
    free(bgra);
    Img *im = (Img *)malloc(sizeof *im);
    if (!im) { free(rgba); return NULL; }
    im->w = (tycho_int)w;
    im->h = (tycho_int)h;
    im->rgba = rgba;
    im->nbytes = nbytes;
    imgx_add(im);
    *status = IMG_OK;
    return (void *)(uintptr_t)im->id;
}

void imgx_bng_encode(const unsigned char *pixels, tycho_int plen, tycho_int w, tycho_int h,
                     tycho_int premultiplied, tycho_int *status,
                     unsigned char **out, tycho_int *outlen) {
    *out = NULL;
    *outlen = 0;
    *status = IMG_FAILED;
    if (w <= 0 || h <= 0 || w > 100000 || h > 100000) { *status = IMG_BADDIMS; return; }
    tycho_int need = w * h * 4;
    if (plen < need) { *status = IMG_SHORTPX; return; }
    size_t n = (size_t)w * (size_t)h;
    if (n > IMG_MAX_OUT / 4) { *status = IMG_TOOBIG; return; }
    unsigned char *bgra = (unsigned char *)malloc(n ? 4 * n : 1);
    if (!bgra) return;
    for (size_t i = 0; i < n; i++) {
        unsigned r = pixels[4 * i + 0];
        unsigned g = pixels[4 * i + 1];
        unsigned b = pixels[4 * i + 2];
        unsigned a = pixels[4 * i + 3];
        if (!premultiplied && a != 255) {
            r = (r * a + 127) / 255;
            g = (g * a + 127) / 255;
            b = (b * a + 127) / 255;
        }
        bgra[4 * i + 0] = (unsigned char)b;
        bgra[4 * i + 1] = (unsigned char)g;
        bgra[4 * i + 2] = (unsigned char)r;
        bgra[4 * i + 3] = (unsigned char)a;
    }
    size_t cap = P4_HEADER + 4 * n;
    unsigned char *buf = (unsigned char *)malloc(cap);
    size_t scratch_len = bng_ref_encode_scratch(w, h);
    void *scratch = malloc(scratch_len ? scratch_len : 1);
    if (!buf || !scratch) { free(bgra); free(buf); free(scratch); return; }
    size_t got = bng_ref_encode(bgra, w, h, (size_t)w * 4, buf, cap, scratch, scratch_len);
    free(bgra);
    free(scratch);
    if (!got) { free(buf); return; }
    *out = buf;
    *outlen = (tycho_int)got;
    *status = IMG_OK;
}
