/* json head-to-head, C port. Same document + checksum as the tycho/Go ports.
 * A hand-written recursive-descent parser builds a tagged-union tree with one malloc
 * per node (the natural C representation of a generic JSON value), held live across
 * the traversal -- so peak RSS reflects the whole tree, the memory under test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { JNULL, JBOOL, JNUM, JSTR, JARR, JOBJ };
typedef struct Json {
    int kind;
    long num;                    /* JNUM */
    int b;                       /* JBOOL */
    char *str;                   /* JSTR */
    struct Json **items;         /* JARR */
    char **keys; struct Json **vals;  /* JOBJ */
    int len;                     /* JARR/JOBJ element count */
} Json;

static Json *jnew(int kind) { Json *j = (Json *)calloc(1, sizeof(Json)); j->kind = kind; return j; }

static const char *P;
static void skip_ws(void) { while (*P == ' ' || *P == '\t' || *P == '\n' || *P == '\r') P++; }
static Json *parse_value(void);

static char *parse_str_raw(void) {            /* P at opening quote; returns malloc'd, unescaped */
    P++;
    size_t cap = 16, n = 0;
    char *r = (char *)malloc(cap);
    while (*P && *P != '"') {
        char c = *P;
        if (c == '\\') {
            P++;
            if (!*P) break;
            switch (*P) { case 'n': c = '\n'; break; case 't': c = '\t'; break;
                          case '"': c = '"'; break; case '\\': c = '\\'; break; default: c = *P; }
        }
        if (n + 1 >= cap) { cap *= 2; r = (char *)realloc(r, cap); }
        r[n++] = c;
        P++;
    }
    if (*P == '"') P++;
    r[n] = 0;
    return r;
}

static Json *parse_array(void) {
    P++; skip_ws();
    Json *j = jnew(JARR);
    size_t cap = 4;
    j->items = (Json **)malloc(cap * sizeof(Json *));
    while (*P && *P != ']') {
        if ((size_t)j->len >= cap) { cap *= 2; j->items = (Json **)realloc(j->items, cap * sizeof(Json *)); }
        j->items[j->len++] = parse_value();
        skip_ws();
        if (*P == ',') { P++; skip_ws(); }
    }
    if (*P == ']') P++;
    return j;
}

static Json *parse_object(void) {
    P++; skip_ws();
    Json *j = jnew(JOBJ);
    size_t cap = 4;
    j->keys = (char **)malloc(cap * sizeof(char *));
    j->vals = (Json **)malloc(cap * sizeof(Json *));
    while (*P && *P != '}') {
        if ((size_t)j->len >= cap) {
            cap *= 2;
            j->keys = (char **)realloc(j->keys, cap * sizeof(char *));
            j->vals = (Json **)realloc(j->vals, cap * sizeof(Json *));
        }
        char *k = parse_str_raw();
        skip_ws();
        if (*P == ':') P++;
        skip_ws();
        j->keys[j->len] = k;
        j->vals[j->len] = parse_value();
        j->len++;
        skip_ws();
        if (*P == ',') { P++; skip_ws(); }
    }
    if (*P == '}') P++;
    return j;
}

static Json *parse_value(void) {
    skip_ws();
    char c = *P;
    if (c == '{') return parse_object();
    if (c == '[') return parse_array();
    if (c == '"') { Json *j = jnew(JSTR); j->str = parse_str_raw(); return j; }
    if (c == 't') { P += 4; Json *j = jnew(JBOOL); j->b = 1; return j; }
    if (c == 'f') { P += 5; Json *j = jnew(JBOOL); j->b = 0; return j; }
    if (c == 'n') { P += 4; return jnew(JNULL); }
    /* number */
    int neg = 0;
    if (c == '-') { neg = 1; P++; }
    long v = 0;
    while (*P >= '0' && *P <= '9') { v = v * 10 + (*P - '0'); P++; }
    Json *j = jnew(JNUM);
    j->num = neg ? -v : v;
    return j;
}

static Json *json_parse(const char *s) { P = s; return parse_value(); }

static Json *jget(Json *j, const char *key) {     /* object field, else a shared JNULL */
    static Json jnull = { JNULL, 0, 0, 0, 0, 0, 0, 0 };
    if (j->kind != JOBJ) return &jnull;
    for (int i = 0; i < j->len; i++) if (strcmp(j->keys[i], key) == 0) return j->vals[i];
    return &jnull;
}
static long as_num(Json *j) { return j->kind == JNUM ? j->num : 0; }
static long len_of(Json *j) {
    if (j->kind == JSTR) return (long)strlen(j->str);
    if (j->kind == JARR || j->kind == JOBJ) return j->len;
    return 0;
}

int main(void) {
    int n = 50000;
    /* build the document (same as the tycho/Go ports) */
    size_t cap = 1 << 20, len = 0;
    char *doc = (char *)malloc(cap);
    char rec[128];
    doc[len++] = '[';
    for (int i = 0; i < n; i++) {
        int m = snprintf(rec, sizeof rec,
            "%s{\"id\":%d,\"cat\":%d,\"amt\":%ld,\"name\":\"u%d\"}",
            i ? "," : "", i, i % 32, ((long)i * 2654435761L) % 1000, i % 1000);
        if (len + (size_t)m + 2 >= cap) { cap *= 2; doc = (char *)realloc(doc, cap); }
        memcpy(doc + len, rec, (size_t)m); len += (size_t)m;
    }
    doc[len++] = ']'; doc[len] = 0;

    Json *j = json_parse(doc);
    long acc = 0;
    for (int k = 0; k < j->len; k++) {
        Json *e = j->items[k];
        acc += as_num(jget(e, "id")) + as_num(jget(e, "cat")) + as_num(jget(e, "amt")) + len_of(jget(e, "name"));
    }
    printf("%ld\n", acc);
    return 0;
}
