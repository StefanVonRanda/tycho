/* EXPERIMENT ONLY: inserted into emitted C by compare.py, never the runtime.
 * A slab owns storage; HBlocks own disjoint, immutable subranges of it.
 * Pooling transfers a whole view, not its backing. Reset only rewinds that view.
 * No lifetime ordering is assumed, including caller allocations inside callees.
 *
 * Only the creating thread advances the slab cursor. Each materialized view and
 * the current cursor hold one backing reference. Views can cross the task/wait
 * boundary, so backing references are atomic even though cursors/pools are TLS.
 * These are backing-block references, not references on language values.
 */
typedef struct PackSlab {
    _Alignas(8) atomic_size_t refs;
    size_t cap, off;
} PackSlab;
static __thread PackSlab *pack_cursor;
static atomic_size_t pack_reserved, pack_peak_reserved, pack_slabs;

static PackSlab *pack_slab_new(size_t cap) {
    if (cap > SIZE_MAX - sizeof(PackSlab)) tycho_oom();
    uint64_t start = g_arena_stats ? st_now_ns() : 0;
    PackSlab *s = malloc(sizeof(*s) + cap);
    if (!s) tycho_oom();
    atomic_init(&s->refs, 1);
    s->cap = cap;
    s->off = 0;
    size_t bytes = sizeof(*s) + cap;
    if (g_arena_stats) {
        size_t live = atomic_fetch_add(&pack_reserved, bytes) + bytes;
        st_peak_up(&pack_peak_reserved, live);
        atomic_fetch_add(&pack_slabs, 1);
        atomic_fetch_add(&st_ns_os, (size_t)(st_now_ns() - start));
        atomic_fetch_add(&st_os_bytes, bytes);
        atomic_fetch_add(&st_os_blocks, 1);
    }
    return s;
}
static void pack_slab_drop(PackSlab *s) {
    if (atomic_fetch_sub_explicit(&s->refs, 1, memory_order_acq_rel) == 1) {
        if (g_arena_stats) atomic_fetch_sub(&pack_reserved, sizeof(*s) + s->cap);
        free(s);
    }
}
static void pack_cursor_drop(void) {
    PackSlab *s = pack_cursor;
    pack_cursor = NULL;
    if (s) pack_slab_drop(s);
}
static HBlock *pack_block_new(size_t cap) {
    if (cap > SIZE_MAX - 7u - sizeof(HBlock)) tycho_oom();
    cap = (cap + 7u) & ~(size_t)7u;
    size_t bytes = sizeof(HBlock) + cap;
    PackSlab *s;
    if (bytes > TYCHO_BLOCK_DEFAULT) {
        /* Oversized requests get dedicated backing; don't discard a small
         * slab's remaining tail just because one array grows very large. */
        s = pack_slab_new(bytes);  /* its initial reference belongs to the view */
    } else {
        if (!pack_cursor || bytes > pack_cursor->cap - pack_cursor->off) {
            pack_cursor_drop();
            pack_cursor = pack_slab_new(TYCHO_BLOCK_DEFAULT);
        }
        s = pack_cursor;
        atomic_fetch_add_explicit(&s->refs, 1, memory_order_relaxed);
    }
    HBlock *b = (HBlock *)((char *)(s + 1) + s->off);
    s->off += bytes;
    b->backing = s;
    b->cap = cap;
    return b;
}
static void pack_block_free(HBlock *b) { pack_slab_drop(b->backing); }
