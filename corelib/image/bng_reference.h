#ifndef BNG_REFERENCE_H
#define BNG_REFERENCE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define P4_TDIR 63
#define P4_KEXP 6
#define P4_ALPHA 144
#define P4_EOL 131
#define P4_RUN 132
#define P4_MAXLEN 12
#define P4_MAX_PIXELS (1u << 28)

#define P4_MAGIC "\xFF\x0E\x38\x30\x2A\x1A\x18"
#define P4_VERSION 0x01
#define P4_HEADER 20
#define P4_RAW_FLAG 0x80000000u

#define BNG_MAX_ENCODED(w, h) ((size_t)P4_HEADER + 4 * (size_t)(w) * (size_t)(h))
#define P4_COUNT_MASK 0x7FFFFFFFu

#define P4_B 0
#define P4_G 1
#define P4_R 2
#define P4_X 3

static const uint8_t p4_len[P4_ALPHA] = {
    3,  3,  3,  4,  4,  4,  4,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,  7,
    7,  8,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9,  9,
    9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 11,
   11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
   11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
   12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
   12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 11,
   12,  7,  7, 12, 11,  9,  4,  6,  7,  8,  9, 10, 10, 11, 12, 12, 12, 12,
};

typedef struct {
   int base;
   int nbits;
   int kind;
} P4_Token;

static inline P4_Token p4_token(int tk) {
   P4_Token t;
   if (tk == 0) {

      t.base = 0;
      t.nbits = 0;
      t.kind = 0;
   } else if (tk <= 2 * P4_TDIR) {
      t.base = (tk + 1) / 2;
      t.nbits = 0;
      t.kind = (tk & 1) ? 0 : 1;
   } else if (tk < P4_EOL) {
      int k = tk - (2 * P4_TDIR + 1);
      int ex = P4_KEXP + k / 2;
      t.base = 1 << ex;
      t.nbits = ex;
      t.kind = (k & 1) ? 1 : 0;
   } else if (tk == P4_EOL) {

      t.base = 0x7FFFFFFF;
      t.nbits = 0;
      t.kind = 2;
   } else {
      int j = tk - P4_RUN + 1;
      t.base = 1 << j;
      t.nbits = j;
      t.kind = 3;
   }
   return t;
}

typedef struct {
   uint32_t enc_code[P4_ALPHA];
   uint8_t enc_len[P4_ALPHA];
   int first[P4_MAXLEN + 1];
   int count[P4_MAXLEN + 1];
   int base[P4_MAXLEN + 1];
   uint8_t sym[P4_ALPHA];
} P4_Code;

static inline void p4_build(P4_Code *c) {
   int code = 0, idx = 0;
   memset(c, 0, sizeof *c);
   for (int L = 1; L <= P4_MAXLEN; L++) {
      c->first[L] = code;
      c->base[L] = idx;
      for (int tk = 0; tk < P4_ALPHA; tk++) {
         if (p4_len[tk] != L) continue;
         c->enc_code[tk] = (uint32_t)(code + c->count[L]);
         c->enc_len[tk] = (uint8_t)L;
         c->sym[idx++] = (uint8_t)tk;
         c->count[L]++;
      }
      code = (code + c->count[L]) << 1;
   }
}

typedef struct {
   uint8_t *buf;
   size_t cap, n;
   uint32_t acc;
   int nbits;
   int overflow;
} P4_Writer;

static inline void p4_put(P4_Writer *w, uint32_t code, int len) {
   if (len == 0) return;
   w->acc = (w->acc << len) | code;
   w->nbits += len;
   while (w->nbits >= 8) {
      w->nbits -= 8;
      if (w->n < w->cap) w->buf[w->n] = (uint8_t)(w->acc >> w->nbits);
      else w->overflow = 1;
      w->n++;
   }
}

static inline void p4_flush(P4_Writer *w) {
   if (w->nbits == 0) return;
   if (w->n < w->cap) w->buf[w->n] = (uint8_t)(w->acc << (8 - w->nbits));
   else w->overflow = 1;
   w->n++;
   w->nbits = 0;
}

typedef struct {
   const uint8_t *p, *end;
   uint32_t acc;
   int nbits;
   int pad;
   int bad;
} P4_Reader;

static inline int p4_bit(P4_Reader *r) {
   if (r->nbits == 0) {
      uint32_t b = 0;
      if (r->p < r->end) {
         b = *r->p++;
      } else if (++r->pad > 16) {

         r->bad = 1;
         return 0;
      }
      r->acc = b;
      r->nbits = 8;
   }
   r->nbits--;
   return (int)((r->acc >> r->nbits) & 1);
}

static inline uint32_t p4_bits(P4_Reader *r, int n) {
   uint32_t v = 0;
   for (int i = 0; i < n; i++) v = (v << 1) | (uint32_t)p4_bit(r);
   return v;
}

static inline int p4_symbol(P4_Reader *r, const P4_Code *c) {
   int code = 0;
   for (int L = 1; L <= P4_MAXLEN; L++) {
      code = (code << 1) | p4_bit(r);
      if (r->bad) return -1;
      if (code - c->first[L] < c->count[L])
         return c->sym[c->base[L] + code - c->first[L]];
   }
   return -1;
}

static inline int p4_med(int L, int T, int TL) {
   int mn = L < T ? L : T;
   int mx = L > T ? L : T;
   int g = L + T - TL;
   if (g < mn) g = mn;
   if (g > mx) g = mx;
   return g;
}

static inline void p4_to_ycocg(int R, int G, int B, uint8_t *y, uint8_t *co,
                        uint8_t *cg) {
   int c = (R - B) & 0xFF;
   int t = (B + ((int8_t)c >> 1)) & 0xFF;
   int g = (G - t) & 0xFF;
   *co = (uint8_t)c;
   *cg = (uint8_t)g;
   *y = (uint8_t)((t + ((int8_t)g >> 1)) & 0xFF);
}

static inline void p4_from_ycocg(int y, int co, int cg, uint8_t *R, uint8_t *G,
                          uint8_t *B) {
   int t = (y - ((int8_t)cg >> 1)) & 0xFF;
   int g = (cg + t) & 0xFF;
   int b = (t - ((int8_t)co >> 1)) & 0xFF;
   *R = (uint8_t)((b + co) & 0xFF);
   *G = (uint8_t)g;
   *B = (uint8_t)b;
}

static inline int p4_log2(int v) {
   int e = 0;
   while (v >> (e + 1)) e++;
   return e;
}

static inline void p4_put32(uint8_t *p, uint32_t v) {
   p[0] = (uint8_t)v;
   p[1] = (uint8_t)(v >> 8);
   p[2] = (uint8_t)(v >> 16);
   p[3] = (uint8_t)(v >> 24);
}

static inline uint32_t p4_get32(const uint8_t *p) {
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
          ((uint32_t)p[3] << 24);
}

static inline void p4_encode_row(P4_Writer *w, const P4_Code *c, const int8_t *rc,
                          int w_) {
   int x = 0;
   while (x < w_) {
      int e = rc[x];
      if (e != 0) {
         int a = e < 0 ? -e : e;
         int tk;
         if (a <= P4_TDIR) {
            tk = 2 * a - 1 + (e < 0);
            p4_put(w, c->enc_code[tk], c->enc_len[tk]);
         } else {
            int ex = p4_log2(a);
            tk = 2 * P4_TDIR + 1 + 2 * (ex - P4_KEXP) + (e < 0);
            p4_put(w, c->enc_code[tk], c->enc_len[tk]);
            p4_put(w, (uint32_t)(a - (1 << ex)), ex);
         }
         x++;
         continue;
      }
      {
         int run = 1, j;
         while (x + run < w_ && rc[x + run] == 0) run++;
         if (x + run == w_) {

            p4_put(w, c->enc_code[P4_EOL], c->enc_len[P4_EOL]);
            return;
         }
         if (run == 1) {
            p4_put(w, c->enc_code[0], c->enc_len[0]);
            x++;
            continue;
         }
         if (run > 8191) run = 8191;
         j = p4_log2(run);
         p4_put(w, c->enc_code[P4_RUN + j - 1], c->enc_len[P4_RUN + j - 1]);
         p4_put(w, (uint32_t)(run - (1 << j)), j);
         x += run;
      }
   }
}

static inline size_t bng_ref_encode_scratch(int w, int h) {
   return 4 * (size_t)w * (size_t)h + (size_t)w;
}

static inline size_t bng_ref_decode_scratch(int w, int h) {
   (void)h;
   return 12 * (size_t)w;
}

static inline int bng_ref_info(const uint8_t *file, size_t len, int *w, int *h) {
   if (len < P4_HEADER || memcmp(file, P4_MAGIC, 7) != 0) return 0;
   if (file[7] != P4_VERSION) return 0;
   *w = (int)p4_get32(file + 8);
   *h = (int)p4_get32(file + 12);
   if (*w <= 0 || *h <= 0) return 0;
   if ((uint64_t)*w * (uint64_t)*h > P4_MAX_PIXELS) return 0;
   return 1;
}

static inline size_t p4_encode_body(const uint8_t *px, int w, int h, size_t pitch,
                             const P4_Code *code, uint8_t *body, size_t cap,
                             uint8_t *scratch) {
   size_t n = (size_t)w * (size_t)h;
   uint8_t *plane[4];
   int8_t *res;
   P4_Writer wr;

   for (int p = 0; p < 4; p++) plane[p] = scratch + (size_t)p * n;
   res = (int8_t *)(scratch + 4 * n);

   for (int y = 0; y < h; y++) {
      const uint8_t *src = px + (size_t)y * pitch;
      for (int x = 0; x < w; x++) {
         size_t i = (size_t)y * w + x;
         p4_to_ycocg(src[x * 4 + P4_R], src[x * 4 + P4_G], src[x * 4 + P4_B],
                     &plane[0][i], &plane[1][i], &plane[2][i]);
         plane[3][i] = src[x * 4 + P4_X];
      }
   }

   memset(&wr, 0, sizeof wr);
   wr.buf = body;
   wr.cap = cap;
   for (int y = 0; y < h; y++) {
      for (int p = 0; p < 4; p++) {
         const uint8_t *row = plane[p] + (size_t)y * w;
         const uint8_t *prev = row - w;
         for (int x = 0; x < w; x++) {
            int L, T, TL;
            if (y == 0) {
               L = x ? row[x - 1] : 0;
               T = L;
               TL = L;
            } else {
               T = prev[x];
               L = x ? row[x - 1] : T;
               TL = x ? prev[x - 1] : T;
            }
            res[x] = (int8_t)((row[x] - p4_med(L, T, TL)) & 0xFF);
         }
         p4_encode_row(&wr, code, res, w);
      }
   }
   p4_flush(&wr);
   return wr.n;
}

static inline size_t bng_ref_encode(const uint8_t *px, int w, int h, size_t pitch,
                      uint8_t *out, size_t out_cap, void *scratch,
                      size_t scratch_len) {
   size_t n = (size_t)w * (size_t)h;
   size_t raw_bytes = 4 * n;
   size_t got;
   P4_Code code;

   if (w <= 0 || h <= 0) return 0;
   if (pitch < (size_t)w * 4) return 0;
   if (out_cap < P4_HEADER + raw_bytes) return 0;
   if (scratch_len < bng_ref_encode_scratch(w, h)) return 0;
   p4_build(&code);

   got = p4_encode_body(px, w, h, pitch, &code, out + P4_HEADER, raw_bytes,
                        (uint8_t *)scratch);

   memcpy(out, P4_MAGIC, 7);
   out[7] = P4_VERSION;
   p4_put32(out + 8, (uint32_t)w);
   p4_put32(out + 12, (uint32_t)h);

   if (got >= raw_bytes) {

      for (int y = 0; y < h; y++)
         memcpy(out + P4_HEADER + (size_t)y * w * 4, px + (size_t)y * pitch,
                (size_t)w * 4);
      p4_put32(out + 16, (uint32_t)(raw_bytes | P4_RAW_FLAG));
      return P4_HEADER + raw_bytes;
   }
   p4_put32(out + 16, (uint32_t)got);
   return P4_HEADER + got;
}

static inline int p4_decode_row(P4_Reader *r, const P4_Code *c, int8_t *rc, int w_) {
   int x = 0;
   memset(rc, 0, (size_t)w_);
   while (x < w_) {
      int tk = p4_symbol(r, c);
      P4_Token t;
      int v, adv;
      if (tk < 0) return 0;
      t = p4_token(tk);
      v = t.base + (int)p4_bits(r, t.nbits);
      if (r->bad) return 0;
      if (t.kind == 0 || t.kind == 1) {
         rc[x] = (int8_t)(t.kind ? -v : v);
         x++;
         continue;
      }

      adv = v > w_ - x ? w_ - x : v;
      if (adv <= 0) return 0;
      x += adv;
   }
   return 1;
}

static inline int bng_ref_decode(const uint8_t *file, size_t len, uint8_t *out,
                   size_t out_pitch, size_t out_cap, void *scratch,
                   size_t scratch_len) {
   P4_Code code;
   P4_Reader r;
   uint8_t *cur[4], *prv[4];
   int8_t *rc[4];
   int w, h, raw;
   size_t n, stream_byte_count;
   uint8_t *sp = (uint8_t *)scratch;

   if (!bng_ref_info(file, len, &w, &h)) return 0;
   n = (size_t)w * (size_t)h;
   {
      uint32_t field = p4_get32(file + 16);
      raw = (field & P4_RAW_FLAG) != 0;
      stream_byte_count = field & P4_COUNT_MASK;
   }

   if (stream_byte_count > len - P4_HEADER) return 0;
   len = P4_HEADER + stream_byte_count;
   if (out_pitch < (size_t)w * 4) return 0;
   if (out_cap < (size_t)(h - 1) * out_pitch + (size_t)w * 4) return 0;

   if (raw) {

      if (stream_byte_count != 4 * n) return 0;
      for (int y = 0; y < h; y++)
         memcpy(out + (size_t)y * out_pitch,
                file + P4_HEADER + (size_t)y * w * 4, (size_t)w * 4);
      return 1;
   }
   if (scratch_len < bng_ref_decode_scratch(w, h)) return 0;

   if ((uint64_t)stream_byte_count * 8 < (uint64_t)h) return 0;

   p4_build(&code);
   memset(&r, 0, sizeof r);
   r.p = file + P4_HEADER;
   r.end = file + len;

   for (int p = 0; p < 4; p++) {
      cur[p] = sp + (size_t)p * w;
      prv[p] = sp + (size_t)(p + 4) * w;
      rc[p] = (int8_t *)(sp + (size_t)(p + 8) * w);
      memset(cur[p], 0, (size_t)w);
      memset(prv[p], 0, (size_t)w);
   }

   for (int y = 0; y < h; y++) {
      uint8_t *o = out + (size_t)y * out_pitch;
      for (int p = 0; p < 4; p++) {
         if (!p4_decode_row(&r, &code, rc[p], w)) return 0;

         for (int x = 0; x < w; x++) {
            int L, T, TL;
            if (y == 0) {
               L = x ? cur[p][x - 1] : 0;
               T = L;
               TL = L;
            } else {
               T = prv[p][x];
               L = x ? cur[p][x - 1] : T;
               TL = x ? prv[p][x - 1] : T;
            }
            cur[p][x] = (uint8_t)((p4_med(L, T, TL) + rc[p][x]) & 0xFF);
         }
      }
      for (int x = 0; x < w; x++) {
         p4_from_ycocg(cur[0][x], cur[1][x], cur[2][x], &o[x * 4 + P4_R],
                       &o[x * 4 + P4_G], &o[x * 4 + P4_B]);
         o[x * 4 + P4_X] = cur[3][x];
      }
      for (int p = 0; p < 4; p++) {
         uint8_t *t = cur[p];
         cur[p] = prv[p];
         prv[p] = t;
      }
   }
   return 1;
}

#endif
