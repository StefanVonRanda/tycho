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
 * (docs/ffi.md), so recycling is safe. Failure sentinel for string returns is
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

static const char *const ERRTAG = "!err";     /* non-hex failure sentinel for string returns */

/* ---- recycled hex return buffer (one per thread) ---- */
static __thread char *g_out = NULL;
static const char *out_hex(const unsigned char *buf, size_t n) {
    static const char H[] = "0123456789abcdef";
    char *p = realloc(g_out, 2 * n + 1);
    if (!p) return ERRTAG;
    g_out = p;
    for (size_t i = 0; i < n; i++) {
        g_out[2 * i]     = H[(buf[i] >> 4) & 0xf];
        g_out[2 * i + 1] = H[buf[i] & 0xf];
    }
    g_out[2 * n] = '\0';
    return g_out;
}

/* ---- strict, fail-closed hex decode ---- */
static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static unsigned char *hexdec(const char *s, size_t *outlen) {
    size_t L = strlen(s);
    if (L & 1u) return NULL;
    size_t n = L / 2;
    unsigned char *b = malloc(n ? n : 1);
    if (!b) return NULL;
    for (size_t i = 0; i < n; i++) {
        int hi = hexval((unsigned char)s[2 * i]);
        int lo = hexval((unsigned char)s[2 * i + 1]);
        if (hi < 0 || lo < 0) { free(b); return NULL; }
        b[i] = (unsigned char)((hi << 4) | lo);
    }
    *outlen = n;
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
void *cx_key_random(long n) {
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

long cx_key_len(void *kp) { CxKey *k = kp; return k ? (long)k->len : -1; }

/* =====================================================================
 * CSPRNG (public bytes: nonces, salts) -> hex
 * ===================================================================== */
const char *cx_random_hex(long n) {
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
void *cx_pbkdf2_sha256(const char *password, const char *salt_hex, long iters, long dklen) {
    if (iters < 1 || dklen < 1 || dklen > 1024) return NULL;
    size_t slen;
    unsigned char *salt = hexdec(salt_hex, &slen);
    if (!salt) return NULL;
    CxKey *k = key_new((size_t)dklen);
    if (k && PKCS5_PBKDF2_HMAC(password, (int)strlen(password), salt, (int)slen,
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
long cx_ct_equal(const char *a_hex, const char *b_hex) {
    size_t an, bn;
    unsigned char *a = hexdec(a_hex, &an);
    if (!a) return 0;
    unsigned char *b = hexdec(b_hex, &bn);
    if (!b) { free(a); return 0; }
    long eq = (an == bn && CRYPTO_memcmp(a, b, an) == 0) ? 1 : 0;
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

long cx_ed25519_verify(const char *pub_hex, const char *msg_hex, const char *sig_hex) {
    size_t pl = 0, ml = 0, gl = 0;
    unsigned char *pub = hexdec(pub_hex, &pl);
    unsigned char *msg = hexdec(msg_hex, &ml);
    unsigned char *sig = hexdec(sig_hex, &gl);
    long ok = 0;
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
