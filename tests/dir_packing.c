#include "tycho_rt.c"
static void need(int ok, const char *why) {
    if (!ok) { fprintf(stderr, "dir-packing: %s\n", why); exit(1); }
}
/* Before this optimization: one temporary arena string, then push copies it. */
static TychoArrStr original(Arena *a, const char *path) {
    TychoArrStr r = tycho_arr_str_with_cap(a, 8);
    DIR *d = opendir(path); if (!d) return r;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        tycho_arr_str_push(a, &r, tycho_str_from_c(a, e->d_name));
    }
    closedir(d); return r;
}
static void fresh(Arena *a, TychoArrStr *r, const char *name) {
    if (r->len == r->cap) {
        tycho_int cap = 8;
        while (cap <= r->len) { tycho_cap_check(cap, 2u * sizeof(char *)); cap *= 2; }
        tycho_arr_str_reserve(a, r, cap);
    }
    r->data[r->len++] = tycho_str_from_c(a, name);
}
/* Simpler controls: adopt fresh names, with/without a bounded stack index. */
static TychoArrStr native(Arena *a, const char *path, int stage) {
    TychoArrStr r = {NULL, 0, 0};
    char *ptrs[512]; tycho_int n = 0;
    DIR *d = opendir(path); if (!d) return r;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (stage && n < 512) { ptrs[n++] = tycho_str_from_c(a, e->d_name); continue; }
        if (stage) {
            r = tycho_arr_str_with_cap(a, n); r.len = n;
            memcpy(r.data, ptrs, (size_t)n * sizeof(char *)); stage = 0;
        }
        fresh(a, &r, e->d_name);
    }
    closedir(d);
    if (stage) {
        r = tycho_arr_str_with_cap(a, n); r.len = n;
        if (n) memcpy(r.data, ptrs, (size_t)n * sizeof(char *));
    }
    return r;
}
static TychoArrStr list(Arena *a, const char *path, int mode) {
    return mode == 0 ? original(a, path) : mode == 1 ? native(a, path, 0)
         : mode == 2 ? native(a, path, 1) : tycho_list_dir(a, path);
}
static void contract(const char *path) {
    /* Tiny blocks, overlap at commit, unused tails, and pointer-width alignment. */
    for (size_t block = 1; block <= 8192; block *= 2) {
        Arena a = arena_new(block), ref = arena_new(block);
        (void)arena_alloc(&a, 8);
        TychoArrStr r = tycho_list_dir(&a, path), expected = original(&ref, path);
        need(tycho_arr_str_eq(r, expected), "entries/order differ");
        for (tycho_int i = 0; i < r.len; i++) {
            need(arena_owns(&a, r.data[i] - 8), "name escaped owner");
            need(!((uintptr_t)(r.data[i] - 8) & 7u), "unaligned string");
        }
        TychoArrStr snap = tycho_arr_str_copy(&ref, r);
        if (r.len) tycho_arr_str_set(&a, &r, 0, tycho_str_from_c(&ref, "changed"));
        arena_reset(&a); (void)arena_alloc(&a, block);
        need(tycho_arr_str_eq(snap, expected), "snapshot depends on packed storage");
        arena_free(&a); arena_free(&ref); tycho_pool_flush();
    }
    /* Exact metadata/payload collision and recycled publication elsewhere. */
    Arena a = arena_new(1024);
    void *dead = arena_alloc(&a, 512); arena_recycle(&a, dead, 512);
    TychoDirBuild b = {0}; b.arena = &a;
    for (int i = 0; i < 8; i++) need(tycho_dir_build_add(&b, "abcdefghijklmnop"), "builder rejected fitting name");
    char *staged = (char *)(b.block + 1) + b.begin;
    TychoArrStr r = tycho_dir_build_finish(&b);
    need((char *)r.data < staged, "recycled publication path was not exercised");
    memset(arena_alloc(&a, 512), 'z', 512);   /* overwrite abandoned staging bytes */
    for (int i = 0; i < 8; i++) need(!strcmp(r.data[i], "abcdefghijklmnop"), "relocation broke name pointer");
    arena_free(&a); tycho_pool_flush();
    a = arena_new(56); b = (TychoDirBuild){0}; b.arena = &a;
    need(tycho_dir_build_add(&b, "x"), "first packed name");
    while (tycho_dir_build_add(&b, "x")) {}
    size_t low = b.low, high = b.high; tycho_int n = b.len;
    need(!tycho_dir_build_add(&b, "this-name-cannot-fit") && b.low == low && b.high == high && b.len == n,
         "collision changed builder state");
    r = tycho_dir_build_finish(&b);
    need(r.len == n, "collision lost entries");
    for (tycho_int i = 0; i < n; i++) need(!strcmp(r.data[i], "x"), "overlapping commit corrupted names");
    arena_free(&a); tycho_pool_flush();
    puts("dir-packing: ownership, order, fallback, relocation and snapshots hold");
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "overflow")) { (void)block_get(SIZE_MAX); return 2; }
    if (argc == 3 && !strcmp(argv[1], "contract")) { contract(argv[2]); return 0; }
    if (argc != 4) return 2;
    int mode = atoi(argv[1]), rounds = atoi(argv[3]);
    need(mode >= 0 && mode <= 3 && rounds > 0 && rounds <= 1000, "bad arguments");
    Arena arenas[32];
    uint64_t sum = 0, start = st_now_ns();
    for (int i = 0; i < 32; i++) arenas[i] = arena_new(0);
    for (int round = 0; round < rounds; round++) {
        for (int i = 0; i < 32; i++) {
            TychoArrStr r = list(&arenas[i], argv[2], mode);
            for (tycho_int j = 0; j < r.len; j++) {
                sum = sum * 33 + (uint64_t)tycho_str_len(r.data[j]);
                for (const unsigned char *p = (unsigned char *)r.data[j]; *p; p++) sum = sum * 33 + *p;
            }
        }
        for (int i = 0; i < 32; i++) arena_reset(&arenas[i]);
    }
    size_t reserved = atomic_load(&st_os_bytes), bump = atomic_load(&st_alloc_bytes);
    for (int i = 0; i < 32; i++) arena_free(&arenas[i]);
    tycho_pool_flush();
    printf("{\"checksum\":\"%llu\",\"elapsed_ns\":%llu,\"reserved_bytes\":%zu,\"bump_bytes\":%zu}\n",
        (unsigned long long)sum, (unsigned long long)(st_now_ns() - start), reserved, bump);
    return 0;
}
