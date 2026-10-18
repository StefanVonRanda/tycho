/* Generated enum recycling must use the same size as variant allocation. */
#define main enum_fixture_main
#include TYCHO_ENUM_RECYCLE_GENERATED
#undef main

int main(void) {
    const size_t sizes[] = {
        offsetof(E_RecycleValue, u) + sizeof(TychoPayload_RecycleValue_Tiny),
        offsetof(E_RecycleValue, u) + sizeof(TychoPayload_RecycleValue_Medium),
        offsetof(E_RecycleValue, u) + sizeof(TychoPayload_RecycleValue_Wide),
    };
    for (int tag = 1; tag <= 3; tag++) {
        Arena a = arena_new(4096);
        E_RecycleValue *v = arena_alloc(&a, sizes[tag - 1]);
        memset(v, 0, sizes[tag - 1]);
        v->tag = tag;
        unsigned char *guard = arena_alloc(&a, 128);
        memset(guard, 0xa5, 128);
        tycho_recycle_E_RecycleValue(&a, v);
        void *large = arena_alloc(&a, sizes[2]);
        memset(large, 0x5a, sizes[2]);
        for (size_t i = 0; i < 128; i++) {
            if (guard[i] != 0xa5) {
                fputs("enum-recycle-size: payload reuse overwrote adjacent allocation\n", stderr);
                arena_free(&a);
                return 1;
            }
        }
        arena_free(&a);
    }
    puts("enum-recycle-size: all payload sizes preserve adjacent allocations");
    return 0;
}
