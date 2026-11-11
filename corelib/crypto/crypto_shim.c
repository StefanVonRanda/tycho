#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/bio.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/crypto.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>    /* INT_MAX -- the pbkdf2 iteration/salt bounds */
#include "../tycho.h"

static const char *const ERRTAG = "!err";     /* non-hex failure sentinel for string returns */

/* ---- recycled hex return buffer (one per thread) ---- */
static __thread char *g_out = NULL;
static __thread size_t g_out_n = 0;               /* what is still in it */
static const char *out_hex(const unsigned char *buf, size_t n) {
    static const char H[] = "0123456789abcdef";
    /* Wipe the PREVIOUS contents before realloc may copy or release them: this
       buffer held the last hex produced, and cx_key_export_hex puts a raw secret
       key through it. */
    if (g_out && g_out_n) OPENSSL_cleanse(g_out, g_out_n);
    char *p = realloc(g_out, 2 * n + 1);
    if (!p) return ERRTAG;
    g_out = p;
    for (size_t i = 0; i < n; i++) {
        g_out[2 * i]     = H[(buf[i] >> 4) & 0xf];
        g_out[2 * i + 1] = H[buf[i] & 0xf];
    }
    g_out[2 * n] = '\0';
    g_out_n = 2 * n;
    return g_out;
}

static unsigned ct_lt(unsigned a, unsigned b) { return ((a - b) >> 8) & 1u; }  /* a,b < 256 */

static unsigned hexval_ct(unsigned c, unsigned *v) {
    unsigned d = (c - '0') & 0xFFu;                 /* 0..9  for '0'..'9' */
    unsigned x = ((c | 0x20u) - 'a') & 0xFFu;       /* 0..5  for 'a'..'f' and 'A'..'F' */
    unsigned is_d = ct_lt(d, 10), is_x = ct_lt(x, 6);
    *v = is_d * d + is_x * (x + 10);
    return is_d | is_x;
}

/* The whole string is always walked; *bad is the one public bit (was the hex
   well-formed), which the caller may branch on and the probe declassifies. */
static unsigned char *hexdec_ct(const char *s, size_t *outlen, unsigned *bad) {
    size_t L = strlen(s);
    *bad = (unsigned)(L & 1u);
    if (*bad) return NULL;                          /* the LENGTH is not secret */
    size_t n = L / 2;
    unsigned char *b = malloc(n ? n : 1);
    if (!b) { *bad = 1; return NULL; }
    unsigned ok = 1;
    for (size_t i = 0; i < n; i++) {
        unsigned hi, lo;
        ok &= hexval_ct((unsigned char)s[2 * i], &hi);
        ok &= hexval_ct((unsigned char)s[2 * i + 1], &lo);
        b[i] = (unsigned char)((hi << 4) | lo);
    }
    *outlen = n;
    *bad = ok ^ 1u;
    return b;
}

static unsigned char *hexdec(const char *s, size_t *outlen) {
    unsigned bad = 0;
    unsigned char *b = hexdec_ct(s, outlen, &bad);
    if (b && bad) { OPENSSL_cleanse(b, *outlen); free(b); return NULL; }
    return b;
}

/* =====================================================================
 * Opaque secret key handle
 * ===================================================================== */
/* A RUN-TIME guard, not a compile-time rule: a `ptr` handle is not affine, so
   nothing stops a caller freeing one twice or using it afterwards. The header is
   deliberately NOT released on free -- the sentinel in it is what the next call
   reads. It holds no key material; the key bytes are cleansed and freed. */
#define CXK_LIVE 0x43784b45594c4956ull
#define CXK_DEAD 0x43784b4559444544ull
typedef struct { uint64_t magic; unsigned char *buf; size_t len; } CxKey;

static void cxk_live(const CxKey *k, int freeing) {
    if (!k || k->magic == CXK_LIVE) return;
    fputs(freeing ? "tycho: double free of crypto key handle\n"
                  : "tycho: crypto key handle used after free\n", stderr);
    exit(1);
}

void cx_key_free(void *kp);   /* fwd: used by constructors on the error path */

static CxKey *key_new(size_t n) {
    CxKey *k = malloc(sizeof *k);
    if (!k) return NULL;
    k->buf = malloc(n ? n : 1);
    if (!k->buf) { free(k); return NULL; }
    k->magic = CXK_LIVE;
    k->len = n;
    return k;
}

void cx_key_free(void *kp) {
    CxKey *k = kp;
    if (!k) return;
    cxk_live(k, 1);
    if (k->buf) { OPENSSL_cleanse(k->buf, k->len); free(k->buf); }
    k->buf = NULL;
    k->len = 0;
    k->magic = CXK_DEAD;
}

/* n cryptographically secure random bytes -> a new secret key handle (NULL on failure) */
void *cx_key_random(tycho_int n) {
    if (n < 1 || n > (1L << 20)) return NULL;
    CxKey *k = key_new((size_t)n);
    if (!k) return NULL;
    if (RAND_bytes(k->buf, (int)n) != 1) { cx_key_free(k); return NULL; }
    return k;
}

/* import a key from hex (e.g. one you stored) -> handle (NULL on bad hex) */
void *cx_key_from_hex(const char *hex) {
    size_t n;
    unsigned char *b = hexdec(hex, &n);
    if (!b) return NULL;
    CxKey *k = key_new(n);
    if (k) memcpy(k->buf, b, n);
    OPENSSL_cleanse(b, n);            /* don't leave a plaintext copy from the decode buffer */
    free(b);
    return k;
}

/* explicit re-materialization: the one place a secret leaves the handle */
const char *cx_key_export_hex(void *kp) {
    CxKey *k = kp;
    cxk_live(k, 0);
    if (!k) return ERRTAG;
    return out_hex(k->buf, k->len);
}

tycho_int cx_key_len(void *kp) { CxKey *k = kp; cxk_live(k, 0); return k ? (tycho_int)k->len : -1; }

/* =====================================================================
 * CSPRNG (public bytes: nonces, salts) -> hex
 * ===================================================================== */
const char *cx_random_hex(tycho_int n) {
    if (n < 0 || n > (1L << 20)) return ERRTAG;
    unsigned char *b = malloc(n ? (size_t)n : 1);
    if (!b) return ERRTAG;
    const char *res = (RAND_bytes(b, (int)n) == 1) ? out_hex(b, (size_t)n) : ERRTAG;
    free(b);
    return res;
}

/* =====================================================================
 * Hashing (public data; message is hex) -> hex
 * ===================================================================== */
static const char *digest_hex(const EVP_MD *md, const char *msg_hex) {
    size_t mlen;
    unsigned char *m = hexdec(msg_hex, &mlen);
    if (!m) return ERRTAG;
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int olen = 0;
    const char *res = (EVP_Digest(m, mlen, out, &olen, md, NULL) == 1)
                          ? out_hex(out, olen) : ERRTAG;
    free(m);
    return res;
}
const char *cx_sha256_hex(const char *msg_hex) { return digest_hex(EVP_sha256(), msg_hex); }
const char *cx_sha1_hex(const char *msg_hex) { return digest_hex(EVP_sha1(), msg_hex); }
const char *cx_sha512_hex(const char *msg_hex) { return digest_hex(EVP_sha512(), msg_hex); }
const char *cx_sha3_256_hex(const char *msg_hex) { return digest_hex(EVP_sha3_256(), msg_hex); }
const char *cx_sha3_512_hex(const char *msg_hex) { return digest_hex(EVP_sha3_512(), msg_hex); }
const char *cx_blake2b_hex(const char *msg_hex) { return digest_hex(EVP_blake2b512(), msg_hex); }
const char *cx_blake2s_hex(const char *msg_hex) { return digest_hex(EVP_blake2s256(), msg_hex); }

static const char *xof_hex(const EVP_MD *md, const char *msg_hex, tycho_int outlen) {
    if (outlen < 1 || outlen > (1L << 20)) return ERRTAG;
    size_t mlen;
    unsigned char *m = hexdec(msg_hex, &mlen);
    if (!m) return ERRTAG;
    unsigned char *out = malloc((size_t)outlen);
    if (!out) { free(m); return ERRTAG; }
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    const char *res = ERRTAG;
    if (ctx && EVP_DigestInit_ex(ctx, md, NULL) == 1 &&
        EVP_DigestUpdate(ctx, m, mlen) == 1 &&
        EVP_DigestFinalXOF(ctx, out, (size_t)outlen) == 1) {
        res = out_hex(out, (size_t)outlen);
    }
    if (ctx) EVP_MD_CTX_free(ctx);
    free(out);
    free(m);
    return res;
}
const char *cx_shake128_hex(const char *msg_hex, tycho_int outlen) { return xof_hex(EVP_shake128(), msg_hex, outlen); }
const char *cx_shake256_hex(const char *msg_hex, tycho_int outlen) { return xof_hex(EVP_shake256(), msg_hex, outlen); }

/* =====================================================================
 * HMAC-SHA256(key handle, msg_hex) -> hex MAC
 * ===================================================================== */
const char *cx_hmac_sha256_hex(void *kp, const char *msg_hex) {
    CxKey *k = kp;
    cxk_live(k, 0);
    if (!k) return ERRTAG;
    size_t mlen;
    unsigned char *m = hexdec(msg_hex, &mlen);
    if (!m) return ERRTAG;
    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int maclen = 0;
    unsigned char *r = HMAC(EVP_sha256(), k->buf, (int)k->len, m, mlen, mac, &maclen);
    const char *res = r ? out_hex(mac, maclen) : ERRTAG;
    free(m);
    return res;
}

const char *cx_hmac_sha512_hex(void *kp, const char *msg_hex) {
    CxKey *k = kp;
    cxk_live(k, 0);
    if (!k) return ERRTAG;
    size_t mlen;
    unsigned char *m = hexdec(msg_hex, &mlen);
    if (!m) return ERRTAG;
    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int maclen = 0;
    unsigned char *r = HMAC(EVP_sha512(), k->buf, (int)k->len, m, mlen, mac, &maclen);
    const char *res = r ? out_hex(mac, maclen) : ERRTAG;
    free(m);
    return res;
}

/* HKDF-SHA256 (RFC 5869): IKM is a key handle; salt and info are hex. */
void *cx_hkdf_sha256(void *ikm, const char *salt_hex, const char *info_hex, tycho_int outlen) {
    if (outlen < 1 || outlen > (1L << 20)) return NULL;
    CxKey *k = ikm;
    cxk_live(k, 0);
    if (!k) return NULL;
    size_t slen = 0, ilen = 0;
    unsigned char *salt = hexdec(salt_hex, &slen);
    unsigned char *info = hexdec(info_hex, &ilen);
    if (!salt || !info) { free(salt); free(info); return NULL; }
    if (slen > (size_t)INT_MAX || ilen > (size_t)INT_MAX) {
        free(salt); free(info); return NULL;
    }
    CxKey *out = key_new((size_t)outlen);
    EVP_PKEY_CTX *p = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL);
    int ok = out && p &&
             EVP_PKEY_derive_init(p) > 0 &&
             EVP_PKEY_CTX_set_hkdf_md(p, EVP_sha256()) > 0 &&
             (slen == 0 || EVP_PKEY_CTX_set1_hkdf_salt(p, salt, (int)slen) > 0) &&
             EVP_PKEY_CTX_set1_hkdf_key(p, k->buf, (int)k->len) > 0 &&
             (ilen == 0 || EVP_PKEY_CTX_add1_hkdf_info(p, info, (int)ilen) > 0);
    if (ok) {
        size_t n = (size_t)outlen;
        ok = EVP_PKEY_derive(p, out->buf, &n) > 0 && n == (size_t)outlen;
    }
    if (p) EVP_PKEY_CTX_free(p);
    free(salt);
    free(info);
    if (!ok) {
        if (out) cx_key_free(out);
        return NULL;
    }
    return out;
}

/* Argon2id (OpenSSL 3 KDF provider): password is length-counted, salt is hex. */
void *cx_argon2id(const char *password, tycho_int pwlen, const char *salt_hex,
                  tycho_int iters, tycho_int memcost, tycho_int lanes, tycho_int dklen) {
    if (!password || pwlen < 0 || pwlen > 1048576) return NULL;
    if (iters < 1 || memcost < 8 || lanes < 1 || lanes > 64) return NULL;
    if (dklen < 1 || dklen > 1024) return NULL;
    size_t slen = 0;
    unsigned char *salt = hexdec(salt_hex, &slen);
    if (!salt || slen < 8) { free(salt); return NULL; }
    CxKey *k = key_new((size_t)dklen);
    if (!k) { free(salt); return NULL; }
    EVP_KDF *kdf = EVP_KDF_fetch(NULL, "ARGON2ID", NULL);
    EVP_KDF_CTX *ctx = kdf ? EVP_KDF_CTX_new(kdf) : NULL;
    int ok = 0;
    if (ctx) {
        unsigned int it = (unsigned int)iters;
        unsigned int mc = (unsigned int)memcost;
        unsigned int ln = (unsigned int)lanes;
        OSSL_PARAM params[6];
        params[0] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD, (void *)password, (size_t)pwlen);
        params[1] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, salt, slen);
        params[2] = OSSL_PARAM_construct_uint(OSSL_KDF_PARAM_ITER, &it);
        params[3] = OSSL_PARAM_construct_uint(OSSL_KDF_PARAM_ARGON2_MEMCOST, &mc);
        params[4] = OSSL_PARAM_construct_uint(OSSL_KDF_PARAM_ARGON2_LANES, &ln);
        params[5] = OSSL_PARAM_construct_end();
        ok = EVP_KDF_derive(ctx, k->buf, (size_t)dklen, params) > 0;
    }
    if (ctx) EVP_KDF_CTX_free(ctx);
    if (kdf) EVP_KDF_free(kdf);
    free(salt);
    if (!ok) { cx_key_free(k); return NULL; }
    return k;
}

void *cx_pbkdf2_sha256(const char *password, tycho_int pwlen, const char *salt_hex, tycho_int iters, tycho_int dklen) {
    /* PKCS5_PBKDF2_HMAC takes `int iter`. Refuse rather than truncate: 2^32+1
     * silently derived a key at ONE iteration, and every caller still worked. */
    if (iters < 1 || iters > INT_MAX) return NULL;
    if (dklen < 1 || dklen > 1024) return NULL;
    if (pwlen < 0 || pwlen > 1048576) return NULL;   /* fail closed, never strlen */
    size_t slen;
    unsigned char *salt = hexdec(salt_hex, &slen);
    if (!salt) return NULL;
    if (slen > (size_t)INT_MAX) { free(salt); return NULL; }   /* same truncation, salt side */
    CxKey *k = key_new((size_t)dklen);
    if (k && PKCS5_PBKDF2_HMAC(password, (int)pwlen, salt, (int)slen,
                               (int)iters, EVP_sha256(), (int)dklen, k->buf) != 1) {
        cx_key_free(k);
        k = NULL;
    }
    free(salt);
    return k;
}

tycho_int cx_ct_equal(const char *a_hex, tycho_int alen, const char *b_hex, tycho_int blen) {
    size_t an, bn;
    if (alen < 0 || blen < 0) return 0;
    if ((size_t)alen != strlen(a_hex) || (size_t)blen != strlen(b_hex)) return 0;
    unsigned char *a = hexdec(a_hex, &an);
    if (!a) return 0;
    unsigned char *b = hexdec(b_hex, &bn);
    if (!b) { free(a); return 0; }
    tycho_int eq = (an == bn && CRYPTO_memcmp(a, b, an) == 0) ? 1 : 0;
    free(a); free(b);
    return eq;
}

static const EVP_CIPHER *aes_gcm_cipher(size_t klen) {
    if (klen == 16) return EVP_aes_128_gcm();
    if (klen == 24) return EVP_aes_192_gcm();
    if (klen == 32) return EVP_aes_256_gcm();
    return NULL;
}

static const EVP_CIPHER *aes_ctr_cipher(size_t klen) {
    if (klen == 16) return EVP_aes_128_ctr();
    if (klen == 24) return EVP_aes_192_ctr();
    if (klen == 32) return EVP_aes_256_ctr();
    return NULL;
}

/* AES-GCM (key handle 16/24/32 bytes, 12-byte nonce; out = ct||16B tag). */
const char *cx_aes_gcm_encrypt(void *kp, const char *nonce_hex,
                               const char *pt_hex, const char *aad_hex) {
    CxKey *k = kp;
    cxk_live(k, 0);
    size_t nlen = 0, plen = 0, alen = 0;
    unsigned char *nonce = hexdec(nonce_hex, &nlen);
    unsigned char *pt = hexdec(pt_hex, &plen);
    unsigned char *aad = hexdec(aad_hex, &alen);
    const char *res = ERRTAG;
    EVP_CIPHER_CTX *ctx = NULL;
    unsigned char *ct = NULL;
    const EVP_CIPHER *cipher = NULL;
    int len = 0, ctlen = 0;
    if (!k || !nonce || !pt || !aad || nlen != 12) goto done;
    if (plen > (size_t)INT_MAX || alen > (size_t)INT_MAX) goto done;
    cipher = aes_gcm_cipher(k->len);
    if (!cipher) goto done;
    ct = malloc(plen + 16);
    if (!ct) goto done;
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) goto done;
    if (EVP_EncryptInit_ex(ctx, cipher, NULL, NULL, NULL) != 1) goto done;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, 12, NULL) != 1) goto done;
    if (EVP_EncryptInit_ex(ctx, NULL, NULL, k->buf, nonce) != 1) goto done;
    if (alen > 0 && EVP_EncryptUpdate(ctx, NULL, &len, aad, (int)alen) != 1) goto done;
    if (EVP_EncryptUpdate(ctx, ct, &len, pt, (int)plen) != 1) goto done;
    ctlen = len;
    if (EVP_EncryptFinal_ex(ctx, ct + ctlen, &len) != 1) goto done;
    ctlen += len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, 16, ct + ctlen) != 1) goto done;
    ctlen += 16;
    res = out_hex(ct, (size_t)ctlen);
done:
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    if (pt) OPENSSL_cleanse(pt, plen);
    free(nonce); free(pt); free(aad); free(ct);
    return res;
}

const char *cx_aes_gcm_decrypt(void *kp, const char *nonce_hex,
                               const char *ct_hex, const char *aad_hex) {
    CxKey *k = kp;
    cxk_live(k, 0);
    size_t nlen = 0, clen = 0, alen = 0;
    unsigned char *nonce = hexdec(nonce_hex, &nlen);
    unsigned char *ctt = hexdec(ct_hex, &clen);
    unsigned char *aad = hexdec(aad_hex, &alen);
    const char *res = ERRTAG;
    EVP_CIPHER_CTX *ctx = NULL;
    unsigned char *pt = NULL;
    const EVP_CIPHER *cipher = NULL;
    size_t ctlen = 0;
    int len = 0, ptlen = 0;
    if (!k || !nonce || !ctt || !aad || nlen != 12 || clen < 16) goto done;
    if (alen > (size_t)INT_MAX) goto done;
    cipher = aes_gcm_cipher(k->len);
    if (!cipher) goto done;
    ctlen = clen - 16;
    pt = malloc(ctlen ? ctlen : 1);
    if (!pt) goto done;
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) goto done;
    if (EVP_DecryptInit_ex(ctx, cipher, NULL, NULL, NULL) != 1) goto done;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, 12, NULL) != 1) goto done;
    if (EVP_DecryptInit_ex(ctx, NULL, NULL, k->buf, nonce) != 1) goto done;
    if (alen > 0 && EVP_DecryptUpdate(ctx, NULL, &len, aad, (int)alen) != 1) goto done;
    if (EVP_DecryptUpdate(ctx, pt, &len, ctt, (int)ctlen) != 1) goto done;
    ptlen = len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, 16, ctt + ctlen) != 1) goto done;
    if (EVP_DecryptFinal_ex(ctx, pt + ptlen, &len) <= 0) goto done;
    ptlen += len;
    res = out_hex(pt, (size_t)ptlen);
done:
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    if (pt) OPENSSL_cleanse(pt, ctlen);
    free(nonce); free(ctt); free(aad); free(pt);
    return res;
}

static const EVP_CIPHER *aes_cbc_cipher(size_t klen) {
    if (klen == 16) return EVP_aes_128_cbc();
    if (klen == 24) return EVP_aes_192_cbc();
    if (klen == 32) return EVP_aes_256_cbc();
    return NULL;
}

/* AES-CBC, no padding (the input length must be a multiple of 16). */
const char *cx_aes_cbc_encrypt(void *kp, const char *iv_hex, const char *pt_hex) {
    CxKey *k = kp;
    cxk_live(k, 0);
    size_t ivlen = 0, plen = 0;
    unsigned char *iv = hexdec(iv_hex, &ivlen);
    unsigned char *pt = hexdec(pt_hex, &plen);
    const char *res = ERRTAG;
    EVP_CIPHER_CTX *ctx = NULL;
    unsigned char *out = NULL;
    const EVP_CIPHER *cipher = NULL;
    int len = 0, outlen = 0;
    if (!k || !iv || !pt || ivlen != 16 || plen == 0 || plen % 16 != 0) goto done;
    if (plen > (size_t)INT_MAX) goto done;
    cipher = aes_cbc_cipher(k->len);
    if (!cipher) goto done;
    out = malloc(plen);
    if (!out) goto done;
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) goto done;
    if (EVP_EncryptInit_ex(ctx, cipher, NULL, k->buf, iv) != 1) goto done;
    if (EVP_CIPHER_CTX_set_padding(ctx, 0) != 1) goto done;
    if (EVP_EncryptUpdate(ctx, out, &len, pt, (int)plen) != 1) goto done;
    outlen = len;
    if (EVP_EncryptFinal_ex(ctx, out + outlen, &len) != 1) goto done;
    outlen += len;
    res = out_hex(out, (size_t)outlen);
done:
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    if (pt) OPENSSL_cleanse(pt, plen);
    free(iv); free(pt); free(out);
    return res;
}

const char *cx_aes_cbc_decrypt(void *kp, const char *iv_hex, const char *ct_hex) {
    CxKey *k = kp;
    cxk_live(k, 0);
    size_t ivlen = 0, clen = 0;
    unsigned char *iv = hexdec(iv_hex, &ivlen);
    unsigned char *ct = hexdec(ct_hex, &clen);
    const char *res = ERRTAG;
    EVP_CIPHER_CTX *ctx = NULL;
    unsigned char *out = NULL;
    const EVP_CIPHER *cipher = NULL;
    int len = 0, outlen = 0;
    if (!k || !iv || !ct || ivlen != 16 || clen == 0 || clen % 16 != 0) goto done;
    if (clen > (size_t)INT_MAX) goto done;
    cipher = aes_cbc_cipher(k->len);
    if (!cipher) goto done;
    out = malloc(clen);
    if (!out) goto done;
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) goto done;
    if (EVP_DecryptInit_ex(ctx, cipher, NULL, k->buf, iv) != 1) goto done;
    if (EVP_CIPHER_CTX_set_padding(ctx, 0) != 1) goto done;
    if (EVP_DecryptUpdate(ctx, out, &len, ct, (int)clen) != 1) goto done;
    outlen = len;
    if (EVP_DecryptFinal_ex(ctx, out + outlen, &len) != 1) goto done;
    outlen += len;
    res = out_hex(out, (size_t)outlen);
done:
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    if (out) OPENSSL_cleanse(out, clen);
    free(iv); free(ct); free(out);
    return res;
}

/* AES-CTR (key handle 16/24/32 bytes, 16-byte counter block). */
const char *cx_aes_ctr_hex(void *kp, const char *iv_hex, const char *data_hex) {
    CxKey *k = kp;
    cxk_live(k, 0);
    size_t ivlen = 0, dlen = 0;
    unsigned char *iv = hexdec(iv_hex, &ivlen);
    unsigned char *data = hexdec(data_hex, &dlen);
    const char *res = ERRTAG;
    EVP_CIPHER_CTX *ctx = NULL;
    unsigned char *out = NULL;
    const EVP_CIPHER *cipher = NULL;
    int len = 0, outlen = 0;
    if (!k || !iv || !data || ivlen != 16 || dlen > (size_t)INT_MAX) goto done;
    cipher = aes_ctr_cipher(k->len);
    if (!cipher) goto done;
    out = malloc(dlen ? dlen : 1);
    if (!out) goto done;
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) goto done;
    if (EVP_EncryptInit_ex(ctx, cipher, NULL, k->buf, iv) != 1) goto done;
    if (EVP_EncryptUpdate(ctx, out, &len, data, (int)dlen) != 1) goto done;
    outlen = len;
    if (EVP_EncryptFinal_ex(ctx, out + outlen, &len) != 1) goto done;
    outlen += len;
    res = out_hex(out, (size_t)outlen);
done:
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    if (data) OPENSSL_cleanse(data, dlen);
    free(iv); free(data); free(out);
    return res;
}

/* =====================================================================
 * AEAD: ChaCha20-Poly1305 (key handle = 32 bytes, nonce 12 bytes; out = ct||16B tag)
 * ===================================================================== */
const char *cx_aead_encrypt(void *kp, const char *nonce_hex,
                            const char *pt_hex, const char *aad_hex) {
    CxKey *k = kp;
    cxk_live(k, 0);
    size_t nlen = 0, plen = 0, alen = 0;
    unsigned char *nonce = hexdec(nonce_hex, &nlen);
    unsigned char *pt = hexdec(pt_hex, &plen);
    unsigned char *aad = hexdec(aad_hex, &alen);
    const char *res = ERRTAG;
    EVP_CIPHER_CTX *ctx = NULL;
    unsigned char *ct = NULL;
    int len = 0, ctlen = 0;
    if (!k || k->len != 32 || !nonce || !pt || !aad || nlen != 12) goto done;
    ct = malloc(plen + 16);
    if (!ct) goto done;
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) goto done;
    if (EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), NULL, NULL, NULL) != 1) goto done;
    if (EVP_EncryptInit_ex(ctx, NULL, NULL, k->buf, nonce) != 1) goto done;
    if (alen > 0 && EVP_EncryptUpdate(ctx, NULL, &len, aad, (int)alen) != 1) goto done;
    if (EVP_EncryptUpdate(ctx, ct, &len, pt, (int)plen) != 1) goto done;
    ctlen = len;
    if (EVP_EncryptFinal_ex(ctx, ct + ctlen, &len) != 1) goto done;
    ctlen += len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, 16, ct + ctlen) != 1) goto done;
    ctlen += 16;
    res = out_hex(ct, (size_t)ctlen);
done:
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    if (pt) OPENSSL_cleanse(pt, plen);        /* the plaintext, decoded from hex */
    free(nonce); free(pt); free(aad); free(ct);
    return res;
}

const char *cx_aead_decrypt(void *kp, const char *nonce_hex,
                            const char *ct_hex, const char *aad_hex) {
    CxKey *k = kp;
    cxk_live(k, 0);
    size_t nlen = 0, clen = 0, alen = 0;
    unsigned char *nonce = hexdec(nonce_hex, &nlen);
    unsigned char *ctt = hexdec(ct_hex, &clen);
    unsigned char *aad = hexdec(aad_hex, &alen);
    const char *res = ERRTAG;
    EVP_CIPHER_CTX *ctx = NULL;
    unsigned char *pt = NULL;
    size_t ctlen = 0;
    int len = 0, ptlen = 0;
    if (!k || k->len != 32 || !nonce || !ctt || !aad || nlen != 12 || clen < 16) goto done;
    ctlen = clen - 16;
    pt = malloc(ctlen ? ctlen : 1);
    if (!pt) goto done;
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) goto done;
    if (EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), NULL, NULL, NULL) != 1) goto done;
    if (EVP_DecryptInit_ex(ctx, NULL, NULL, k->buf, nonce) != 1) goto done;
    if (alen > 0 && EVP_DecryptUpdate(ctx, NULL, &len, aad, (int)alen) != 1) goto done;
    if (EVP_DecryptUpdate(ctx, pt, &len, ctt, (int)ctlen) != 1) goto done;
    ptlen = len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, 16, ctt + ctlen) != 1) goto done;
    if (EVP_DecryptFinal_ex(ctx, pt + ptlen, &len) <= 0) goto done;   /* auth check */
    ptlen += len;
    res = out_hex(pt, (size_t)ptlen);
done:
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    if (pt) OPENSSL_cleanse(pt, ctlen);       /* the recovered plaintext */
    free(nonce); free(ctt); free(aad); free(pt);
    return res;
}

/* =====================================================================
 * RSA (PKCS#1 v1.5 with SHA-256). The opaque handle is an EVP_PKEY*.
 * ===================================================================== */
static __thread char *g_pem = NULL;
static __thread size_t g_pem_n = 0;

static const char *pem_from_bio(BIO *b) {
    char *data = NULL;
    long n = BIO_get_mem_data(b, &data);
    if (n <= 0 || !data) return ERRTAG;
    if (g_pem && g_pem_n) OPENSSL_cleanse(g_pem, g_pem_n);
    char *p = realloc(g_pem, (size_t)n + 1);
    if (!p) return ERRTAG;
    memcpy(p, data, (size_t)n);
    p[n] = 0;
    g_pem = p;
    g_pem_n = (size_t)n;
    return g_pem;
}

void *cx_rsa_key_from_pem(const char *pem) {
    BIO *b = BIO_new_mem_buf(pem, -1);
    if (!b) return NULL;
    EVP_PKEY *pk = PEM_read_bio_PrivateKey(b, NULL, NULL, NULL);
    BIO_free(b);
    if (!pk || EVP_PKEY_base_id(pk) != EVP_PKEY_RSA) {
        if (pk) EVP_PKEY_free(pk);
        return NULL;
    }
    return pk;
}

void *cx_rsa_pubkey_from_pem(const char *pem) {
    BIO *b = BIO_new_mem_buf(pem, -1);
    if (!b) return NULL;
    EVP_PKEY *pk = PEM_read_bio_PUBKEY(b, NULL, NULL, NULL);
    BIO_free(b);
    if (!pk || EVP_PKEY_base_id(pk) != EVP_PKEY_RSA) {
        if (pk) EVP_PKEY_free(pk);
        return NULL;
    }
    return pk;
}

void *cx_rsa_key_random(tycho_int bits) {
    if (bits < 2048 || bits > 8192) return NULL;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!ctx) return NULL;
    EVP_PKEY *pk = NULL;
    int ok = EVP_PKEY_keygen_init(ctx) > 0 &&
             EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, (int)bits) > 0 &&
             EVP_PKEY_keygen(ctx, &pk) > 0;
    EVP_PKEY_CTX_free(ctx);
    if (!ok) {
        if (pk) EVP_PKEY_free(pk);
        return NULL;
    }
    return pk;
}

void cx_rsa_key_free(void *kp) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    if (pk) EVP_PKEY_free(pk);
}

tycho_int cx_rsa_key_bits(void *kp) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    if (!pk || EVP_PKEY_base_id(pk) != EVP_PKEY_RSA) return -1;
    return (tycho_int)EVP_PKEY_bits(pk);
}

const char *cx_rsa_key_to_pem(void *kp) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    if (!pk || EVP_PKEY_base_id(pk) != EVP_PKEY_RSA) return ERRTAG;
    BIO *b = BIO_new(BIO_s_mem());
    if (!b) return ERRTAG;
    const char *res = ERRTAG;
    if (PEM_write_bio_PrivateKey(b, pk, NULL, NULL, 0, NULL, NULL) == 1) res = pem_from_bio(b);
    BIO_free(b);
    return res;
}

const char *cx_rsa_pubkey_to_pem(void *kp) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    if (!pk || EVP_PKEY_base_id(pk) != EVP_PKEY_RSA) return ERRTAG;
    BIO *b = BIO_new(BIO_s_mem());
    if (!b) return ERRTAG;
    const char *res = ERRTAG;
    if (PEM_write_bio_PUBKEY(b, pk) == 1) res = pem_from_bio(b);
    BIO_free(b);
    return res;
}

const char *cx_rsa_sign_sha256(void *kp, const char *msg_hex) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    size_t mlen = 0;
    unsigned char *msg = hexdec(msg_hex, &mlen);
    const char *res = ERRTAG;
    EVP_MD_CTX *md = NULL;
    unsigned char *sig = NULL;
    size_t siglen = 0;
    if (!pk || EVP_PKEY_base_id(pk) != EVP_PKEY_RSA || !msg) goto done;
    md = EVP_MD_CTX_new();
    if (!md) goto done;
    if (EVP_DigestSignInit(md, NULL, EVP_sha256(), NULL, pk) != 1) goto done;
    if (EVP_DigestSignUpdate(md, msg, mlen) != 1) goto done;
    if (EVP_DigestSignFinal(md, NULL, &siglen) != 1) goto done;
    sig = malloc(siglen ? siglen : 1);
    if (!sig) goto done;
    if (EVP_DigestSignFinal(md, sig, &siglen) != 1) goto done;
    res = out_hex(sig, siglen);
done:
    if (md) EVP_MD_CTX_free(md);
    free(msg);
    free(sig);
    return res;
}

tycho_int cx_rsa_verify_sha256(void *kp, const char *msg_hex, const char *sig_hex) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    size_t mlen = 0, slen = 0;
    unsigned char *msg = hexdec(msg_hex, &mlen);
    unsigned char *sig = hexdec(sig_hex, &slen);
    tycho_int res = 0;
    EVP_MD_CTX *md = NULL;
    if (!pk || EVP_PKEY_base_id(pk) != EVP_PKEY_RSA || !msg || !sig) goto done;
    md = EVP_MD_CTX_new();
    if (!md) goto done;
    if (EVP_DigestVerifyInit(md, NULL, EVP_sha256(), NULL, pk) != 1) goto done;
    if (EVP_DigestVerifyUpdate(md, msg, mlen) != 1) goto done;
    res = EVP_DigestVerifyFinal(md, sig, slen) == 1 ? 1 : 0;
done:
    if (md) EVP_MD_CTX_free(md);
    free(msg);
    free(sig);
    return res;
}

void *cx_ed25519_key_random(void)               { return cx_key_random(32); }
void *cx_ed25519_key_from_seed(const char *seed_hex) {
    void *k = cx_key_from_hex(seed_hex);
    if (k && ((CxKey *)k)->len != 32) { cx_key_free(k); return NULL; }
    return k;
}

const char *cx_ed25519_pubkey_hex(void *kp) {
    CxKey *k = kp;
    cxk_live(k, 0);
    const char *res = ERRTAG;
    EVP_PKEY *pk = NULL;
    unsigned char pub[32];
    size_t publen = sizeof pub;
    if (!k || k->len != 32) return ERRTAG;
    pk = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, k->buf, 32);
    if (!pk) return ERRTAG;
    if (EVP_PKEY_get_raw_public_key(pk, pub, &publen) == 1) res = out_hex(pub, publen);
    EVP_PKEY_free(pk);
    return res;
}

const char *cx_ed25519_sign_hex(void *kp, const char *msg_hex) {
    CxKey *k = kp;
    cxk_live(k, 0);
    size_t ml = 0;
    unsigned char *msg = hexdec(msg_hex, &ml);
    const char *res = ERRTAG;
    EVP_PKEY *pk = NULL;
    EVP_MD_CTX *md = NULL;
    unsigned char sig[64];
    size_t siglen = sizeof sig;
    if (!k || k->len != 32 || !msg) goto done;
    pk = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, k->buf, 32);
    if (!pk) goto done;
    md = EVP_MD_CTX_new();
    if (!md) goto done;
    if (EVP_DigestSignInit(md, NULL, NULL, NULL, pk) != 1) goto done;
    if (EVP_DigestSign(md, sig, &siglen, msg, ml) != 1) goto done;
    res = out_hex(sig, siglen);
done:
    if (md) EVP_MD_CTX_free(md);
    if (pk) EVP_PKEY_free(pk);
    free(msg);
    return res;
}

tycho_int cx_ed25519_verify(const char *pub_hex, const char *msg_hex, const char *sig_hex) {
    size_t pl = 0, ml = 0, gl = 0;
    unsigned char *pub = hexdec(pub_hex, &pl);
    unsigned char *msg = hexdec(msg_hex, &ml);
    unsigned char *sig = hexdec(sig_hex, &gl);
    tycho_int ok = 0;
    EVP_PKEY *pk = NULL;
    EVP_MD_CTX *md = NULL;
    if (!pub || !msg || !sig || pl != 32 || gl != 64) goto done;
    pk = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, pub, 32);
    if (!pk) goto done;
    md = EVP_MD_CTX_new();
    if (!md) goto done;
    if (EVP_DigestVerifyInit(md, NULL, NULL, NULL, pk) != 1) goto done;
    ok = (EVP_DigestVerify(md, sig, gl, msg, ml) == 1) ? 1 : 0;
done:
    if (md) EVP_MD_CTX_free(md);
    if (pk) EVP_PKEY_free(pk);
    free(pub); free(msg); free(sig);
    return ok;
}

/* =====================================================================
 * X25519 key exchange (secret = key handle; public = hex; shared secret = KEY handle)
 * ===================================================================== */
void *cx_x25519_key_random(void)                  { return cx_key_random(32); }
void *cx_x25519_key_from_secret(const char *sec_hex) {
    void *k = cx_key_from_hex(sec_hex);
    if (k && ((CxKey *)k)->len != 32) { cx_key_free(k); return NULL; }
    return k;
}

const char *cx_x25519_pubkey_hex(void *kp) {
    CxKey *k = kp;
    cxk_live(k, 0);
    const char *res = ERRTAG;
    EVP_PKEY *pk = NULL;
    unsigned char pub[32];
    size_t publen = sizeof pub;
    if (!k || k->len != 32) return ERRTAG;
    pk = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, k->buf, 32);
    if (!pk) return ERRTAG;
    if (EVP_PKEY_get_raw_public_key(pk, pub, &publen) == 1) res = out_hex(pub, publen);
    EVP_PKEY_free(pk);
    return res;
}

/* combine my secret (handle) with their public key (hex) -> shared-secret KEY handle */
void *cx_x25519_shared(void *kp, const char *their_pub_hex) {
    CxKey *k = kp;
    cxk_live(k, 0);
    size_t pl = 0;
    unsigned char *tp = hexdec(their_pub_hex, &pl);
    EVP_PKEY *me = NULL, *peer = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    unsigned char shared[32];
    size_t shlen = sizeof shared;
    CxKey *out = NULL;
    if (!k || k->len != 32 || !tp || pl != 32) goto done;
    me = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, k->buf, 32);
    if (!me) goto done;
    peer = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL, tp, 32);
    if (!peer) goto done;
    ctx = EVP_PKEY_CTX_new(me, NULL);
    if (!ctx) goto done;
    if (EVP_PKEY_derive_init(ctx) != 1) goto done;
    if (EVP_PKEY_derive_set_peer(ctx, peer) != 1) goto done;
    if (EVP_PKEY_derive(ctx, shared, &shlen) != 1) goto done;
    out = key_new(shlen);
    if (out) memcpy(out->buf, shared, shlen);
done:
    OPENSSL_cleanse(shared, sizeof shared);
    if (ctx) EVP_PKEY_CTX_free(ctx);
    if (peer) EVP_PKEY_free(peer);
    if (me) EVP_PKEY_free(me);
    free(tp);
    return out;
}

/* =====================================================================
 * ECDSA P-256 (SHA-256, DER signatures). Handle is an EVP_PKEY*.
 * ===================================================================== */
void *cx_ecdsa_key_from_pem(const char *pem) {
    BIO *b = BIO_new_mem_buf(pem, -1);
    if (!b) return NULL;
    EVP_PKEY *pk = PEM_read_bio_PrivateKey(b, NULL, NULL, NULL);
    BIO_free(b);
    if (!pk || EVP_PKEY_base_id(pk) != EVP_PKEY_EC) {
        if (pk) EVP_PKEY_free(pk);
        return NULL;
    }
    return pk;
}

void *cx_ecdsa_pubkey_from_pem(const char *pem) {
    BIO *b = BIO_new_mem_buf(pem, -1);
    if (!b) return NULL;
    EVP_PKEY *pk = PEM_read_bio_PUBKEY(b, NULL, NULL, NULL);
    BIO_free(b);
    if (!pk || EVP_PKEY_base_id(pk) != EVP_PKEY_EC) {
        if (pk) EVP_PKEY_free(pk);
        return NULL;
    }
    return pk;
}

void *cx_ecdsa_key_random(void) {
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (!ctx) return NULL;
    EVP_PKEY *pk = NULL;
    int ok = EVP_PKEY_keygen_init(ctx) > 0 &&
             EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) > 0 &&
             EVP_PKEY_keygen(ctx, &pk) > 0;
    EVP_PKEY_CTX_free(ctx);
    if (!ok) {
        if (pk) EVP_PKEY_free(pk);
        return NULL;
    }
    return pk;
}

void cx_ecdsa_key_free(void *kp) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    if (pk) EVP_PKEY_free(pk);
}

tycho_int cx_ecdsa_key_bits(void *kp) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    if (!pk || EVP_PKEY_base_id(pk) != EVP_PKEY_EC) return -1;
    return (tycho_int)EVP_PKEY_bits(pk);
}

const char *cx_ecdsa_key_to_pem(void *kp) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    if (!pk || EVP_PKEY_base_id(pk) != EVP_PKEY_EC) return ERRTAG;
    BIO *b = BIO_new(BIO_s_mem());
    if (!b) return ERRTAG;
    const char *res = ERRTAG;
    if (PEM_write_bio_PrivateKey(b, pk, NULL, NULL, 0, NULL, NULL) == 1) res = pem_from_bio(b);
    BIO_free(b);
    return res;
}

const char *cx_ecdsa_pubkey_to_pem(void *kp) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    if (!pk || EVP_PKEY_base_id(pk) != EVP_PKEY_EC) return ERRTAG;
    BIO *b = BIO_new(BIO_s_mem());
    if (!b) return ERRTAG;
    const char *res = ERRTAG;
    if (PEM_write_bio_PUBKEY(b, pk) == 1) res = pem_from_bio(b);
    BIO_free(b);
    return res;
}

const char *cx_ecdsa_sign_sha256(void *kp, const char *msg_hex) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    size_t mlen = 0;
    unsigned char *msg = hexdec(msg_hex, &mlen);
    const char *res = ERRTAG;
    EVP_MD_CTX *md = NULL;
    unsigned char *sig = NULL;
    size_t siglen = 0;
    if (!pk || EVP_PKEY_base_id(pk) != EVP_PKEY_EC || !msg) goto done;
    md = EVP_MD_CTX_new();
    if (!md) goto done;
    if (EVP_DigestSignInit(md, NULL, EVP_sha256(), NULL, pk) != 1) goto done;
    if (EVP_DigestSignUpdate(md, msg, mlen) != 1) goto done;
    if (EVP_DigestSignFinal(md, NULL, &siglen) != 1) goto done;
    sig = malloc(siglen ? siglen : 1);
    if (!sig) goto done;
    if (EVP_DigestSignFinal(md, sig, &siglen) != 1) goto done;
    res = out_hex(sig, siglen);
done:
    if (md) EVP_MD_CTX_free(md);
    free(msg);
    free(sig);
    return res;
}

tycho_int cx_ecdsa_verify_sha256(void *kp, const char *msg_hex, const char *sig_hex) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    size_t mlen = 0, slen = 0;
    unsigned char *msg = hexdec(msg_hex, &mlen);
    unsigned char *sig = hexdec(sig_hex, &slen);
    tycho_int res = 0;
    EVP_MD_CTX *md = NULL;
    if (!pk || EVP_PKEY_base_id(pk) != EVP_PKEY_EC || !msg || !sig) goto done;
    md = EVP_MD_CTX_new();
    if (!md) goto done;
    if (EVP_DigestVerifyInit(md, NULL, EVP_sha256(), NULL, pk) != 1) goto done;
    if (EVP_DigestVerifyUpdate(md, msg, mlen) != 1) goto done;
    res = EVP_DigestVerifyFinal(md, sig, slen) == 1 ? 1 : 0;
done:
    if (md) EVP_MD_CTX_free(md);
    free(msg);
    free(sig);
    return res;
}

/* =====================================================================
 * ML-DSA (post-quantum signatures), OpenSSL provider.
 * ===================================================================== */
static const char *mldsa_name(tycho_int level) {
    if (level == 44) return "ML-DSA-44";
    if (level == 65) return "ML-DSA-65";
    if (level == 87) return "ML-DSA-87";
    return NULL;
}

void *cx_mldsa_key_random(tycho_int level) {
    const char *name = mldsa_name(level);
    if (!name) return NULL;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, name, NULL);
    if (!ctx) return NULL;
    EVP_PKEY *pk = NULL;
    int ok = EVP_PKEY_keygen_init(ctx) > 0 && EVP_PKEY_keygen(ctx, &pk) > 0;
    EVP_PKEY_CTX_free(ctx);
    if (!ok) {
        if (pk) EVP_PKEY_free(pk);
        return NULL;
    }
    return pk;
}

void *cx_mldsa_key_from_pem(const char *pem) {
    BIO *b = BIO_new_mem_buf(pem, -1);
    if (!b) return NULL;
    EVP_PKEY *pk = PEM_read_bio_PrivateKey(b, NULL, NULL, NULL);
    BIO_free(b);
    return pk;
}

void *cx_mldsa_pubkey_from_pem(const char *pem) {
    BIO *b = BIO_new_mem_buf(pem, -1);
    if (!b) return NULL;
    EVP_PKEY *pk = PEM_read_bio_PUBKEY(b, NULL, NULL, NULL);
    BIO_free(b);
    return pk;
}

void cx_mldsa_key_free(void *kp) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    if (pk) EVP_PKEY_free(pk);
}

const char *cx_mldsa_key_to_pem(void *kp) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    if (!pk) return ERRTAG;
    BIO *b = BIO_new(BIO_s_mem());
    if (!b) return ERRTAG;
    const char *res = ERRTAG;
    if (PEM_write_bio_PrivateKey(b, pk, NULL, NULL, 0, NULL, NULL) == 1) res = pem_from_bio(b);
    BIO_free(b);
    return res;
}

const char *cx_mldsa_pubkey_to_pem(void *kp) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    if (!pk) return ERRTAG;
    BIO *b = BIO_new(BIO_s_mem());
    if (!b) return ERRTAG;
    const char *res = ERRTAG;
    if (PEM_write_bio_PUBKEY(b, pk) == 1) res = pem_from_bio(b);
    BIO_free(b);
    return res;
}

const char *cx_mldsa_sign(void *kp, const char *msg_hex) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    size_t ml = 0;
    unsigned char *msg = hexdec(msg_hex, &ml);
    const char *res = ERRTAG;
    EVP_MD_CTX *md = NULL;
    unsigned char *sig = NULL;
    size_t siglen = 0;
    if (!pk || !msg) goto done;
    md = EVP_MD_CTX_new();
    if (!md) goto done;
    if (EVP_DigestSignInit(md, NULL, NULL, NULL, pk) != 1) goto done;
    if (EVP_DigestSign(md, NULL, &siglen, msg, ml) != 1) goto done;
    sig = malloc(siglen ? siglen : 1);
    if (!sig) goto done;
    if (EVP_DigestSign(md, sig, &siglen, msg, ml) != 1) goto done;
    res = out_hex(sig, siglen);
done:
    if (md) EVP_MD_CTX_free(md);
    free(msg);
    free(sig);
    return res;
}

tycho_int cx_mldsa_verify(void *kp, const char *msg_hex, const char *sig_hex) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    size_t ml = 0, sl = 0;
    unsigned char *msg = hexdec(msg_hex, &ml);
    unsigned char *sig = hexdec(sig_hex, &sl);
    tycho_int res = 0;
    EVP_MD_CTX *md = NULL;
    if (!pk || !msg || !sig) goto done;
    md = EVP_MD_CTX_new();
    if (!md) goto done;
    if (EVP_DigestVerifyInit(md, NULL, NULL, NULL, pk) != 1) goto done;
    res = EVP_DigestVerify(md, sig, sl, msg, ml) == 1 ? 1 : 0;
done:
    if (md) EVP_MD_CTX_free(md);
    free(msg);
    free(sig);
    return res;
}

/* =====================================================================
 * ML-KEM (post-quantum KEM), OpenSSL provider.
 * ===================================================================== */
static const char *mlkem_name(tycho_int level) {
    if (level == 512) return "ML-KEM-512";
    if (level == 768) return "ML-KEM-768";
    if (level == 1024) return "ML-KEM-1024";
    return NULL;
}

void *cx_mlkem_key_random(tycho_int level) {
    const char *name = mlkem_name(level);
    if (!name) return NULL;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, name, NULL);
    if (!ctx) return NULL;
    EVP_PKEY *pk = NULL;
    int ok = EVP_PKEY_keygen_init(ctx) > 0 && EVP_PKEY_keygen(ctx, &pk) > 0;
    EVP_PKEY_CTX_free(ctx);
    if (!ok) { if (pk) EVP_PKEY_free(pk); return NULL; }
    return pk;
}

void *cx_mlkem_key_from_pem(const char *pem) {
    BIO *b = BIO_new_mem_buf(pem, -1);
    if (!b) return NULL;
    EVP_PKEY *pk = PEM_read_bio_PrivateKey(b, NULL, NULL, NULL);
    BIO_free(b);
    return pk;
}

void *cx_mlkem_pubkey_from_pem(const char *pem) {
    BIO *b = BIO_new_mem_buf(pem, -1);
    if (!b) return NULL;
    EVP_PKEY *pk = PEM_read_bio_PUBKEY(b, NULL, NULL, NULL);
    BIO_free(b);
    return pk;
}

void cx_mlkem_key_free(void *kp) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    if (pk) EVP_PKEY_free(pk);
}

const char *cx_mlkem_key_to_pem(void *kp) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    if (!pk) return ERRTAG;
    BIO *b = BIO_new(BIO_s_mem());
    if (!b) return ERRTAG;
    const char *res = ERRTAG;
    if (PEM_write_bio_PrivateKey(b, pk, NULL, NULL, 0, NULL, NULL) == 1) res = pem_from_bio(b);
    BIO_free(b);
    return res;
}

const char *cx_mlkem_pubkey_to_pem(void *kp) {
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    if (!pk) return ERRTAG;
    BIO *b = BIO_new(BIO_s_mem());
    if (!b) return ERRTAG;
    const char *res = ERRTAG;
    if (PEM_write_bio_PUBKEY(b, pk) == 1) res = pem_from_bio(b);
    BIO_free(b);
    return res;
}

typedef struct { unsigned char *ct; size_t ctlen; unsigned char *ss; size_t sslen; } KemResult;

void *cx_mlkem_encapsulate(void *pub, tycho_int *status) {
    *status = 0;
    EVP_PKEY *pk = (EVP_PKEY *)pub;
    if (!pk) return NULL;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pk, NULL);
    if (!ctx) return NULL;
    KemResult *r = NULL;
    size_t ctlen = 0, sslen = 0;
    if (EVP_PKEY_encapsulate_init(ctx, NULL) <= 0) goto done;
    if (EVP_PKEY_encapsulate(ctx, NULL, &ctlen, NULL, &sslen) <= 0) goto done;
    r = (KemResult *)calloc(1, sizeof *r);
    if (!r) goto done;
    r->ct = (unsigned char *)malloc(ctlen ? ctlen : 1);
    r->ss = (unsigned char *)malloc(sslen ? sslen : 1);
    if (!r->ct || !r->ss) { free(r->ct); free(r->ss); free(r); r = NULL; goto done; }
    if (EVP_PKEY_encapsulate(ctx, r->ct, &ctlen, r->ss, &sslen) <= 0) {
        free(r->ct); free(r->ss); free(r); r = NULL; goto done;
    }
    r->ctlen = ctlen; r->sslen = sslen;
done:
    EVP_PKEY_CTX_free(ctx);
    return r;
}

void cx_mlkem_result_free(void *vp) {
    KemResult *r = (KemResult *)vp;
    if (!r) return;
    if (r->ss) OPENSSL_cleanse(r->ss, r->sslen);
    free(r->ct); free(r->ss); free(r);
}

void cx_mlkem_ct(void *vp, unsigned char **out, tycho_int *outlen) {
    KemResult *r = (KemResult *)vp;
    *out = NULL; *outlen = 0;
    if (!r) return;
    unsigned char *p = (unsigned char *)malloc(r->ctlen ? r->ctlen : 1);
    if (!p) return;
    memcpy(p, r->ct, r->ctlen);
    *out = p; *outlen = (tycho_int)r->ctlen;
}

void cx_mlkem_ss(void *vp, unsigned char **out, tycho_int *outlen) {
    KemResult *r = (KemResult *)vp;
    *out = NULL; *outlen = 0;
    if (!r) return;
    unsigned char *p = (unsigned char *)malloc(r->sslen ? r->sslen : 1);
    if (!p) return;
    memcpy(p, r->ss, r->sslen);
    *out = p; *outlen = (tycho_int)r->sslen;
}

void cx_mlkem_decapsulate(void *kp, const unsigned char *ct, tycho_int ctlen,
                          tycho_int *status, unsigned char **out, tycho_int *outlen) {
    *status = 0; *out = NULL; *outlen = 0;
    EVP_PKEY *pk = (EVP_PKEY *)kp;
    if (!pk || !ct || ctlen <= 0) { *status = 1; return; }
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pk, NULL);
    if (!ctx) { *status = 1; return; }
    size_t sslen = 0;
    if (EVP_PKEY_decapsulate_init(ctx, NULL) <= 0 ||
        EVP_PKEY_decapsulate(ctx, NULL, &sslen, ct, (size_t)ctlen) <= 0) {
        EVP_PKEY_CTX_free(ctx); *status = 1; return;
    }
    unsigned char *ss = (unsigned char *)malloc(sslen ? sslen : 1);
    if (!ss) { EVP_PKEY_CTX_free(ctx); *status = 1; return; }
    if (EVP_PKEY_decapsulate(ctx, ss, &sslen, ct, (size_t)ctlen) <= 0) {
        free(ss); EVP_PKEY_CTX_free(ctx); *status = 1; return;
    }
    EVP_PKEY_CTX_free(ctx);
    *out = ss; *outlen = (tycho_int)sslen;
}
