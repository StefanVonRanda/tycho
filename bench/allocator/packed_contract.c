/* Shared backing must not become shared logical ownership. */
#define main base_allocator_contract_main
#include "../../tests/allocator_contract.c"
#undef main

static void shared_ranges(void) {
    Arena a = arena_new(256), b = arena_new(256);
    char *p = filled(&a, 32, 'a'), *q = filled(&b, 48, 'b');
    require(a.head->backing == b.head->backing, "test did not share physical backing");
    require(!arena_owns(&a, q - 8) && !arena_owns(&b, p - 8), "shared backing grants foreign ownership");
    tycho_str_recycle(&a, q);
    require(!a.bkt && !a.freelist, "shared backing permits foreign recycling");
    /* Flush can release a's view but must not release b's backing. */
    arena_free(&a);
    tycho_pool_flush();
    contents(q, 48, 'b');
    arena_reset(&b);
    (void)filled(&b, 48, 'c');
    arena_free(&b);
    tycho_pool_flush();
    require(atomic_load(&pack_reserved) == 0, "backing leaked after partial flush");
}

typedef struct { Arena result; atomic_int phase; } Transfer;
static void *transfer_worker(void *arg) {
    Transfer *t = arg;
    Arena scratch = arena_new(256);
    char *q = filled(&scratch, 48, 'w');
    (void)filled(&t->result, 32, 'r');
    require(scratch.head->backing == t->result.head->backing, "transfer did not share backing");
    atomic_store(&t->phase, 1);
    /* The waiting thread frees its view and flushes while this thread still
     * owns the cursor and scratch view of the same backing. */
    while (atomic_load(&t->phase) != 2) {}
    contents(q, 48, 'w');
    arena_free(&scratch);
    tycho_pool_flush();
    return NULL;
}
static void *return_worker(void *arg) {
    Transfer *t = arg;
    Arena scratch = arena_new(256);
    (void)filled(&scratch, 48, 'w');
    (void)filled(&t->result, 32, 'r');
    require(scratch.head->backing == t->result.head->backing, "returned result did not share backing");
    arena_free(&scratch);
    tycho_pool_flush();
    return NULL;
}
static void thread_transfer(void) {
    for (int i = 0; i < 32; i++) {
        Transfer t;
        t.result = arena_new(256);
        atomic_init(&t.phase, 0);
        pthread_t worker;
        require(pthread_create(&worker, NULL, transfer_worker, &t) == 0, "pthread_create failed");
        while (atomic_load(&t.phase) != 1) {}
        contents((const char *)(t.result.head + 1) + 8, 32, 'r');
        arena_free(&t.result);
        tycho_pool_flush();
        atomic_store(&t.phase, 2);
        require(pthread_join(worker, NULL) == 0, "pthread_join failed");
        require(atomic_load(&pack_reserved) == 0, "cross-thread backing leaked");
        /* The normal wait order: worker flushes and exits first, root survives. */
        t.result = arena_new(256);
        require(pthread_create(&worker, NULL, return_worker, &t) == 0, "pthread_create failed");
        require(pthread_join(worker, NULL) == 0, "pthread_join failed");
        contents((const char *)(t.result.head + 1) + 8, 32, 'r');
        arena_free(&t.result);
        tycho_pool_flush();
        require(atomic_load(&pack_reserved) == 0, "returned backing leaked");
    }
}
int main(int argc, char **argv) {
    if (argc > 1) {
        if (!strcmp(argv[1], "overflow")) (void)pack_block_new(SIZE_MAX);
        else if (!strcmp(argv[1], "slab-overflow")) (void)pack_slab_new(SIZE_MAX);
        else return 2;
        return 3;
    }
    base_allocator_contract_main();
    shared_ranges();
    thread_transfer();
    require(atomic_load(&pack_reserved) == 0, "backing leaked at exit");
    puts("packed-contract: disjoint ranges, partial flush and cross-thread release hold");
    return 0;
}
