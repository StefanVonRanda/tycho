/* core:crypto FFI shim -> OpenSSL libcrypto (EVP).
 *
 * Two kinds of value cross the boundary:
 *   - SECRET key material is an opaque handle (`ptr` in Tycho, CxKey* here).
 *     The bytes live in C, are never materialized into a Tycho string unless
 *     you explicitly call key_export_hex, and are wiped with OPENSSL_cleanse
 *     when you key_free them. This keeps secrets out of Tycho's arena, where
 *     value semantics would copy them around and never zero them.
 *   - PUBLIC / ephemeral data (nonces, salts, ciphertext, signatures, public
 *     keys, MAC tags, message bytes, digests) crosses as a lowercase hex
 *     string, because a Tycho string cannot hold an interior 0x00.
 *
 * Ownership: every cx_*_key* / derive that returns a handle gives the caller a
 * NEW handle it must cx_key_free exactly once. Tycho copies the ptr by value,
 * so do not free the same handle twice.
 *
 * Returned hex strings use a per-thread scratch buffer recycled on the next
 * cx_* call; Tycho arena-copies an extern's returned string at the call site
 * (docs/guides/ffi.md), so recycling is safe. Failure sentinel for string returns is
 * "!err" (never valid hex); handle returns use NULL (Tycho: is_null).
 *
 * Build: tychoc auto-discovers this file as the core:crypto shim and auto-links
 * corelib/crypto/deps (libcrypto). No CLI flags.
 */
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/crypto.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
/* int64-migration (Phase 3): Tycho `int` lowers to tycho_int (int64_t) in the
 * emitted program; this shim is a separate translation unit, so it defines the
 * same type to match the FFI ABI on ILP32/LLP64, not just LP64. */
#ifndef TYCHO_INT_T
#define TYCHO_INT_T
typedef int64_t tycho_int;
#endif

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

/* ---- strict, fail-closed, BRANCH-FREE hex decode ----
   This is on the key-import path (cx_key_from_hex and the two key_from_* that
   wrap it), so neither the digit values nor the position of a bad digit may
   steer control flow: the original rejected at the first bad digit, which timed
   the offset of the error. Verified with valgrind rather than a stopwatch --
   scripts/crypto_hygiene.sh marks the input UNDEFINED and memcheck reports any
   branch derived from it. The old version scored 7; this scores 0. */
static unsigned ct_lt(unsigned a, unsigned b) { return ((a - b) >> 8) & 1u; }  /* a,b < 256 */

/* 0..15 in *v, 1 if c was a hex digit -- no branch on c. The &0xFF matters: on a
   non-digit the subtraction wraps, and without the mask the >>8 test says yes. */
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
typedef struct { unsigned char *buf; size_t len; } CxKey;

void cx_key_free(void *kp);   /* fwd: used by constructors on the error path */

static CxKey *key_new(size_t n) {
    CxKey *k = malloc(sizeof *k);
    if (!k) return NULL;
    k->buf = malloc(n ? n : 1);
    if (!k->buf) { free(k); return NULL; }
    k->len = n;
    return k;
}

void cx_key_free(void *kp) {
    CxKey *k = kp;
    if (!k) return;
    if (k->buf) { OPENSSL_cleanse(k->buf, k->len); free(k->buf); }
    free(k);
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
    if (!k) return ERRTAG;
    return out_hex(k->buf, k->len);
}

tycho_int cx_key_len(void *kp) { CxKey *k = kp; return k ? (tycho_int)k->len : -1; }

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
const char *cx_sha512_hex(const char *msg_hex) { return digest_hex(EVP_sha512(), msg_hex); }

/* =====================================================================
 * HMAC-SHA256(key handle, msg_hex) -> hex MAC
 * ===================================================================== */
const char *cx_hmac_sha256_hex(void *kp, const char *msg_hex) {
    CxKey *k = kp;
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

/* =====================================================================
 * PBKDF2-HMAC-SHA256(password text, salt_hex, iters, dklen) -> derived KEY handle
 * ===================================================================== */
/* The password LENGTH is passed in, never strlen'd. A Tycho string is
 * length-carrying and may hold an interior NUL; strlen stopped there, so
 * "secret\0A" and "secret\0B" derived the SAME key as "secret" -- two distinct
 * credentials collapsing into one, silently (measured 2026-08-15). This is the
 * same class core:net's has_nul refuses at getaddrinfo and core:sqlite got wrong
 * at bind_text; a key derivation is where it costs most. */
void *cx_pbkdf2_sha256(const char *password, tycho_int pwlen, const char *salt_hex, tycho_int iters, tycho_int dklen) {
    if (iters < 1 || dklen < 1 || dklen > 1024) return NULL;
    if (pwlen < 0 || pwlen > 1048576) return NULL;   /* fail closed, never strlen */
    size_t slen;
    unsigned char *salt = hexdec(salt_hex, &slen);
    if (!salt) return NULL;
    CxKey *k = key_new((size_t)dklen);
    if (k && PKCS5_PBKDF2_HMAC(password, (int)pwlen, salt, (int)slen,
                               (int)iters, EVP_sha256(), (int)dklen, k->buf) != 1) {
        cx_key_free(k);
        k = NULL;
    }
    free(salt);
    return k;
}

/* =====================================================================
 * Constant-time equality of two hex byte strings (e.g. MAC verify) -> 1 / 0
 * ===================================================================== */
/* Lengths passed in, so an interior NUL cannot silently shorten either side.
 * Hex never legitimately contains one, and hexdec finds its own end with strlen:
 * two DIFFERENT hex strings truncating to the same prefix compared EQUAL
 * (measured 2026-08-15). Not reachable in the MAC shape -- the trusted side is
 * library-generated and carries no NUL, so the lengths disagree and the answer is
 * already false -- but a caller comparing two SUPPLIED values had a collision, and
 * this is the one comparison in the package that must not surprise anyone. */
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

/* =====================================================================
 * AEAD: ChaCha20-Poly1305 (key handle = 32 bytes, nonce 12 bytes; out = ct||16B tag)
 * ===================================================================== */
const char *cx_aead_encrypt(void *kp, const char *nonce_hex,
                            const char *pt_hex, const char *aad_hex) {
    CxKey *k = kp;
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
 * Ed25519 signatures (private seed = key handle; public key/sig = hex)
 * ===================================================================== */
void *cx_ed25519_key_random(void)               { return cx_key_random(32); }
void *cx_ed25519_key_from_seed(const char *seed_hex) {
    void *k = cx_key_from_hex(seed_hex);
    if (k && ((CxKey *)k)->len != 32) { cx_key_free(k); return NULL; }
    return k;
}

const char *cx_ed25519_pubkey_hex(void *kp) {
    CxKey *k = kp;
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
