#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE          /* timegm under -std=c11 */
#endif
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "../tycho.h"

static __thread char *g_x5 = NULL;
static __thread size_t g_x5_cap = 0;

static const char *bio_text(BIO *b) {
    char *data = NULL;
    long n = BIO_get_mem_data(b, &data);
    if (n <= 0 || !data) return "";
    if (g_x5_cap < (size_t)n + 1) {
        char *p = realloc(g_x5, (size_t)n + 1);
        if (!p) return "";
        g_x5 = p;
        g_x5_cap = (size_t)n + 1;
    }
    memcpy(g_x5, data, (size_t)n);
    g_x5[n] = 0;
    return g_x5;
}

static X509 *parse_pem_x509(const char *pem) {
    BIO *b = BIO_new_mem_buf(pem, -1);
    if (!b) return NULL;
    X509 *x = PEM_read_bio_X509(b, NULL, NULL, NULL);
    BIO_free(b);
    return x;
}

void *x5x_parse_pem(const char *pem) {
    return parse_pem_x509(pem);
}

void *x5x_parse_der(const unsigned char *der, tycho_int len) {
    if (!der || len <= 0 || len > 1048576) return NULL;
    const unsigned char *p = der;
    return d2i_X509(NULL, &p, (long)len);
}

void x5x_free(void *vp) {
    X509 *x = (X509 *)vp;
    if (x) X509_free(x);
}

const char *x5x_subject(void *vp) {
    X509 *x = (X509 *)vp;
    if (!x) return "";
    BIO *b = BIO_new(BIO_s_mem());
    if (!b) return "";
    X509_NAME_print_ex(b, X509_get_subject_name(x), 0, XN_FLAG_RFC2253);
    const char *r = bio_text(b);
    BIO_free(b);
    return r;
}

const char *x5x_issuer(void *vp) {
    X509 *x = (X509 *)vp;
    if (!x) return "";
    BIO *b = BIO_new(BIO_s_mem());
    if (!b) return "";
    X509_NAME_print_ex(b, X509_get_issuer_name(x), 0, XN_FLAG_RFC2253);
    const char *r = bio_text(b);
    BIO_free(b);
    return r;
}

const char *x5x_serial(void *vp) {
    X509 *x = (X509 *)vp;
    if (!x) return "";
    BIO *b = BIO_new(BIO_s_mem());
    if (!b) return "";
    i2a_ASN1_INTEGER(b, X509_get_serialNumber(x));
    const char *r = bio_text(b);
    BIO_free(b);
    return r;
}

static tycho_int asn1_time_unix(const ASN1_TIME *t) {
    if (!t) return -1;
    struct tm tm;
    if (ASN1_TIME_to_tm(t, &tm) != 1) return -1;
#ifdef _WIN32
    __time64_t v = _mkgmtime64(&tm);
    return v < 0 ? -1 : (tycho_int)v;
#else
    time_t v = timegm(&tm);
    return v < 0 ? -1 : (tycho_int)v;
#endif
}

tycho_int x5x_not_before(void *vp) {
    X509 *x = (X509 *)vp;
    if (!x) return -1;
    return asn1_time_unix(X509_get0_notBefore(x));
}

tycho_int x5x_not_after(void *vp) {
    X509 *x = (X509 *)vp;
    if (!x) return -1;
    return asn1_time_unix(X509_get0_notAfter(x));
}

tycho_int x5x_check_host(void *vp, const char *host) {
    X509 *x = (X509 *)vp;
    if (!x || !host) return 0;
    return X509_check_host(x, host, strlen(host), 0, NULL) == 1 ? 1 : 0;
}

tycho_int x5x_verify(void *vp, const char *ca_pem, const char *host) {
    X509 *leaf = (X509 *)vp;
    X509 *ca = parse_pem_x509(ca_pem);
    X509_STORE *store = NULL;
    X509_STORE_CTX *ctx = NULL;
    tycho_int ok = 0;
    if (!leaf || !ca) goto done;
    store = X509_STORE_new();
    if (!store || X509_STORE_add_cert(store, ca) != 1) goto done;
    ctx = X509_STORE_CTX_new();
    if (!ctx || X509_STORE_CTX_init(ctx, store, leaf, NULL) != 1) goto done;
    if (host && host[0]) {
        X509_VERIFY_PARAM *param = X509_STORE_CTX_get0_param(ctx);
        if (!param || X509_VERIFY_PARAM_set1_host(param, host, 0) != 1) goto done;
    }
    ok = X509_verify_cert(ctx) == 1 ? 1 : 0;
done:
    if (ctx) X509_STORE_CTX_free(ctx);
    if (store) X509_STORE_free(store);
    if (ca) X509_free(ca);
    return ok;
}
