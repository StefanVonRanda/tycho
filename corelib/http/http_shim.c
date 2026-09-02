#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "../tycho.h"

/* `status` stays a genuine C `long`: curl_easy_getinfo(CURLINFO_RESPONSE_CODE,
 * &status) writes a libc `long` here, so the field must match curl's ABI. Only
 * http_status()'s Tycho-facing RETURN is tycho_int. */
typedef struct { long status; char *body; size_t len; } Resp;

/* Hard cap on a response body. curl's own CURLOPT_MAXFILESIZE only acts on a
 * Content-Length it was GIVEN, so a chunked or lying response walks past it --
 * this callback is the only place that sees the bytes actually arrive. 64 MiB is
 * far above anything corelib fetches and far below a 32-bit size_t. */
#define TY_HTTP_MAX_BODY ((size_t)64 * 1024 * 1024)

static size_t collect(char *ptr, size_t size, size_t nmemb, void *userp) {
    size_t add = size * nmemb;
    Resp *r = (Resp *)userp;
    /* Refuse before the arithmetic, not after: on ILP32 size_t is 32 bits and
     * `r->len + add + 1` WRAPS past 4 GiB, which realloc would then honour with a
     * small buffer and the memcpy below would run off it. The cap makes the sum
     * unreachable on both widths; returning short tells curl to abort the
     * transfer. */
    if (add > TY_HTTP_MAX_BODY || r->len > TY_HTTP_MAX_BODY - add) return 0;
    char *nb = (char *)realloc(r->body, r->len + add + 1);
    if (!nb) return 0;                       /* signal write error to curl */
    r->body = nb;
    memcpy(r->body + r->len, ptr, add);
    r->len += add;
    r->body[r->len] = 0;
    return add;
}

static int g_curl_init = 0;

static Resp *perform(const char *url, const char *post_body, size_t post_len, const char *ctype) {
    if (!g_curl_init) { curl_global_init(CURL_GLOBAL_DEFAULT); g_curl_init = 1; }
    CURL *c = curl_easy_init();
    if (!c) return NULL;
    Resp *r = (Resp *)calloc(1, sizeof(Resp));
    if (!r) { curl_easy_cleanup(c); return NULL; }
    r->body = (char *)malloc(1); r->body[0] = 0; r->len = 0;

    curl_easy_setopt(c, CURLOPT_URL, url);
    /* The FIRST hop needs the same fence as a redirect: this libcurl carries
     * FILE, FTP, SCP, SMTP and friends, so an unfenced CURLOPT_URL makes
     * http.get("file:///etc/passwd") a working file read. */
#ifdef CURLOPT_PROTOCOLS_STR
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
#elif defined(CURLPROTO_HTTP)
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, r);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    /* Following redirects without bounding them is the SSRF amplifier: a chain
     * can be arbitrarily long, and which SCHEMES a redirect may switch to is
     * curl-version-dependent (CURLOPT_REDIR_PROTOCOLS' default has changed).
     * Pin both here rather than inherit whatever the linked curl decided, so a
     * redirect cannot walk an https fetch onto file:// or gopher://. */
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
#ifdef CURLOPT_REDIR_PROTOCOLS_STR
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#elif defined(CURLPROTO_HTTP)
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "tycho-corelib-http/1.0");

    const char *ca_file = getenv("SSL_CERT_FILE");
    const char *ca_dir  = getenv("SSL_CERT_DIR");
    if (ca_file && *ca_file) curl_easy_setopt(c, CURLOPT_CAINFO, ca_file);
    if (ca_dir  && *ca_dir)  curl_easy_setopt(c, CURLOPT_CAPATH, ca_dir);

    struct curl_slist *hdrs = NULL;
    if (post_body) {
        /* SIZE FIRST, and COPYPOSTFIELDS: without a declared size curl calls
         * strlen() on the body, so a body with an interior NUL is sent short
         * and the Content-Length describes the truncation rather than the
         * entity. The COPY form is what makes the length authoritative. */
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)post_len);
        curl_easy_setopt(c, CURLOPT_COPYPOSTFIELDS, post_body);
        if (ctype && *ctype) {
            char h[512];
            snprintf(h, sizeof h, "Content-Type: %s", ctype);
            hdrs = curl_slist_append(hdrs, h);
            curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
        }
    }

    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) {                    /* transport failure -> NULL handle */
        free(r->body); free(r);
        if (hdrs) curl_slist_free_all(hdrs);
        curl_easy_cleanup(c);
        return NULL;
    }
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r->status);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    return r;
}
/* Binary-safe body: the full byte length including interior NULs, crossed as
 * (ptr, len) so the Tycho side builds a `bytes` value. The wrapper FREES the
 * pointer it receives (the bytes-from-C convention, like the compress shim),
 * so this hands over a malloc'd COPY -- never the Resp's own buffer, which
 * http_free also frees. */
void http_body_bytes(void *resp, const unsigned char **out, tycho_int *outlen) {
    Resp *r = (Resp *)resp;
    *out = NULL; *outlen = 0;
    if (!r || r->len <= 0) return;
    unsigned char *copy = (unsigned char *)malloc((size_t)r->len);
    if (!copy) return;
    memcpy(copy, r->body, (size_t)r->len);
    *out = copy;
    *outlen = (tycho_int)r->len;
}

void *http_get(const char *url) { return perform(url, NULL, 0, NULL); }
/* The body crosses as (ptr, len), never as a C string: a Tycho `bytes` may carry
 * interior NULs and every one of them belongs in the entity. `body` is NULL for
 * an empty body, which is still a POST -- only http_get passes no body at all. */
void *http_post_bytes(const char *url, const unsigned char *body, tycho_int len, const char *ctype) {
    if (len < 0) return NULL;
    return perform(url, body ? (const char *)body : "", (size_t)len, ctype);
}
tycho_int http_status(void *resp) { return resp ? (tycho_int)((Resp *)resp)->status : 0; }
const char *http_body(void *resp) { return resp ? ((Resp *)resp)->body : ""; }
void http_free(void *resp) { if (resp) { free(((Resp *)resp)->body); free(resp); } }
