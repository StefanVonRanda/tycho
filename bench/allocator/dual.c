/* True dual-head experiment; compile against a private native runtime copy. */
#include "tycho_rt.c"
#include <errno.h>
#include "dual.h"
typedef struct { unsigned char *p; size_t n; } Record;
typedef struct {
    Arena low, high, inner; Dual left, right;
    Record held[128]; Record *index[128]; size_t nheld, seed;
} Context;
static int mode; /* -1 native separate/small blocks, 0 native separate, 1 native single/no scratch reclaim,
                    2 fixed split, 3 dual */
static void check(int ok, const char *why) {
    if (!ok) { fprintf(stderr, "dual-contract: %s\n", why); exit(1); }
}
static void contracts(void) {
    Dual d, other;
    check(dual_init(&d, 64) && dual_init(&other, 64), "init");
    unsigned char *p = dual_alloc(&d, 9, 0), *q = dual_alloc(&d, 9, 1);
    check(p && q && !((uintptr_t)p & 7) && !((uintptr_t)q & 7), "alignment");
    memset(p, 1, 16); memset(q, 2, 16);
    check(dual_owns(&d, p, 0) && !dual_owns(&d, p, 1) && dual_owns(&d, q, 1), "ownership");
    DualMark a = dual_mark(&d);
    unsigned char *outer = dual_alloc(&d, 8, 1); memset(outer, 3, 8);
    DualMark b = dual_mark(&d);
    check(!dual_rewind(&d, a), "non-LIFO rewind accepted");
    check(!dual_rewind(&other, b), "foreign mark accepted");
    check(dual_alloc(&d, 24, 1) && d.low == d.high, "exact collision");
    size_t lo = d.low, hi = d.high;
    check(!dual_alloc(&d, 1, 0) && !dual_alloc(&d, SIZE_MAX, 1), "overflow accepted");
    check(d.low == lo && d.high == hi, "failed allocation changes cursors");
    check(dual_rewind(&d, b) && outer[0] == 3 && p[0] == 1, "inner rewind corrupts survivors");
    check(dual_rewind(&d, a) && q[0] == 2 && p[0] == 1, "outer rewind corrupts survivors");
    check(!dual_rewind(&d, a), "duplicate rewind accepted");
    dual_clear(&d);
    DualMark fresh = dual_mark(&d);
    check(!dual_rewind(&d, a) && dual_rewind(&d, fresh), "stale mark accepted");
    DualMark keep = dual_mark(&d);
    unsigned char *kept = dual_alloc(&d, 8, 1);
    memset(kept, 7, 8);
    check(dual_alloc(&d, 24, 0) != NULL, "low allocation");
    dual_clear_low(&d);
    unsigned char *replacement = dual_alloc(&d, 24, 0);
    memset(replacement, 9, 24);
    check(kept[0] == 7 && dual_owns(&d, kept, 1) && dual_rewind(&d, keep), "low reset corrupts high survivor");
    d.serial = SIZE_MAX;
    check(!dual_mark(&d).token && !d.top, "mark counter overflow");
    dual_free(&d); dual_free(&other);
    puts("dual-contract: alignment, ranges, collision, overflow, LIFO and surviving results hold");
}
static void *alloc(Context *c, size_t n, int high, int nested) {
    if (mode == 3) return dual_alloc(&c->left, n, high);
    if (mode == 2) return dual_alloc(high ? &c->right : &c->left, n, high);
    return arena_alloc(mode == 1 || !high ? &c->low : nested ? &c->inner : &c->high, n);
}
static void reset(Context *c) {
    if (mode >= 2) { dual_clear(&c->left); if (mode == 2) dual_clear(&c->right); }
    else { arena_reset(&c->low); arena_reset(&c->high); arena_reset(&c->inner); }
}
static Dual *high_owner(Context *c) { return mode == 3 ? &c->left : &c->right; }
static uint64_t hash(const unsigned char *p, size_t n) {
    uint64_t h = 0;
    for (size_t i = 0; i < n; i++) h = h * 33 + p[i];
    return h;
}
static void fill(unsigned char *p, size_t n, Record in) {
    for (size_t off = 0; off < n;) {
        size_t take = in.n < n - off ? in.n : n - off;
        memcpy(p + off, in.p, take); off += take;
    }
}
static int batch(Context *c, int scenario, Record *input, size_t count, size_t seed, uint64_t *sum) {
    Record saved[128], *index[128];
    size_t nrec = scenario == 0 ? 128 : scenario == 3 || scenario == 4 ? 1 : scenario == 5 ? 64 : scenario == 6 ? 12 : 32;
    Record *presized = mode == 1 && scenario == 0 ? alloc(c, nrec * sizeof(Record), 0, 0) : NULL;
    for (size_t i = 0; i < nrec; i++) {
        Record in = input[(seed * 131 + i * 17) % count];
        size_t low = in.n, high = in.n * 32;
        if (scenario == 3) { low = seed & 1 ? 4096 : 49152; high = seed & 1 ? 49152 : 4096; }
        if (scenario == 4) { low = 49152; high = 32768; }
        if (scenario == 5) { low = 1024; high = 8192; }
        if (scenario == 6) { low = 4096; high = (12 - i) * 4096; }
        DualMark outer = {0}, inner = {0};
        if (scenario != 0 && mode >= 2) outer = dual_mark(high_owner(c));
        unsigned char *temp = NULL, *nested = NULL;
        if (scenario != 0) {
            temp = alloc(c, high, 1, 0);
            if (!temp) return 0;
            fill(temp, high, in);
            *sum += hash(temp, high);
            if (scenario == 2) {
                if (mode >= 2) inner = dual_mark(high_owner(c));
                nested = alloc(c, high / 2, 1, 1);
                if (!nested) return 0;
                fill(nested, high / 2, in);
                *sum += hash(nested, high / 2);
            }
        }
        /* Caller-owned result allocated while scratch (including nested scratch)
         * is still live: simply rewinding a single bump cursor is not valid. */
        unsigned char *out = alloc(c, low, 0, 0);
        if (!out) return 0;
        fill(out, low, in);
        saved[i] = (Record){out, low};
        if (scenario == 0) {
            index[i] = presized ? &presized[i] : alloc(c, sizeof(Record), 1, 0);
            if (!index[i]) return 0;
            *index[i] = saved[i];
        } else {
            if (scenario == 2) {
                if (mode >= 2) check(dual_rewind(high_owner(c), inner), "nested rewind");
                else if (mode <= 0) arena_reset(&c->inner);
                for (size_t k = 0; k < high; k++) check(temp[k] == in.p[k % in.n], "outer scratch changed");
            }
            if (mode >= 2) check(dual_rewind(high_owner(c), outer), "scratch rewind");
            else if (mode <= 0) arena_reset(&c->high);
        }
    }
    for (size_t i = 0; i < nrec; i++) {
        Record r = scenario == 0 ? *index[i] : saved[i];
        check(r.p == saved[i].p && r.n == saved[i].n, "index changed");
        /* Compare every byte, not merely a checksum or the first byte. */
        Record in = input[(seed * 131 + i * 17) % count];
        for (size_t k = 0; k < r.n; k++) check(r.p[k] == in.p[k % in.n], "surviving result changed");
        *sum += hash(r.p, r.n);
    }
    memcpy(c->held, saved, nrec * sizeof(Record));
    if (scenario == 0) memcpy(c->index, index, nrec * sizeof(Record *));
    c->nheld = nrec; c->seed = seed;
    return 1;
}
static void verify_held(Context *c, Record *input, size_t count, int scenario) {
    for (size_t i = 0; i < c->nheld; i++) {
        Record r = scenario == 0 ? *c->index[i] : c->held[i];
        check(r.p == c->held[i].p && r.n == c->held[i].n, "held index changed");
        Record in = input[(c->seed * 131 + i * 17) % count];
        for (size_t k = 0; k < r.n; k++) check(r.p[k] == in.p[k % in.n], "held result changed");
    }
}
static size_t number(const char *s) {
    char *end; errno = 0; unsigned long long v = strtoull(s, &end, 10);
    check(*s && *s != '-' && !*end && errno != ERANGE && v && v <= SIZE_MAX, "invalid argument");
    return (size_t)v;
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "contract")) { contracts(); return 0; }
    if (argc != 8) return 2;
    mode = atoi(argv[1]); int scenario = atoi(argv[2]);
    check(mode >= -1 && mode <= 3 && scenario >= 0 && scenario <= 6, "invalid mode/scenario");
    size_t budget = number(argv[4]), contexts = number(argv[5]), rounds = number(argv[6]);
    int hold = atoi(argv[7]); check(hold == 0 || hold == 1, "invalid schedule");
    check(contexts <= 256 && rounds <= 10000 && budget >= 16 && budget <= 16 * 1024 * 1024
          && !(budget & 7) && budget <= SIZE_MAX / contexts, "run too large");
    FILE *f = fopen(argv[3], "rb"); check(f != NULL, "input open");
    Record input[16384]; size_t count = 0;
    for (;;) {
        unsigned char b[4]; size_t got = fread(b, 1, 4, f);
        if (!got) break;
        check(got == 4 && count < 16384, "input header");
        size_t n = (size_t)b[0] | (size_t)b[1] << 8 | (size_t)b[2] << 16 | (size_t)b[3] << 24;
        check(n > 0 && n <= 4096, "input size");
        unsigned char *p = malloc(n); check(p != NULL, "input allocation");
        check(fread(p, 1, n, f) == n, "input body"); input[count++] = (Record){p, n};
    }
    check(!ferror(f) && count > 0, "input read"); fclose(f);
    Context *c = calloc(contexts, sizeof(*c)); check(c != NULL, "contexts");
    uint64_t start = st_now_ns(), sum = 0;
    for (size_t i = 0; i < contexts; i++) {
        if (mode >= 2) {
            check(dual_init(&c[i].left, mode == 3 ? budget : budget / 2), "left init");
            if (mode == 2) check(dual_init(&c[i].right, budget / 2), "right init");
        } else { size_t block = mode == -1 ? 4096 : 0;
            c[i].low = arena_new(block); c[i].high = arena_new(block); c[i].inner = arena_new(block); }
    }
    int fits = 1;
    for (size_t r = 0; r < rounds && fits; r++) {
        for (size_t i = 0; i < contexts && fits; i++) {
            fits = batch(&c[i], scenario, input, count, r + i, &sum);
            if (fits && !hold) { verify_held(&c[i], input, count, scenario); reset(&c[i]); }
        }
        if (fits && hold) for (size_t i = 0; i < contexts; i++) {
            verify_held(&c[i], input, count, scenario); reset(&c[i]);
        }
    }
    size_t reserved = mode >= 2 ? contexts * (budget & ~(size_t)7) : atomic_load(&st_os_bytes);
    uint64_t cleanup_start = st_now_ns();
    for (size_t i = 0; i < contexts; i++) {
        if (mode >= 2) { dual_free(&c[i].left); if (mode == 2) dual_free(&c[i].right); }
        else { arena_free(&c[i].low); arena_free(&c[i].high); arena_free(&c[i].inner); }
    }
    tycho_pool_flush(); uint64_t end = st_now_ns(), ns = end - start;
    printf("{\"fits\":%s,\"checksum\":\"%llu\",\"elapsed_ns\":%llu,\"cleanup_ns\":%llu,\"reserved_bytes\":%zu}\n",
        fits ? "true" : "false", (unsigned long long)sum, (unsigned long long)ns,
        (unsigned long long)(end - cleanup_start), reserved);
    free(c); for (size_t i = 0; i < count; i++) free(input[i].p);
    return 0;
}
