/* Parallel text indexer -- C rival for the hier concurrency dogfood.
 *
 *   index <corpus-dir>
 *
 * Same algorithm as index.hi: K=4 pthreads pull file paths off a shared,
 * mutex-guarded work queue (the channel analogue), each tallies a LOCAL
 * open-addressing string->long table, then the main thread merges. C owns every
 * allocation by hand (strdup keys, realloc tables, free at the end); hier's
 * arenas reclaim the equivalent for free. Prints the identical checksum. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>

#define K 4

/* ---- open-addressing hash map: char* key -> long count ---- */
typedef struct { char *key; long val; } Slot;
typedef struct { Slot *s; long cap; long n; } Map;

static unsigned long djb2(const char *p, long len) {
    unsigned long h = 5381;
    for (long i = 0; i < len; i++) h = ((h << 5) + h) ^ (unsigned char)p[i];
    return h;
}

static void map_init(Map *m, long cap) {
    m->cap = cap; m->n = 0;
    m->s = calloc((size_t)cap, sizeof(Slot));
}

static void map_grow(Map *m);

/* add `delta` to the count for the key [key,key+len); key need not be NUL-term. */
static void map_add(Map *m, const char *key, long len, long delta) {
    if ((m->n + 1) * 10 >= m->cap * 7) map_grow(m);   /* keep load factor < 0.7 */
    unsigned long h = djb2(key, len);
    long i = (long)(h & (unsigned long)(m->cap - 1));
    for (;;) {
        if (!m->s[i].key) {
            char *k = malloc((size_t)len + 1);
            memcpy(k, key, (size_t)len); k[len] = 0;
            m->s[i].key = k; m->s[i].val = delta; m->n++;
            return;
        }
        if ((long)strlen(m->s[i].key) == len && !memcmp(m->s[i].key, key, (size_t)len)) {
            m->s[i].val += delta;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static void map_grow(Map *m) {
    Map nm; map_init(&nm, m->cap * 2);
    for (long i = 0; i < m->cap; i++)
        if (m->s[i].key) {
            map_add(&nm, m->s[i].key, (long)strlen(m->s[i].key), m->s[i].val);
            free(m->s[i].key);
        }
    free(m->s);
    *m = nm;
}

/* ---- tokenizer: identical rule to index.hi / index.go ---- */
static void tally(const char *text, long len, Map *acc) {
    char cur[256]; long cl = 0;
    for (long i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 'A' && c <= 'Z') { if (cl < 255) cur[cl++] = (char)(c + 32); }
        else if (c >= 'a' && c <= 'z') { if (cl < 255) cur[cl++] = (char)c; }
        else if (cl > 0) { map_add(acc, cur, cl, 1); cl = 0; }
    }
    if (cl > 0) map_add(acc, cur, cl, 1);
}

static char *read_file(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { *out_len = 0; return NULL; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    long got = (long)fread(buf, 1, (size_t)sz, f); fclose(f);
    buf[got] = 0; *out_len = got;
    return buf;
}

/* ---- shared work queue: the channel analogue ---- */
static char **g_paths;
static long g_npaths;
static long g_next;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg) {
    Map *local = arg;
    for (;;) {
        pthread_mutex_lock(&g_mtx);
        long idx = g_next < g_npaths ? g_next++ : -1;
        pthread_mutex_unlock(&g_mtx);
        if (idx < 0) break;
        long len; char *buf = read_file(g_paths[idx], &len);
        if (buf) { tally(buf, len, local); free(buf); }
    }
    return NULL;
}

static int ends_txt(const char *s) {
    size_t n = strlen(s);
    return n >= 4 && !strcmp(s + n - 4, ".txt");
}

int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: index <corpus-dir>\n"); return 0; }
    const char *dir = argv[1];

    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "cannot open dir %s\n", dir); return 1; }
    long cap = 16; g_paths = malloc((size_t)cap * sizeof(char *)); g_npaths = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!ends_txt(e->d_name)) continue;
        if (g_npaths == cap) { cap *= 2; g_paths = realloc(g_paths, (size_t)cap * sizeof(char *)); }
        char *p = malloc(strlen(dir) + strlen(e->d_name) + 2);
        sprintf(p, "%s/%s", dir, e->d_name);
        g_paths[g_npaths++] = p;
    }
    closedir(d);

    Map locals[K];
    pthread_t th[K];
    for (int i = 0; i < K; i++) { map_init(&locals[i], 1024); }
    for (int i = 0; i < K; i++) pthread_create(&th[i], NULL, worker, &locals[i]);
    for (int i = 0; i < K; i++) pthread_join(th[i], NULL);

    Map m; map_init(&m, 1 << 14);
    for (int i = 0; i < K; i++)
        for (long j = 0; j < locals[i].cap; j++)
            if (locals[i].s[j].key)
                map_add(&m, locals[i].s[j].key, (long)strlen(locals[i].s[j].key), locals[i].s[j].val);

    long tokens = 0, csum = 0, distinct = 0;
    for (long j = 0; j < m.cap; j++)
        if (m.s[j].key) {
            distinct++;
            tokens += m.s[j].val;
            csum += (long)strlen(m.s[j].key) * m.s[j].val;
        }
    printf("files=%ld tokens=%ld distinct=%ld csum=%ld\n", g_npaths, tokens, distinct, csum);
    return 0;
}
