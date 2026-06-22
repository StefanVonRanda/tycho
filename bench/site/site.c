/* site benchmark (C): render N Markdown pages to HTML, transiently -- the same
 * work as site.ty, gated by the same FNV-1a-32 checksum. Every page's buffers
 * are malloc'd and freed by hand, so peak RSS is the per-page working set: the
 * manual-memory baseline tycho's arena must match WITHOUT any free() in the
 * source. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct { char *p; size_t len, cap; } Buf;

static void bput(Buf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        while (b->len + n + 1 > b->cap) b->cap = b->cap ? b->cap * 2 : 256;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
}
static void bputs(Buf *b, const char *s) { bput(b, s, strlen(s)); }

static void esc(Buf *out, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '&') bputs(out, "&amp;");
        else if (c == '<') bputs(out, "&lt;");
        else if (c == '>') bputs(out, "&gt;");
        else bput(out, &c, 1);
    }
}

/* block-level Markdown subset -> HTML, splitting `body` on '\n' (trailing empty
 * segments kept, matching tycho's split). */
static void render(Buf *out, const char *body) {
    int in_list = 0;
    const char *p = body;
    for (;;) {
        const char *nl = strchr(p, '\n');
        size_t tlen = nl ? (size_t)(nl - p) : strlen(p);
        const char *t = p;
        if (tlen == 0) {
            if (in_list) { bputs(out, "</ul>\n"); in_list = 0; }
        } else if (tlen >= 3 && t[0] == '#' && t[1] == '#' && t[2] == ' ') {
            if (in_list) { bputs(out, "</ul>\n"); in_list = 0; }
            bputs(out, "<h2>"); esc(out, t + 3, tlen - 3); bputs(out, "</h2>\n");
        } else if (tlen >= 2 && t[0] == '#' && t[1] == ' ') {
            if (in_list) { bputs(out, "</ul>\n"); in_list = 0; }
            bputs(out, "<h1>"); esc(out, t + 2, tlen - 2); bputs(out, "</h1>\n");
        } else if (tlen >= 2 && t[0] == '-' && t[1] == ' ') {
            if (!in_list) { bputs(out, "<ul>\n"); in_list = 1; }
            bputs(out, "<li>"); esc(out, t + 2, tlen - 2); bputs(out, "</li>\n");
        } else {
            if (in_list) { bputs(out, "</ul>\n"); in_list = 0; }
            bputs(out, "<p>"); esc(out, t, tlen); bputs(out, "</p>\n");
        }
        if (!nl) break;
        p = nl + 1;
    }
    if (in_list) bputs(out, "</ul>\n");
}

static char *read_all(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { /* short read: still NUL-terminate */ }
    buf[sz] = 0;
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 3) { printf("usage: site <pages-dir> <N>\n"); return 0; }
    const char *dir = argv[1];
    long n = atol(argv[2]);
    uint32_t fnv = 2166136261u;
    long total = 0;
    char path[1024];
    for (long i = 0; i < n; i++) {
        snprintf(path, sizeof path, "%s/%ld.md", dir, i);
        char *body = read_all(path);
        if (!body) { fprintf(stderr, "cannot read %s\n", path); return 1; }
        Buf out = {0, 0, 0};
        render(&out, body);
        for (size_t j = 0; j < out.len; j++) {
            fnv ^= (uint8_t)out.p[j];
            fnv *= 16777619u;
        }
        total += (long)out.len;
        free(out.p);     /* manual free per page -> flat memory */
        free(body);
    }
    printf("pages=%ld bytes=%ld fnv=%u\n", n, total, fnv);
    return 0;
}
