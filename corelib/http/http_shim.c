/* core:http C shim -- HTTP(S) over libcurl. The deps manifest (corelib/http/deps)
 * names `libcurl`; pkg-config supplies the cflags (curl headers) and -lcurl, so
 * this module is turnkey on any platform that has libcurl, and its test is
 * skipped where it doesn't.
 *
 * A request returns an opaque handle (a malloc'd Resp) that tycho holds as a
 * `ptr` and never dereferences; the body is a NUL-terminated C string that tycho
 * arena-copies on return, so the handle must be freed with http_free once done
 * (FFI memory is not arena-managed). Binary bodies with interior NUL bytes
 * truncate at the arena-copy (a tycho string limitation), so this is for text. */
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { long status; char *body; size_t len; } Resp;

static size_t collect(char *ptr, size_t size, size_t nmemb, void *userp) {
    size_t add = size * nmemb;
    Resp *r = (Resp *)userp;
    char *nb = (char *)realloc(r->body, r->len + add + 1);
    if (!nb) return 0;                       /* signal write error to curl */
    r->body = nb;
    memcpy(r->body + r->len, ptr, add);
    r->len += add;
    r->body[r->len] = 0;
    return add;
}

static int g_curl_init = 0;

static Resp *perform(const char *url, const char *post_body, const char *ctype) {
    if (!g_curl_init) { curl_global_init(CURL_GLOBAL_DEFAULT); g_curl_init = 1; }
    CURL *c = curl_easy_init();
    if (!c) return NULL;
    Resp *r = (Resp *)calloc(1, sizeof(Resp));
    if (!r) { curl_easy_cleanup(c); return NULL; }
    r->body = (char *)malloc(1); r->body[0] = 0; r->len = 0;

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, r);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "tycho-corelib-http/1.0");

    struct curl_slist *hdrs = NULL;
    if (post_body) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, post_body);
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

void *http_get(const char *url) { return perform(url, NULL, NULL); }
void *http_post(const char *url, const char *body, const char *ctype) { return perform(url, body ? body : "", ctype); }
long http_status(void *resp) { return resp ? ((Resp *)resp)->status : 0; }
const char *http_body(void *resp) { return resp ? ((Resp *)resp)->body : ""; }
void http_free(void *resp) { if (resp) { free(((Resp *)resp)->body); free(resp); } }
