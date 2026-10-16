/* Runtime ownership/lifetime contract. Build with -I runtime, or with an
 * instrumented runtime copy supplied by allocator_contract.sh's controls. */
#include "tycho_rt.c"

static void require(int ok, const char *why) {
    if (!ok) { fprintf(stderr, "allocator-contract: %s\n", why); exit(1); }
}
static char *filled(Arena *a, tycho_int n, char c) {
    char *s = tycho_str_alloc(a, n);
    memset(s, c, (size_t)n);
    return s;
}
static void contents(const char *s, tycho_int n, char c) {
    require(tycho_str_len(s) == n, "survivor length changed");
    for (tycho_int i = 0; i < n; i++)
        require(s[i] == c, "survivor contents changed");
}
static void scopes(void) {
    Arena parent = arena_new(256);
    char *outer = filled(&parent, 24, 'p');
    Arena child = arena_child(&parent);
    char *inner = filled(&child, 40, 'c');
    require(arena_owns(&parent, outer - 8), "owner rejects its allocation");
    require(arena_owns(&child, inner - 8), "child rejects its allocation");
    require(!arena_owns(&child, outer - 8), "child claims parent allocation");
    require(!arena_owns(&parent, inner - 8), "parent claims child allocation");
    require(!arena_owns(&parent, "literal"), "arena claims static storage");

    /* Foreign leaves must never enter either owner's recycle list. */
    tycho_str_recycle(&parent, inner);
    tycho_str_recycle(&child, outer);
    require(!parent.bkt && !parent.freelist, "parent recycles foreign storage");
    require(!child.bkt && !child.freelist, "child recycles foreign storage");
    char *result = tycho_str_copy(&parent, inner);
    tycho_str_recycle(&child, inner);
    char *again = filled(&child, 40, 'r');
    require(again == inner, "owned leaf was not recycled");
    contents(result, 40, 'c');
    contents(outer, 24, 'p');

    /* Overflow blocks, independent parent allocations while scratch is live,
     * then repeated rewind/reuse. Parent results must survive every boundary. */
    (void)filled(&child, 4096, 'x');
    char *late = filled(&parent, 512, 'l');
    HBlock *head = child.head;
    arena_reset(&child);
    require(child.head == head && head->off == 0 && !head->next,
            "reset did not rewind head and release overflow");
    for (int i = 0; i < 128; i++) {
        char *scratch = filled(&child, 300 + i, 's');
        require(((uintptr_t)(scratch - 8) & 7u) == 0, "allocation is not aligned");
        contents(outer, 24, 'p'); contents(result, 40, 'c'); contents(late, 512, 'l');
        arena_reset(&child);
    }
    arena_free(&child);
    require(!child.head && !child.bkt && !child.freelist, "release leaves live arena state");
    arena_free(&child);  /* task finalization can release an already-waited root */
    contents(result, 40, 'c'); contents(late, 512, 'l');
    Arena sibling = arena_child(&parent);
    (void)filled(&sibling, 4096, 'z');
    require(!arena_owns(&sibling, result - 8), "sibling claims surviving result");
    contents(result, 40, 'c');
    arena_free(&sibling);

    /* A fresh append state detaches borrowed storage; forwarded state reuses
     * capacity. Rebinding must not grant stale capacity to another buffer. */
    char *acc = outer;
    TychoStrAppend state = {0};
    tycho_str_append_char(&parent, &acc, &state, '!');
    require(acc != outer, "first append mutates borrowed input");
    char *adopted = acc;
    char *snapshot = tycho_str_copy(&parent, acc);
    tycho_str_append_char(&parent, &acc, &state, '?');
    require(acc == adopted, "forwarded append state loses capacity");
    require(tycho_str_len(snapshot) == 25 && snapshot[24] == '!', "snapshot changed");
    acc = filled(&parent, 1, 'k');
    tycho_str_append_char(&parent, &acc, &state, '.');
    require(tycho_str_len(acc) == 2 && acc[0] == 'k' && acc[1] == '.', "rebind uses stale state");
    contents(outer, 24, 'p');
    arena_free(&parent);
}
int main(void) {
    scopes();
    tycho_pool_flush();
    puts("allocator-contract: ownership, recycling, survivors, reset and append state hold");
    return 0;
}
