"""Throwaway emitted-C instrumentation shared by allocator experiments."""


def once(src, old, new):
    if src.count(old) != 1:
        raise RuntimeError('instrumentation anchor drifted: ' + old[:80])
    return src.replace(old, new)


def instrument(src):
    # All counters are in a throwaway emitted-C copy. Samples occur after
    # completed alloc/reset/free operations, not during their transient states.
    helper = '''
static size_t prof_owned, prof_blocks, prof_peak_owned, prof_peak_blocks;
static size_t prof_peak_slack, prof_capacity_at_slack, prof_live_at_slack;
static void prof_sample(void) {
    size_t live = atomic_load(&st_live);
    if (live > prof_owned) { fputs("allocator-profile: inconsistent counters\\n", stderr); abort(); }
    if (prof_owned > prof_peak_owned) prof_peak_owned = prof_owned;
    if (prof_blocks > prof_peak_blocks) prof_peak_blocks = prof_blocks;
    if (prof_owned - live > prof_peak_slack) {
        prof_peak_slack = prof_owned - live;
        prof_capacity_at_slack = prof_owned;
        prof_live_at_slack = live;
    }
}
'''
    src = once(src, 'static void st_bump_live(size_t n) {',
               helper + '\nstatic void st_bump_live(size_t n) {')
    src = once(src,
               'st_peak_up(&st_peak_live, atomic_fetch_add_explicit(&st_live, n, memory_order_relaxed) + n);',
               'st_peak_up(&st_peak_live, atomic_fetch_add_explicit(&st_live, n, memory_order_relaxed) + n);\n    prof_sample();')
    start = src.index('static HBlock *block_get(size_t cap) {')
    end = src.index('\nstatic void block_release_chain', start)
    body = src[start:end]
    if body.count('return b;') != 3:
        raise RuntimeError('block_get return paths changed')
    body = body.replace('return b;', 'prof_owned += b->cap; prof_blocks++; return b;')
    src = src[:start] + body + src[end:]
    src = once(src,
               'while (b) { HBlock *nx = b->next; b->next = g_block_pool; g_block_pool = b; b = nx; }',
               'while (b) { HBlock *nx = b->next; prof_owned -= b->cap; prof_blocks--; b->next = g_block_pool; g_block_pool = b; b = nx; }')
    src = once(src,
               'b->off = 0;                     /* keep & reuse the head block */',
               'b->off = 0;                     /* keep & reuse the head block */\n    prof_sample();')
    src = once(src,
               '    block_release_chain(a->head);\n    a->head = NULL;',
               '    block_release_chain(a->head);\n    a->head = NULL;\n    prof_sample();')
    src = once(src, 'static void stats_dump(void) {',
               'static void stats_dump(void) {\n    prof_dump();')
    printer = '''
static void prof_dump(void) {
    fprintf(stderr, "allocator-profile: %zu %zu %zu %zu %zu %zu\\n",
        prof_peak_owned, prof_peak_slack, prof_capacity_at_slack,
        prof_live_at_slack, prof_peak_blocks, prof_owned);
}
'''
    src = once(src, '\nstatic void prof_sample(void) {', printer + '\nstatic void prof_sample(void) {')
    return src


def instrument_peaks(src):
    """Capture simultaneous bump-owner rows at the FIRST capacity high-water.

    Single-threaded profiling only. Rows sum to occupied block offsets, not
    live language values or per-owner backing capacity. Never time this build.
    """
    src = instrument(src)
    helper = r'''
#include <execinfo.h>
static size_t peak_cap, peak_bump, peak_os, peak_rows[TYCHO_NLBL];
static const char *peak_names[TYCHO_NLBL], *peak_owner;
static void *peak_stack[32];
static int peak_depth;
static void peak_capture(const Arena *a) {
    if (prof_owned <= peak_cap) return;
    peak_cap = prof_owned;
    peak_bump = atomic_load(&st_live);
    peak_os = atomic_load(&st_os_bytes);
    peak_owner = a->name ? a->name : st_anon;
    size_t sum = 0;
    for (int i = 0; i < TYCHO_NLBL; i++) {
        peak_names[i] = atomic_load(&st_lbl[i].key);
        peak_rows[i] = atomic_load(&st_lbl[i].live);
        sum += peak_rows[i];
    }
    if (sum != peak_bump || atomic_load(&st_lbl_lost)) {
        fputs("peak-profile: owner sum mismatch\n", stderr); abort();
    }
    peak_depth = backtrace(peak_stack, 32);
}
static void peak_dump(void) {
    fprintf(stderr, "peak-point: %zu %zu %zu %s\n", peak_cap, peak_bump,
        peak_os, peak_owner ? peak_owner : "none");
    for (int i = 0; i < TYCHO_NLBL; i++)
        if (peak_rows[i]) fprintf(stderr, "peak-label: %s\t%zu\n", peak_names[i], peak_rows[i]);
    backtrace_symbols_fd(peak_stack, peak_depth, 2);
}
'''
    src = once(src, 'static void stats_dump(void) {', helper + '\nstatic void stats_dump(void) {\n    peak_dump();')
    src = once(src, 'st_lbl_alloc(a, n);   /* charge it to the arena\'s owning function */',
               'st_lbl_alloc(a, n);   /* charge it to the arena\'s owning function */\n        peak_capture(a);')
    return src
