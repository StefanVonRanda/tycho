/* Tycho runtime - embedded verbatim into every generated C file.
 *
 * Memory model: implicit hierarchical arenas.
 *
 *   - Every scope (each proc, each if/else block, each loop) gets its own
 *     arena. An arena owns a singly-linked list of bump-allocated blocks
 *     and grows by adding blocks; it never reallocs a live block, so
 *     every pointer it hands out stays valid until the arena is reset or
 *     freed.
 *   - Arenas form a hierarchy: arena_child(parent) creates a child arena.
 *     A child is freed (its blocks released) when its scope ends; a loop's
 *     scratch arena is reset (blocks released) every iteration, so loop
 *     memory stays bounded.
 *
 * Data moves between arenas exactly two ways, and because each arena has
 * its own storage both are simply "allocate in the target arena":
 *
 *   1. Down, as a function argument: a pointer is passed to a callee. The
 *      callee's arena is a child, so the pointer outlives the call. No copy.
 *   2. Up, by being returned: the value is allocated directly in the
 *      caller's (parent) arena, so it survives the callee's arena being
 *      freed. Assigning to a variable owned by an outer scope works the
 *      same way - the compiler allocates the value in that variable's
 *      arena.
 *
 * No part of this is visible in Tycho source: a programmer only declares
 * and uses values, as if the language were dynamically managed.
 */

/* strict -std=c11 (__STRICT_ANSI__) hides the POSIX declarations the
 * concurrency runtime needs (clock_gettime, sched_yield, nanosleep, ...);
 * _DEFAULT_SOURCE restores glibc's default visibility. Must precede every
 * include -- this runtime is the first thing in each generated file. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <limits.h>    /* LONG_MIN for the integer-division overflow guard */
#include <math.h>
#include <dirent.h>
#include <pthread.h>   /* spawn/wait tasks; on modern glibc pthread_* lives in libc */
#include <unistd.h>    /* sysconf(_SC_NPROCESSORS_ONLN) for parallel-for chunking */
#include <stdatomic.h> /* lock-free channel fast path (CC-5) */
#include <sched.h>     /* sched_yield in the spin-escalation ladder */
#include <time.h>      /* clock_gettime (clock()), time() (now()) */
#include <time.h>      /* timed parking */

#define TYCHO_BLOCK_DEFAULT (1u << 16)

typedef struct HBlock HBlock;
struct HBlock { HBlock *next; size_t cap; size_t off; };
/* block payload lives immediately after the header */

/* Per-arena object free-list for liveness-driven recycling. When the compiler
 * proves a heap buffer is dead and uniquely owned (a reassigned loop-carried
 * local — value semantics guarantees no aliasing), it calls arena_recycle to
 * hand the buffer back; the next same-or-smaller arena_alloc reuses it instead
 * of bumping. This is FBIP-style in-place reuse derived from STATIC value
 * semantics, not runtime refcounts (cf. Perceus). Bounded by TYCHO_FREECAP nodes
 * so the alloc-path scan stays O(1)-ish. The list is per-arena, so arena_reset/
 * arena_free simply drop it (the chunks live in blocks that get pooled/rewound,
 * so a stale node could otherwise alias a fresh bump — clearing prevents that). */
typedef struct FreeNode { struct FreeNode *next; size_t size; } FreeNode;
#define TYCHO_FREECAP 32
/* Tiny-object recycling uses a SEGREGATED free-list: one LIFO per 8-byte size
 * class, so push/pop is O(1) with no cap and no scan -- a sliding-window eviction
 * of heap records (peak ~ window) needs the per-class list to grow to the window
 * size, which a single capped best-fit list cannot do. TYCHO_NBKT classes cover
 * sizes 8..TYCHO_NBKT*8-8 bytes (strings/small structs -- the churn that
 * accumulates); larger chunks (array spines etc.) fall back to the capped
 * best-fit `freelist`, unchanged, so MM-8 large-buffer reuse is unaffected.
 *
 * The bucket table is a LAZILY-ALLOCATED pointer, NULL until an arena first
 * recycles a tiny object. This matters because generated code creates a by-value
 * scope `Arena` per function call / loop iteration (e.g. recursive node builders);
 * an inline `FreeNode *bkt[TYCHO_NBKT]` array bloated that struct by 128 B and made
 * arena_new/reset/free each run a TYCHO_NBKT-iteration init/clear loop on every
 * call -- a ~5x regression on allocation-heavy workloads that never recycle
 * (binary-trees, maptree). With a lazy pointer a non-recycling arena pays only a
 * single NULL store + one not-taken branch; only recycling arenas allocate the
 * table (once, freed at arena_free). */
#define TYCHO_NBKT 16
typedef struct { HBlock *head; size_t blocksz; FreeNode **bkt; FreeNode *freelist; int nfree; } Arena;

static void tycho_oom(void) { fprintf(stderr, "tycho: out of memory\n"); exit(1); }

/* Integer division/modulo guard. The int-overflow contract
 * (docs/internals/integer-overflow.md) defines signed overflow as two's-complement
 * wrapping via -fwrapv -- but division is the one arithmetic op -fwrapv does NOT
 * make total: `x / 0` and `x % 0` are undefined (SIGFPE on x86), and LONG_MIN/-1
 * overflows the quotient (also a trap). Abort cleanly with a tycho: message, like
 * the bounds checks. LONG_MIN % -1 is mathematically 0, so modulo returns it
 * directly instead of trapping. Int `/`/`%` route through these (codegen). */
static long tycho_idiv(long a, long b) {
    if (b == 0) { fprintf(stderr, "tycho: division by zero\n"); exit(1); }
    if (a == LONG_MIN && b == -1) { fprintf(stderr, "tycho: division overflow\n"); exit(1); }
    return a / b;
}
static long tycho_imod(long a, long b) {
    if (b == 0) { fprintf(stderr, "tycho: modulo by zero\n"); exit(1); }
    if (a == LONG_MIN && b == -1) return 0;
    return a % b;
}
/* Unsigned div/mod for u32/u64: same clean-abort guard as the signed path. No
 * overflow case (unsigned division never traps except on zero). */
static unsigned long long tycho_udiv(unsigned long long a, unsigned long long b) {
    if (b == 0) { fprintf(stderr, "tycho: division by zero\n"); exit(1); }
    return a / b;
}
static unsigned long long tycho_umod(unsigned long long a, unsigned long long b) {
    if (b == 0) { fprintf(stderr, "tycho: modulo by zero\n"); exit(1); }
    return a % b;
}
/* Shift guard. Shifting by >= the operand's bit width, or by a negative count, is
 * C undefined behavior -- the determinism contract requires a defined result.
 * A count >= width shifts every bit out, so the result is 0 (as Go, Odin, and
 * Swift all define). A negative count is a program bug (like a bad index), so
 * abort cleanly with a tycho: message. Signed `<<` is computed in unsigned to
 * dodge signed-overflow UB (the -fwrapv two's-complement contract); signed `>>`
 * stays arithmetic (sign-extending) for in-range counts. */
static long tycho_shl_i(long x, long long n) {
    if (n < 0) { fprintf(stderr, "tycho: negative shift count\n"); exit(1); }
    if (n >= 64) return 0;
    return (long)((unsigned long)x << n);
}
static long tycho_shr_i(long x, long long n) {
    if (n < 0) { fprintf(stderr, "tycho: negative shift count\n"); exit(1); }
    if (n >= 64) return 0;
    return x >> n;
}
static unsigned int tycho_shl_u32(unsigned int x, long long n) {
    if (n < 0) { fprintf(stderr, "tycho: negative shift count\n"); exit(1); }
    if (n >= 32) return 0;
    return x << n;
}
static unsigned int tycho_shr_u32(unsigned int x, long long n) {
    if (n < 0) { fprintf(stderr, "tycho: negative shift count\n"); exit(1); }
    if (n >= 32) return 0;
    return x >> n;
}
static unsigned long long tycho_shl_u64(unsigned long long x, long long n) {
    if (n < 0) { fprintf(stderr, "tycho: negative shift count\n"); exit(1); }
    if (n >= 64) return 0;
    return x << n;
}
static unsigned long long tycho_shr_u64(unsigned long long x, long long n) {
    if (n < 0) { fprintf(stderr, "tycho: negative shift count\n"); exit(1); }
    if (n >= 64) return 0;
    return x >> n;
}
/* Generic width-parameterized shift for the narrow sized ints (u8/u16/i8/i16) and
 * i32/i64: `w` is the type's bit width; the caller truncates the returned value to
 * the exact C type. Same guard as above (count >= width -> 0, negative -> abort).
 * shrn's `sgn` selects arithmetic (signed) vs logical (unsigned) fill; the caller
 * passes x already widened with the correct sign. */
static long long tycho_shln(long long x, long long n, int w) {
    if (n < 0) { fprintf(stderr, "tycho: negative shift count\n"); exit(1); }
    if (n >= w) return 0;
    return (long long)((unsigned long long)x << n);
}
static long long tycho_shrn(long long x, long long n, int w, int sgn) {
    if (n < 0) { fprintf(stderr, "tycho: negative shift count\n"); exit(1); }
    if (n >= w) return 0;
    if (sgn) return x >> n;
    return (long long)((unsigned long long)x >> n);
}
/* Float-to-int guard. Casting a NaN or an out-of-range float to an integer is C
 * undefined behavior; the determinism contract requires a defined result, so
 * abort cleanly instead (Swift traps here too). In-range values still truncate
 * toward zero. The valid interval is [-2^63, 2^63); 2^63 is not a representable
 * long. The negated `>=/<` form also catches NaN (both comparisons are false). */
static long tycho_f2i(double x) {
    if (!(x >= -9223372036854775808.0 && x < 9223372036854775808.0)) {
        fprintf(stderr, "tycho: float-to-int conversion out of range\n");
        exit(1);
    }
    return (long)x;
}
/* reserve() takes a runtime int straight from user code: a negative or huge n
 * would make (size_t)n*elem wrap, allocating a tiny buffer under a huge cap --
 * every later push then writes out of bounds. Fail loudly instead. */
static void tycho_cap_check(long n, size_t elem) {
    if (n < 0 || (unsigned long)n > (size_t)-1 / elem) {
        fprintf(stderr, "tycho: reserve capacity %ld out of range\n", n);
        exit(1);
    }
}

/* ---- arena stats (TYCHO_ARENA_STATS): opt-in memory observability ---------
 * The language whose thesis is "memory from structure" should be able to show
 * that memory stayed bounded and that blocks got reused. Set TYCHO_ARENA_STATS
 * to anything but empty/0 and a one-shot summary prints to stderr at exit.
 *
 * Scope: GLOBAL aggregates, not per-scope. Arenas carry no name at runtime, so
 * per-scope attribution would mean threading a label through every codegen emit
 * site -- deferred (ponytail: global first; add per-scope labels when a real
 * profiling need shows up). All counters are only touched when g_arena_stats is
 * set, so a normal run pays one never-taken branch per alloc.
 *
 * Threading: the block pool is thread-local, but these aggregates are shared, so
 * every counter is atomic. g_arena_stats is set once by a constructor (before
 * main, before any thread) and only read after, so it's a plain int. */
static int g_arena_stats = 0;
static atomic_size_t st_live, st_peak_live, st_alloc_calls, st_alloc_bytes,
                     st_os_bytes, st_os_blocks, st_block_gets,
                     st_arenas, st_arena_frees;

static void st_bump_live(size_t n) {  /* live += n; track high-water */
    size_t cur = atomic_fetch_add_explicit(&st_live, n, memory_order_relaxed) + n;
    size_t pk = atomic_load_explicit(&st_peak_live, memory_order_relaxed);
    while (cur > pk && !atomic_compare_exchange_weak_explicit(
            &st_peak_live, &pk, cur, memory_order_relaxed, memory_order_relaxed)) {}
}
static size_t st_chain_off(HBlock *b) { size_t s = 0; for (; b; b = b->next) s += b->off; return s; }
static void st_drop_live(HBlock *b) {  /* whole arena chain going away/rewound */
    if (b) atomic_fetch_sub_explicit(&st_live, st_chain_off(b), memory_order_relaxed);
}

static void st_fmt(size_t n, char *out, size_t outsz) {
    const char *u[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    double v = (double)n; int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; i++; }
    if (i == 0) snprintf(out, outsz, "%zu B", n);
    else        snprintf(out, outsz, "%.1f %s", v, u[i]);
}

static void stats_dump(void) {
    char live[32], bump[32], os[32];
    size_t gets = atomic_load(&st_block_gets), osbl = atomic_load(&st_os_blocks);
    size_t reuse = gets - osbl;   /* every block_get either mallocs or reuses a pooled block */
    st_fmt(atomic_load(&st_peak_live), live, sizeof live);
    st_fmt(atomic_load(&st_alloc_bytes), bump, sizeof bump);
    st_fmt(atomic_load(&st_os_bytes), os, sizeof os);
    fprintf(stderr,
        "\n[tycho arena stats]\n"
        "  peak live:   %s   (working-set high-water)\n"
        "  bump-alloc:  %s over %zu allocations\n"
        "  OS reserved: %s over %zu blocks\n"
        "  block reuse: %zu of %zu requests from pool (%.1f%%)\n"
        "  arenas:      %zu created, %zu freed\n",
        live, bump, (size_t)atomic_load(&st_alloc_calls),
        os, osbl,
        reuse, gets, gets ? 100.0 * (double)reuse / (double)gets : 0.0,
        (size_t)atomic_load(&st_arenas), (size_t)atomic_load(&st_arena_frees));
}

/* Runs before main (gcc/clang, same family as the __thread pool below). Reads
 * the env once; when off, nothing else in this file does any work. */
__attribute__((constructor)) static void stats_init(void) {
    const char *e = getenv("TYCHO_ARENA_STATS");
    if (e && *e && *e != '0') { g_arena_stats = 1; atexit(stats_dump); }
}

/* Global block free-list. Arenas are created/reset/freed per block scope, call,
 * and loop iteration, so naive malloc/free of a TYCHO_BLOCK_DEFAULT-sized block
 * per scope dominated runtime on allocation-heavy workloads (e.g. the
 * self-hosting compiler: ~13x slower than no-free). Instead of returning blocks
 * to the OS, reset/free hand them to this pool, and arena_alloc takes from it
 * first -- O(1) pointer ops, no malloc/free churn, no page re-faulting. Peak
 * live memory is unchanged (the pool holds at most what a scope just released);
 * pool blocks are reclaimed by the OS at process exit.
 *
 * THREAD-LOCAL (CC-0): each thread owns a private pool, so allocation never
 * contends and never races. A spawned task's arenas may be built on one thread
 * and freed on another (wait() frees the task tree on the waiting thread) --
 * that's fine: a block is just malloc'd memory; releasing it pushes it onto
 * the *releasing* thread's pool. A spawned thread flushes its own pool to
 * free() before exiting (tycho_pool_flush) so nothing leaks with the TLS. */
static __thread HBlock *g_block_pool = NULL;

static HBlock *block_get(size_t cap) {
    if (g_arena_stats) atomic_fetch_add_explicit(&st_block_gets, 1, memory_order_relaxed);
    if (g_block_pool && g_block_pool->cap >= cap) {  /* fast path: head fits (uniform sizes) */
        HBlock *b = g_block_pool;
        g_block_pool = b->next;
        b->off = 0;
        b->next = NULL;
        return b;
    }
    /* head too small for an over-sized request (e.g. a geometrically grown array
     * buffer > blocksz): scan the pool for a block that fits rather than malloc a
     * fresh one and leave the suitable larger block buried -- otherwise variable
     * large blocks never get reused and the pool grows pass over pass. */
    for (HBlock **link = &g_block_pool; *link; link = &(*link)->next)
        if ((*link)->cap >= cap) {
            HBlock *b = *link;
            *link = b->next;
            b->off = 0;
            b->next = NULL;
            return b;
        }
    HBlock *b = (HBlock *)malloc(sizeof(HBlock) + cap);
    if (!b) tycho_oom();
    if (g_arena_stats) {
        atomic_fetch_add_explicit(&st_os_bytes, sizeof(HBlock) + cap, memory_order_relaxed);
        atomic_fetch_add_explicit(&st_os_blocks, 1, memory_order_relaxed);
    }
    b->cap = cap;
    b->off = 0;
    b->next = NULL;
    return b;
}

static void block_release_chain(HBlock *b) {     /* push a block chain onto the pool */
    while (b) { HBlock *nx = b->next; b->next = g_block_pool; g_block_pool = b; b = nx; }
}

Arena arena_new(size_t blocksz) {
    if (g_arena_stats) atomic_fetch_add_explicit(&st_arenas, 1, memory_order_relaxed);
    Arena a;
    a.head = NULL;
    a.blocksz = blocksz ? blocksz : TYCHO_BLOCK_DEFAULT;
    a.bkt = NULL;                            /* lazily allocated on first tiny recycle */
    a.freelist = NULL;
    a.nfree = 0;
    return a;
}

/* Hand a proven-dead, uniquely-owned buffer back to its arena for reuse. `n` is
 * the buffer's byte size. Stored only if it can hold a FreeNode header and the
 * list isn't full (else dropped — it still frees normally at arena_reset/free).
 * SOUND because: the caller has proven nothing else references this buffer
 * (value semantics ⇒ unique ownership), and arena_alloc only ever bumps FORWARD,
 * so a recycled chunk (already below the bump pointer) can never overlap a fresh
 * bump — each region is handed out by exactly one path at a time. */
void arena_recycle(Arena *a, void *p, size_t n) {
    if (!p || n < sizeof(FreeNode)) return;
    n = (n + 7u) & ~(size_t)7u;             /* match arena_alloc's rounding so size classes line up */
    size_t k = n >> 3;                       /* 8-byte size class */
    FreeNode *fn = (FreeNode *)p;
    fn->size = n;
    if (k < TYCHO_NBKT) {                      /* tiny object: O(1) per-class push, NO cap (this is what lets
                                              * an eviction window's dead records all be reused) */
        if (!a->bkt) {                        /* first tiny recycle for this arena: allocate the table */
            a->bkt = (FreeNode **)calloc(TYCHO_NBKT, sizeof(FreeNode *));
            if (!a->bkt) return;              /* OOM: drop the chunk (still freed at arena_reset/free) */
        }
        fn->next = a->bkt[k];
        a->bkt[k] = fn;
        return;
    }
    if (a->nfree >= TYCHO_FREECAP) return;     /* large chunk: capped best-fit list, unchanged */
    fn->next = a->freelist;
    a->freelist = fn;
    a->nfree++;
}

Arena arena_child(Arena *parent) { return arena_new(parent->blocksz); }

/* Does `p` point inside one of this arena's blocks? Used by element-overwrite
 * recycling (MM-9) to recycle ONLY buffers this arena owns -- an interned string
 * literal (malloc'd, immortal, possibly shared) or a buffer from another arena
 * must never be handed to a->freelist. O(blocks); the list is short for a
 * recycling arena (it stays bounded precisely because recycling works). */
static int arena_owns(Arena *a, const void *p) {
    for (HBlock *b = a->head; b; b = b->next) {
        const char *base = (const char *)(b + 1);
        if ((const char *)p >= base && (const char *)p < base + b->cap) return 1;
    }
    return 0;
}

void *arena_alloc(Arena *a, size_t n) {
    n = (n + 7u) & ~(size_t)7u;             /* 8-byte align (max align of Tycho types: long/double/ptr) */
    size_t k = n >> 3;                       /* 8-byte size class */
    /* reuse a recycled buffer first. Tiny objects: O(1) exact-class pop from the
     * segregated list -- no scan, no cap. Larger objects: best-fit in [n, 2n] over
     * the capped `freelist` (a huge recycled buffer is never wasted on a tiny
     * request -- which would defeat reuse for a geometrically-grown array). Both
     * lists are empty for any arena that never recycles, so this is one predictable
     * branch on the hot path. */
    if (k < TYCHO_NBKT) {
        if (a->bkt && a->bkt[k]) { FreeNode *fn = a->bkt[k]; a->bkt[k] = fn->next; return (void *)fn; }
    } else if (a->freelist) {
        FreeNode **link = &a->freelist, **best = NULL; size_t bestsz = (size_t)-1;
        for (; *link; link = &(*link)->next)
            if ((*link)->size >= n && (*link)->size <= 2u * n && (*link)->size < bestsz) {
                best = link; bestsz = (*link)->size;
            }
        if (best) { FreeNode *fn = *best; *best = fn->next; a->nfree--; return (void *)fn; }
    }
    if (!a->head || a->head->off + n > a->head->cap) {
        size_t cap = n > a->blocksz ? n : a->blocksz;
        HBlock *b = block_get(cap);
        b->next = a->head;
        a->head = b;
    }
    void *p = (char *)(a->head + 1) + a->head->off;
    a->head->off += n;
    if (g_arena_stats) {
        atomic_fetch_add_explicit(&st_alloc_calls, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&st_alloc_bytes, n, memory_order_relaxed);
        st_bump_live(n);   /* recycle-reuse returns earlier and never bumps, so st_live == sum of block offsets */
    }
    return p;
}

/* Reset for reuse (a loop's scratch arena, each iteration): RETAIN the head
 * block and just rewind it (off=0), releasing only any overflow blocks to the
 * pool. The common case -- a loop body whose per-iteration allocations fit in
 * one block -- then does zero pool traffic per iteration, only a pointer rewind. */
void arena_reset(Arena *a) {
    /* chunks live in blocks we're about to rewind/pool, so drop every free-list.
     * Keep the bucket table itself (the arena is being reused) -- just clear its
     * entries; only recycling arenas ever allocated one, so this is skipped wholesale
     * for the common non-recycling scratch arena. */
    if (a->bkt) for (int i = 0; i < TYCHO_NBKT; i++) a->bkt[i] = NULL;
    a->freelist = NULL; a->nfree = 0;
    HBlock *b = a->head;
    if (!b) return;
    if (g_arena_stats) st_drop_live(b);   /* head rewound + overflow pooled: whole chain's live goes to 0 */
    block_release_chain(b->next);   /* overflow blocks -> pool */
    b->next = NULL;
    b->off = 0;                     /* keep & reuse the head block */
}

/* Release the arena entirely (scope/call end): all blocks go to the pool. */
void arena_free(Arena *a) {
    if (g_arena_stats) {
        atomic_fetch_add_explicit(&st_arena_frees, 1, memory_order_relaxed);
        st_drop_live(a->head);
    }
    if (a->bkt) { free(a->bkt); a->bkt = NULL; }   /* release the lazily-allocated table */
    a->freelist = NULL; a->nfree = 0;
    block_release_chain(a->head);
    a->head = NULL;
}

/* ---- tasks (CC-1: `spawn f(args)` / `wait(t)`) --------------------------
 * A task is one OS thread running one tycho function call against a private
 * arena tree rooted at `root`. The thread boundary is tycho's existing call
 * convention: arguments are deep-copied INTO root before the thread starts
 * (the generated spawn site does this), the function's return value lands in
 * root (root is passed as the callee's _parent), and wait() deep-copies the
 * result OUT into the waiting scope's arena before freeing the whole tree.
 * After copy-in, spawner and task share zero bytes -- no locks needed beyond
 * the ones inside pthread create/join. */
typedef struct { pthread_t th; Arena root; void *ret; int done; } HTask;

static HTask *tycho_task_new(void) {
    HTask *t = (HTask *)malloc(sizeof(HTask));
    if (!t) tycho_oom();
    t->root = arena_new(0);
    t->ret = NULL;
    t->done = 0;
    return t;
}

/* Free this thread's block pool back to the OS. Called as the LAST thing a
 * spawned thread does: blocks its scopes released into the thread-local pool
 * would otherwise become unreachable when the thread (and its TLS) dies.
 * Blocks still owned by the task's root tree are NOT in the pool -- they're
 * freed later by wait() on the waiting thread, into that thread's pool. */
static void tycho_pool_flush(void) {
    while (g_block_pool) { HBlock *b = g_block_pool; g_block_pool = b->next; free(b); }
}

/* Bounded concurrency: `spawn` (and each parallel-for chunk) is a 1:1 OS thread,
 * so `spawn` in a loop without joining is an unbounded thread fork-bomb that
 * exhausts the host. Cap the number of CONCURRENTLY-LIVE tasks (started but not
 * yet joined) and fail closed past it. This is a hard ceiling, not a pool: every
 * admitted task still gets its own thread and runs immediately, so a task that
 * blocks on a channel waiting for another task never starves (no queueing). Only
 * a genuine runaway aborts. Default 1024; TYCHO_MAX_TASKS overrides. */
static _Atomic long g_live_tasks = 0;
static long tycho_max_tasks(void) {
    static long m = 0;   /* benign race: every thread computes the same value */
    if (m == 0) { const char *e = getenv("TYCHO_MAX_TASKS"); m = (e && *e && atol(e) >= 1) ? atol(e) : 1024; }
    return m;
}

static void tycho_task_start(HTask *t, void *(*fn)(void *), void *arg) {
    if (atomic_fetch_add(&g_live_tasks, 1) + 1 > tycho_max_tasks()) {
        atomic_fetch_sub(&g_live_tasks, 1);
        fprintf(stderr, "tycho: too many concurrent tasks (max %ld); raise TYCHO_MAX_TASKS to override\n", tycho_max_tasks());
        exit(1);
    }
    if (pthread_create(&t->th, NULL, fn, arg) != 0) {
        atomic_fetch_sub(&g_live_tasks, 1);
        fprintf(stderr, "tycho: spawn failed (cannot create thread)\n");
        exit(1);
    }
}

/* CC-2 affine backstop: a named task's handle struct stays alive until its
 * variable's scope exit (tycho_task_finish below frees it), so a second wait
 * lands HERE on done==1 and dies loudly -- a defined error, never a freed
 * read or a double pthread_join (UB). */
static void tycho_task_join(HTask *t) {
    if (t->done) { fprintf(stderr, "tycho: task already waited\n"); exit(1); }
    pthread_join(t->th, NULL);
    t->done = 1;
    atomic_fetch_sub(&g_live_tasks, 1);   /* reaped: free a slot under the spawn cap */
}

static void tycho_task_free(HTask *t) { arena_free(&t->root); free(t); }

/* CC-2 implicit join, emitted by the compiler at every scope exit (block end,
 * break/continue, early return, proc end) for each task variable dying there:
 * join if never waited, then release everything. arena_free on an already-
 * freed root is a no-op (head/bkt/freelist are NULLed), so a waited task --
 * whose tree was reclaimed eagerly at the wait -- just frees its handle. */
static void tycho_task_finish(HTask *t) {
    if (!t->done) { pthread_join(t->th, NULL); t->done = 1; atomic_fetch_sub(&g_live_tasks, 1); }   /* reap an un-waited task: free its cap slot */
    arena_free(&t->root);
    free(t);
}

/* ---- channels (CC-4: `ch := channel(T, cap)` / send / recv / close) -----
 * Bounded MPMC queue, the ONE intentionally shared object in tycho's
 * concurrency story -- internally synchronized, so value semantics outside
 * it are undisturbed: send deep-copies the payload IN (generated per-type
 * wrapper), recv deep-copies it OUT into the receiver's arena. Payload bytes
 * live in PER-SLOT arenas, each reset when its ring slot is reclaimed by a
 * later send -- so channel memory is bounded by cap * max payload (no
 * monotonic growth). The generic begin/commit pairs below hold the mutex
 * across the generated copy, which keeps slot reuse race-free.
 *
 * Lifetime: the channel is freed at its CREATING scope's exit (emitted by
 * the compiler like a task finish). Sound because CC-2's implicit join means
 * every task that could hold the handle has already joined by then.
 * recv on a closed, drained channel reports 0 (surfaced as None in tycho);
 * send on a closed channel and double close die loudly. */
/* CC-5: the ring is a Vyukov bounded MPMC queue. Each cell carries a sequence
 * counter and its OWN arena: a sender claims a cell with one CAS, deep-copies
 * the payload into the cell arena with NO lock held (the claim makes the cell
 * exclusive until the seq store publishes it), then releases the cell to
 * receivers; a receiver symmetrically claims, copies out, and recycles the
 * cell back to senders. Waiting is a spin -> sched_yield -> timed-park ladder;
 * the parked-waiter COUNT gates the publisher's wake path, so the uncontended
 * fast path does zero syscalls and takes zero locks (the mutex/cond exist only
 * for parking). The 1ms timed wait makes the check-then-park race harmless
 * (worst case one extra retry), never a lost wakeup. Capacity rounds up to a
 * power of two (still bounded; blocking threshold may exceed the request). */
typedef struct __attribute__((aligned(64))) {
    _Atomic long seq;     /* Vyukov sequence: ==pos -> sender may claim; ==pos+1 -> receiver may */
    long  pos;            /* the claim ticket, stashed between claim and commit (cell-exclusive) */
    void *val;
    Arena arena;          /* payload bytes live here from send-copy until the cell is reused */
} HCell;                  /* line-aligned: a consumer's seq store on cell k must not contend with
                           * the producer touching cell k+1 through a straddled line */

typedef struct {
    HCell *cells;
    long   cap;                    /* power of two >= requested capacity */
    /* enq and deq each get their own cache line (Vyukov's original layout):
     * the producer CASes enq while every consumer CASes deq -- packed together
     * they false-share one line and the ping-pong throttles BOTH sides,
     * worst exactly when the ring runs near-empty (consumers outpace the
     * producer) or near-full. */
    char _pad0[64];
    _Atomic long enq;              /* next send ticket */
    char _pad1[64];
    _Atomic long deq;              /* next recv ticket */
    char _pad2[64];
    _Atomic int  closed;
    _Atomic int  waiters;          /* parked threads; >0 makes publishers take the wake slow path */
    pthread_mutex_t mu;            /* parking only -- never held on the data path */
    pthread_cond_t  cv;
} HChan;

static HChan *tycho_chan_new(long cap) {
    if (cap < 1) { fprintf(stderr, "tycho: channel capacity must be >= 1\n"); exit(1); }
    /* Minimum ring size 2: with one cell the published state (seq == pos+1)
     * and the recycled next-lap state (seq == pos+cap) are the same value, so
     * a second send claims the cell before the receiver takes the first value
     * (freeing its payload) and the receiver waits forever. Vyukov's queue
     * requires buffer_size >= 2 for exactly this reason. */
    long c2 = 2;
    while (c2 < cap) c2 <<= 1;
    HChan *ch = (HChan *)malloc(sizeof(HChan));
    if (!ch) tycho_oom();
    ch->cells = (HCell *)aligned_alloc(64, (size_t)c2 * sizeof(HCell));   /* HCell is aligned(64) */
    if (!ch->cells) tycho_oom();
    for (long i = 0; i < c2; i++) {
        atomic_store_explicit(&ch->cells[i].seq, i, memory_order_relaxed);
        ch->cells[i].pos = 0;
        ch->cells[i].val = NULL;
        ch->cells[i].arena = arena_new(0);
    }
    ch->cap = c2;
    atomic_store_explicit(&ch->enq, 0, memory_order_relaxed);
    atomic_store_explicit(&ch->deq, 0, memory_order_relaxed);
    atomic_store_explicit(&ch->closed, 0, memory_order_relaxed);
    atomic_store_explicit(&ch->waiters, 0, memory_order_relaxed);
    pthread_mutex_init(&ch->mu, NULL);
    pthread_cond_init(&ch->cv, NULL);
    return ch;
}

/* Park after the spin budget. The 1ms cap turns the publisher's
 * check-waiters-then-skip race into at most one extra retry. */
static void tycho_chan_park(HChan *ch) {
    pthread_mutex_lock(&ch->mu);
    atomic_fetch_add_explicit(&ch->waiters, 1, memory_order_relaxed);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += 1000000;
    if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
    pthread_cond_timedwait(&ch->cv, &ch->mu, &ts);
    atomic_fetch_sub_explicit(&ch->waiters, 1, memory_order_relaxed);
    pthread_mutex_unlock(&ch->mu);
}

static void tycho_chan_wake(HChan *ch) {        /* publisher slow path: only when someone is parked */
    if (atomic_load_explicit(&ch->waiters, memory_order_relaxed) > 0) {
        pthread_mutex_lock(&ch->mu);
        pthread_cond_signal(&ch->cv);          /* ONE waiter: a commit publishes one cell, so waking
                                                * all N is a thundering herd (N-1 respin + repark per
                                                * item). close() still broadcasts. The timed park
                                                * (1ms) self-heals any wake the signal/park race drops. */
        pthread_mutex_unlock(&ch->mu);
    }
}

/* Claim a send cell: spins, escalates, parks; dies on a closed channel. The
 * returned cell is EXCLUSIVE until tycho_chan_send_commit -- the caller copies
 * the payload into c->arena (already recycled here) with no lock held. */
static HCell *tycho_chan_send_cell(HChan *ch) {
    long pos = atomic_load_explicit(&ch->enq, memory_order_relaxed);
    int spins = 0;
    for (;;) {
        HCell *c = &ch->cells[pos & (ch->cap - 1)];
        long d = atomic_load_explicit(&c->seq, memory_order_acquire) - pos;
        if (d == 0) {
            if (atomic_compare_exchange_weak_explicit(&ch->enq, &pos, pos + 1,
                                                      memory_order_relaxed, memory_order_relaxed)) {
                if (atomic_load_explicit(&ch->closed, memory_order_acquire)) {
                    fprintf(stderr, "tycho: send on a closed channel\n"); exit(1);
                }
                arena_free(&c->arena);          /* recycle the previous payload's bytes */
                c->pos = pos;
                return c;
            }
            /* CAS failure reloaded pos; retry */
        } else if (d < 0) {                     /* full */
            if (atomic_load_explicit(&ch->closed, memory_order_acquire)) {
                fprintf(stderr, "tycho: send on a closed channel\n"); exit(1);
            }
            if (++spins < 256) { /* tight spin */ }
            else if (spins < 512) sched_yield();
            else { tycho_chan_park(ch); spins = 0; }
            pos = atomic_load_explicit(&ch->enq, memory_order_relaxed);
        } else {
            pos = atomic_load_explicit(&ch->enq, memory_order_relaxed);
        }
    }
}

static void tycho_chan_send_commit(HChan *ch, HCell *c, void *val) {
    c->val = val;
    atomic_store_explicit(&c->seq, c->pos + 1, memory_order_release);   /* publish to receivers */
    tycho_chan_wake(ch);
}

/* Claim the next ready cell, or NULL once the channel is closed AND drained
 * (no committed value at deq and nothing in flight: enq == deq). The cell is
 * exclusive until tycho_chan_recv_commit -- copy the value out first. */
static HCell *tycho_chan_recv_cell(HChan *ch) {
    long pos = atomic_load_explicit(&ch->deq, memory_order_relaxed);
    int spins = 0;
    for (;;) {
        HCell *c = &ch->cells[pos & (ch->cap - 1)];
        long d = atomic_load_explicit(&c->seq, memory_order_acquire) - (pos + 1);
        if (d == 0) {
            if (atomic_compare_exchange_weak_explicit(&ch->deq, &pos, pos + 1,
                                                      memory_order_relaxed, memory_order_relaxed)) {
                c->pos = pos;
                return c;
            }
        } else if (d < 0) {                     /* nothing committed at pos */
            if (atomic_load_explicit(&ch->closed, memory_order_acquire)
                && atomic_load_explicit(&ch->enq, memory_order_acquire) == pos)
                return NULL;                    /* closed + drained */
            if (++spins < 256) { /* tight spin */ }
            else if (spins < 512) sched_yield();
            else { tycho_chan_park(ch); spins = 0; }
            pos = atomic_load_explicit(&ch->deq, memory_order_relaxed);
        } else {
            pos = atomic_load_explicit(&ch->deq, memory_order_relaxed);
        }
    }
}

static void tycho_chan_recv_commit(HChan *ch, HCell *c) {
    atomic_store_explicit(&c->seq, c->pos + ch->cap, memory_order_release);   /* recycle to senders */
    tycho_chan_wake(ch);
}

/* Non-blocking receive for `select`: 1 = got a cell (commit it after the
 * copy-out), 0 = open but empty right now, 2 = closed and drained. */
static int tycho_chan_try_recv(HChan *ch, HCell **out) {
    long pos = atomic_load_explicit(&ch->deq, memory_order_relaxed);
    for (;;) {
        HCell *c = &ch->cells[pos & (ch->cap - 1)];
        long d = atomic_load_explicit(&c->seq, memory_order_acquire) - (pos + 1);
        if (d == 0) {
            if (atomic_compare_exchange_weak_explicit(&ch->deq, &pos, pos + 1,
                                                      memory_order_relaxed, memory_order_relaxed)) {
                c->pos = pos;
                *out = c;
                return 1;
            }
        } else if (d < 0) {
            if (atomic_load_explicit(&ch->closed, memory_order_acquire)
                && atomic_load_explicit(&ch->enq, memory_order_acquire) == pos)
                return 2;
            return 0;
        } else {
            pos = atomic_load_explicit(&ch->deq, memory_order_relaxed);
        }
    }
}

/* select's wait ladder when every arm is open-but-empty: tight retries, then
 * yields, then a bounded sleep -- a select cannot park on N condvars, so this
 * caps both the idle CPU burn and the worst-case wake latency (~50us). */
static void tycho_select_pause(int *spins) {
    (*spins)++;
    if (*spins < 64) return;
    if (*spins < 256) { sched_yield(); return; }
    struct timespec ts = { 0, 50000 };
    nanosleep(&ts, NULL);
}

static void tycho_chan_close(HChan *ch) {
    pthread_mutex_lock(&ch->mu);
    if (atomic_load_explicit(&ch->closed, memory_order_relaxed)) {
        fprintf(stderr, "tycho: channel already closed\n"); exit(1);
    }
    atomic_store_explicit(&ch->closed, 1, memory_order_release);
    pthread_cond_broadcast(&ch->cv);            /* receivers drain then see None; senders die loudly */
    pthread_mutex_unlock(&ch->mu);
}

static void tycho_chan_free(HChan *ch) {
    for (long i = 0; i < ch->cap; i++) arena_free(&ch->cells[i].arena);
    free(ch->cells);
    pthread_mutex_destroy(&ch->mu);
    pthread_cond_destroy(&ch->cv);
    free(ch);
}

/* CC-3 parallel for: how many chunk tasks to fan out. TYCHO_THREADS overrides
 * (useful for benchmarks and for pinning tests); otherwise online CPU count.
 * Integer +,* reductions are chunk-count-independent (associative, exact), so
 * results never depend on this value -- float reductions may reassociate. */
static long tycho_ncpu(void) {
    const char *e = getenv("TYCHO_THREADS");
    if (e && *e) { long v = atol(e); if (v >= 1) return v; }
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? n : 1;
}

/* Allocate a string with `n` data bytes: an 8-byte length header sits just
 * before the returned pointer (tycho_str_len reads it in O(1)), the data is
 * NUL-terminated, and the pointer is a valid C `char *` (the header is hidden
 * "behind" it, so printf/strcmp/strstr stay unaffected). 8-byte arena alignment
 * keeps the header word aligned. */
static char *tycho_str_alloc(Arena *a, long n) {
    /* guard a negative/corrupted length: (size_t)(8 + n + 1) would otherwise
     * wrap to a huge request. Lengths are 64-bit, so a positive value can't
     * overflow the size_t computation on a 64-bit host. */
    if (n < 0) {
        fprintf(stderr, "tycho: string length %ld out of range\n", n);
        exit(1);
    }
    char *base = (char *)arena_alloc(a, (size_t)(8 + n + 1));
    *(long *)base = n;
    char *data = base + 8;
    data[n] = '\0';
    return data;
}

char *tycho_str_concat(Arena *a, const char *x, const char *y) {
    long lx = ((const long *)x)[-1], ly = ((const long *)y)[-1];   /* header lengths: byte-safe (interior NUL preserved) */
    char *r = tycho_str_alloc(a, lx + ly);
    memcpy(r, x, (size_t)lx);
    memcpy(r + lx, y, (size_t)ly);
    return r;
}

/* Multi-piece concat: the compiler flattens an all-string `a + b + c [+ d [+ e]]`
 * chain to ONE of these instead of N-2 chained tycho_str_concat calls, so a
 * k-piece concat does ONE allocation + one copy per piece (no throwaway
 * intermediates, no re-copying the growing prefix). */
#define TYCHO_SLEN(s) (((const long *)(s))[-1])
char *tycho_str_concat3(Arena *a, const char *s0, const char *s1, const char *s2) {
    long n0 = TYCHO_SLEN(s0), n1 = TYCHO_SLEN(s1), n2 = TYCHO_SLEN(s2);
    char *r = tycho_str_alloc(a, n0 + n1 + n2), *p = r;
    memcpy(p, s0, (size_t)n0); p += n0; memcpy(p, s1, (size_t)n1); p += n1; memcpy(p, s2, (size_t)n2);
    return r;
}
char *tycho_str_concat4(Arena *a, const char *s0, const char *s1, const char *s2, const char *s3) {
    long n0 = TYCHO_SLEN(s0), n1 = TYCHO_SLEN(s1), n2 = TYCHO_SLEN(s2), n3 = TYCHO_SLEN(s3);
    char *r = tycho_str_alloc(a, n0 + n1 + n2 + n3), *p = r;
    memcpy(p, s0, (size_t)n0); p += n0; memcpy(p, s1, (size_t)n1); p += n1; memcpy(p, s2, (size_t)n2); p += n2; memcpy(p, s3, (size_t)n3);
    return r;
}
char *tycho_str_concat5(Arena *a, const char *s0, const char *s1, const char *s2, const char *s3, const char *s4) {
    long n0 = TYCHO_SLEN(s0), n1 = TYCHO_SLEN(s1), n2 = TYCHO_SLEN(s2), n3 = TYCHO_SLEN(s3), n4 = TYCHO_SLEN(s4);
    char *r = tycho_str_alloc(a, n0 + n1 + n2 + n3 + n4), *p = r;
    memcpy(p, s0, (size_t)n0); p += n0; memcpy(p, s1, (size_t)n1); p += n1; memcpy(p, s2, (size_t)n2); p += n2; memcpy(p, s3, (size_t)n3); p += n3; memcpy(p, s4, (size_t)n4);
    return r;
}
char *tycho_str_concat6(Arena *a, const char *s0, const char *s1, const char *s2, const char *s3, const char *s4, const char *s5) {
    long n0 = TYCHO_SLEN(s0), n1 = TYCHO_SLEN(s1), n2 = TYCHO_SLEN(s2), n3 = TYCHO_SLEN(s3), n4 = TYCHO_SLEN(s4), n5 = TYCHO_SLEN(s5);
    char *r = tycho_str_alloc(a, n0 + n1 + n2 + n3 + n4 + n5), *p = r;
    memcpy(p, s0, (size_t)n0); p += n0; memcpy(p, s1, (size_t)n1); p += n1; memcpy(p, s2, (size_t)n2); p += n2; memcpy(p, s3, (size_t)n3); p += n3; memcpy(p, s4, (size_t)n4); p += n4; memcpy(p, s5, (size_t)n5);
    return r;
}

/* In-place string append for the accumulator pattern `acc = acc + e`. The
 * compiler emits this only for a uniquely-owned local string it tracks with
 * sidecar len/cap, so growing the buffer in place is sound (value semantics
 * guarantees no alias observes the mutation). Geometric growth makes a loop
 * of appends amortized O(1) each -> O(N) total time and memory, vs the O(N^2)
 * of repeated tycho_str_concat. Mirrors tycho_arr_int_push. The new buffer
 * lives in arena `a` (the variable's owning arena, not the caller's scratch).
 * memmove, not memcpy: e may alias *s (acc = acc + acc). */
void tycho_str_append(Arena *a, char **s, long *len, long *cap, const char *e) {
    long el = ((const long *)e)[-1];   /* header length: byte-safe */
    long need = *len + el + 1;
    if (need > *cap) {
        long nc = *cap ? *cap * 2 : 16;
        while (nc < need) nc *= 2;
        char *base = (char *)arena_alloc(a, (size_t)(8 + nc));   /* +8 for the length header */
        char *nb = base + 8;
        memcpy(nb, *s, (size_t)*len);    /* old buffer still live (bump arena) */
        *s = nb;
        *cap = nc;
    }
    memmove(*s + *len, e, (size_t)el);
    *len += el;
    (*s)[*len] = '\0';
    ((long *)(*s))[-1] = *len;            /* keep the length header in sync (always grown by here) */
}

/* string + char: one-byte append, no strlen/snprintf. `c` is a byte carried in
 * a long (Tycho's char type). New buffer lives in arena `a` (cf. tycho_str_concat). */
char *tycho_str_concat_char(Arena *a, const char *x, long c) {
    long lx = ((const long *)x)[-1];   /* header length: byte-safe */
    char *r = tycho_str_alloc(a, lx + 1);
    memcpy(r, x, (size_t)lx);
    r[lx] = (char)c;
    return r;
}

/* In-place one-byte append for the accumulator `acc = acc + c` where c is a
 * char. Same uniqueness/geometric-growth contract as tycho_str_append. */
void tycho_str_append_char(Arena *a, char **s, long *len, long *cap, long c) {
    long need = *len + 2;
    if (need > *cap) {
        long nc = *cap ? *cap * 2 : 16;
        while (nc < need) nc *= 2;
        char *base = (char *)arena_alloc(a, (size_t)(8 + nc));
        char *nb = base + 8;
        memcpy(nb, *s, (size_t)*len);
        *s = nb;
        *cap = nc;
    }
    (*s)[*len] = (char)c;
    *len += 1;
    (*s)[*len] = '\0';
    ((long *)(*s))[-1] = *len;
}

/* value-semantic copy of a string into arena `a`. Used when a bare string
 * variable is returned or assigned to an outer scope: the variable is only
 * a pointer into a scope about to be freed, so the bytes must be copied
 * into the destination arena to survive (cf. tycho_arr_int_copy). */
char *tycho_str_copy(Arena *a, const char *s) {
    long n = ((const long *)s)[-1];   /* header length: a value-semantic copy of a Tycho string preserves every byte */
    char *r = tycho_str_alloc(a, n);
    memcpy(r, s, (size_t)n);
    return r;
}

/* Ingest a bare C string (NO length header — getenv, argv, a C-library return)
 * into a length-headered Tycho string. strlen-bounded, because a C string ends at
 * its first NUL by definition. Use this at every boundary where foreign bytes
 * enter Tycho's string world; tycho_str_copy above is for Tycho strings only. */
char *tycho_str_from_c(Arena *a, const char *s) {
    size_t n = strlen(s);
    char *r = tycho_str_alloc(a, (long)n);
    memcpy(r, s, n);
    return r;
}

/* FFI: copy a C-owned (ptr,len) byte buffer returned by an extern into an arena
 * `bytes` value (same length-headered repr as string), then free() the C buffer.
 * Out-param-shim convention: the extern malloc's *out, Tycho copies + frees it.
 * NULL/empty -> a zero-length bytes. Length-carried, so interior 0x00 is fine. */
char *tycho_bytes_from_c(Arena *a, unsigned char *p, long n) {
    if (n < 0) n = 0;
    char *r = tycho_str_alloc(a, p ? n : 0);
    if (p) { if (n > 0) memcpy(r, p, (size_t)n); free(p); }
    return r;
}

/* Intern a string literal: a malloc'd, length-headered, immortal copy. Each
 * literal occurrence caches the result in a function-local static (see the
 * codegen), so the strlen+copy runs once; the allocation stays reachable for the
 * process lifetime (never an LSan leak). */
char *tycho_str_intern(const char *s) {
    size_t n = strlen(s);
    char *base = (char *)malloc(8 + n + 1);
    if (!base) tycho_oom();
    *(long *)base = (long)n;
    char *data = base + 8;
    memcpy(data, s, n + 1);   /* bytes + NUL */
    return data;
}

void tycho_print(const char *s) { fputs(s, stdout); }   /* bare C string: codegen newline, FFI read-once borrow */
void tycho_print_s(const char *s) { fwrite(s, 1, (size_t)((const long *)s)[-1], stdout); }   /* a Tycho string: all bytes, incl. interior NUL */
void tycho_eprint(const char *s) { fputs(s, stderr); }   /* non-fatal stderr write (warnings) */

/* --- string builtins ------------------------------------------------------
 * Strings are NUL-terminated byte buffers (char *). len/index are byte-
 * oriented. substr returns a fresh copy in the target arena (value
 * semantics, like everything else); its range is clamped Python-style. */

long tycho_str_len(const char *s) { return ((const long *)s)[-1]; }   /* O(1): the length header */

/* Byte-safe lexicographic compare: -1/0/1, using the length headers (NOT NUL
 * termination) so a string with an interior '\0' (e.g. from read_file of a binary
 * file) compares by its true bytes. Replaces strcmp everywhere a Tycho string is
 * compared (==, !=, <, >, <=, >=, map keys, array equality). */
int tycho_str_cmp(const char *a, const char *b) {
    long la = ((const long *)a)[-1], lb = ((const long *)b)[-1];
    long n = la < lb ? la : lb;
    int c = n ? memcmp(a, b, (size_t)n) : 0;
    if (c != 0) return c < 0 ? -1 : 1;
    return la < lb ? -1 : (la > lb ? 1 : 0);
}

long tycho_str_get(const char *s, long i) {
    long n = ((const long *)s)[-1];
    if (i < 0 || i >= n) {
        fprintf(stderr, "tycho: string index %ld out of bounds (len %ld)\n", i, n);
        exit(1);
    }
    return (long)(unsigned char)s[i];   /* unsigned: 0..255, never negative */
}

/* Same bounds-checked byte read, but the caller passes the length — used when the
 * codegen has hoisted strlen(s) into a sidecar for an indexed, never-reassigned
 * string (its length is loop-invariant). Turns an O(n)-per-access bounds check
 * into O(1), so indexing a string in a loop is O(n) not O(n^2). */
long tycho_str_get_n(const char *s, long i, long n) {
    if (i < 0 || i >= n) {
        fprintf(stderr, "tycho: string index %ld out of bounds (len %ld)\n", i, n);
        exit(1);
    }
    return (long)(unsigned char)s[i];
}

/* substring [start, end); out-of-range bounds are clamped, not an error */
char *tycho_str_substr(Arena *a, const char *s, long start, long end) {
    long n = ((const long *)s)[-1];   /* header length: byte-safe */
    if (start < 0) start = 0;
    if (end > n) end = n;
    if (end < start) end = start;
    long m = end - start;
    char *r = tycho_str_alloc(a, m);
    memcpy(r, s + start, (size_t)m);
    return r;
}

/* byte index of the first occurrence of sub in s, or -1 if absent. Byte-safe:
 * scans by the length headers, not NUL, so interior '\0' bytes are matched. */
long tycho_str_find(const char *s, const char *sub) {
    long ls = ((const long *)s)[-1], lsub = ((const long *)sub)[-1];
    if (lsub == 0) return 0;
    for (long i = 0; i + lsub <= ls; i++)
        if (memcmp(s + i, sub, (size_t)lsub) == 0) return i;
    return -1;
}

char *tycho_input(Arena *a) {
    size_t cap = 128, len = 0;
    char *buf = (char *)arena_alloc(a, cap);
    int c;
    fflush(stdout);                          /* flush any prompt first */
    while ((c = getchar()) != EOF && c != '\n') {
        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            char *nb = (char *)arena_alloc(a, ncap);
            memcpy(nb, buf, len);
            buf = nb;
            cap = ncap;
        }
        buf[len++] = (char)c;
    }
    char *r = tycho_str_alloc(a, (long)len);
    memcpy(r, buf, len);
    return r;
}

/* read ALL of stdin into one string (the whole source file), newlines and all.
 * Unlike tycho_input (one line, can't tell EOF from a blank line), this is what
 * a source-to-source tool needs: `tychoc-tycho < src.ty > out.c`. */
char *tycho_read_all(Arena *a) {
    size_t cap = 4096, len = 0;
    char *buf = (char *)arena_alloc(a, cap);
    int c;
    while ((c = getchar()) != EOF) {
        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            char *nb = (char *)arena_alloc(a, ncap);
            memcpy(nb, buf, len);
            buf = nb;
            cap = ncap;
        }
        buf[len++] = (char)c;
    }
    char *r = tycho_str_alloc(a, (long)len);
    memcpy(r, buf, len);
    return r;
}

/* clock(): monotonic nanoseconds since an arbitrary epoch -- for measuring
 * elapsed time (diffs are meaningful; the absolute value is not). now():
 * wall-clock seconds since the UNIX epoch -- for timestamps. Both return Tycho
 * `int` (C long, 64-bit), so ns fit for ~292 years. */
long tycho_clock(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000000000L + (long)ts.tv_nsec;
}
long tycho_now(void) { return (long)time(NULL); }

/* write_file(path, data): write data's exact bytes (length from the string
 * header, so embedded NULs survive) to path, truncating it. 1 on success, 0 if
 * the file couldn't be opened or the write was short. */
long tycho_write_file(const char *path, const char *data) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    long n = tycho_str_len(data);
    size_t w = fwrite(data, 1, (size_t)n, f);
    fclose(f);
    return (w == (size_t)n) ? 1 : 0;
}

/* getenv(name): the environment variable's value as a string, or "" if unset. */
char *tycho_getenv(Arena *a, const char *name) {
    const char *v = getenv(name);
    return tycho_str_from_c(a, v ? v : "");   /* env value is a bare C string */
}

/* read_file(path): the whole file as a string, or "" if it can't be opened. */
char *tycho_read_file(Arena *a, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return tycho_str_from_c(a, "");
    size_t cap = 4096, len = 0;
    char *buf = (char *)arena_alloc(a, cap);
    size_t n;
    while ((n = fread(buf + len, 1, cap - len, f)) > 0) {
        len += n;
        if (len == cap) {
            size_t ncap = cap * 2;
            char *nb = (char *)arena_alloc(a, ncap);
            memcpy(nb, buf, len);
            buf = nb;
            cap = ncap;
        }
    }
    fclose(f);
    char *r = tycho_str_alloc(a, (long)len);
    memcpy(r, buf, len);
    return r;
}

/* args(): the program's argv as a string array (args()[0] is the program name).
 * The emitted `main` stashes argc/argv here. */
static int    tycho_argc = 0;
static char **tycho_argv = NULL;

/* chr(n): the one-byte string for byte value n (0..255) — the inverse of the
 * `s[i] -> int` byte read. A value outside 0..255 is a program error and aborts
 * cleanly (like a bad index), rather than silently masking to a byte. n==0 is a
 * real NUL byte, one byte long: strings are byte-safe. */
char *tycho_chr(Arena *a, long n) {
    if (n < 0 || n > 255) { fprintf(stderr, "tycho: chr(%ld) out of byte range 0..255\n", n); exit(1); }
    char *r = tycho_str_alloc(a, 1);
    r[0] = (char)n;
    return r;
}

/* die(msg): the error path for a Tycho-written compiler (and any tool) — print
 * to stderr and exit non-zero. Never returns; declared T_VOID, so a non-void
 * function that dies in a branch still gets its defensive fallback return. */
void tycho_die(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

char *tycho_int_to_str(Arena *a, long n) {
    /* hand-rolled itoa: no snprintf format parsing. Digits written backward then
     * copied out. Unsigned magnitude so LONG_MIN negates without UB. */
    char tmp[24];
    int i = (int)sizeof tmp;
    unsigned long u = n < 0 ? -(unsigned long)n : (unsigned long)n;
    do { tmp[--i] = (char)('0' + u % 10); u /= 10; } while (u);
    if (n < 0) tmp[--i] = '-';
    int m = (int)sizeof tmp - i;
    char *r = tycho_str_alloc(a, m);
    memcpy(r, tmp + i, (size_t)m);
    return r;
}

/* Unsigned to string (u32/u64): plain decimal, no sign. */
char *tycho_uint_to_str(Arena *a, unsigned long long u) {
    char tmp[24];
    int i = (int)sizeof tmp;
    do { tmp[--i] = (char)('0' + u % 10); u /= 10; } while (u);
    int m = (int)sizeof tmp - i;
    char *r = tycho_str_alloc(a, m);
    memcpy(r, tmp + i, (size_t)m);
    return r;
}

/* Bool to string: the words "true"/"false" (not 1/0). A bool is carried as a
 * long 0/1, so any non-zero prints "true". */
char *tycho_bool_to_str(Arena *a, long b) {
    const char *s = b ? "true" : "false";
    int m = (int)strlen(s);
    char *r = tycho_str_alloc(a, m);
    memcpy(r, s, (size_t)m);
    return r;
}

/* Float to string: %.15g trims trailing zeros while keeping ~15 significant
 * digits (readable, not full 17-digit round-trip). A value that prints with no
 * '.', exponent, or inf/nan marker (e.g. 3 for 3.0) gets a trailing ".0" so it
 * is never mistaken for an int. */
char *tycho_float_to_str(Arena *a, double x) {
    char tmp[64];
    int m = snprintf(tmp, sizeof tmp, "%.15g", x);
    int floaty = 0;
    for (int i = 0; i < m; i++) {
        char c = tmp[i];
        if (c == '.' || c == 'e' || c == 'E' || c == 'n' || c == 'N' || c == 'i' || c == 'I') { floaty = 1; break; }
    }
    if (!floaty && m + 2 < (int)sizeof tmp) { tmp[m++] = '.'; tmp[m++] = '0'; tmp[m] = '\0'; }
    char *r = tycho_str_alloc(a, m);
    memcpy(r, tmp, (size_t)m);
    return r;
}

/* --- [int] arrays ---------------------------------------------------------
 * A TychoArrInt is a value (passed/copied by value, 3 words). Its backing
 * buffer lives in the arena that owns the variable holding it; growth
 * allocates a fresh, larger buffer in that arena (old one wasted but
 * bounded by geometric growth, reclaimed when the arena ends). */

typedef struct { long *data; long len; long cap; } TychoArrInt;

TychoArrInt tycho_arr_int_with_cap(Arena *a, long cap) {
    TychoArrInt r;
    r.len = 0;
    r.cap = cap;
    r.data = cap > 0 ? (long *)arena_alloc(a, (size_t)cap * sizeof(long)) : NULL;
    return r;
}

/* Build a bytes value from an [int] array, each element truncated to a byte
 * (`& 0xFF`). The dual of `s[i]` reads: lets pure Tycho assemble a binary buffer
 * -- interior 0x00 included -- that a string can't hold. `to_bytes([int])`. */
char *tycho_bytes_from_intarr(Arena *a, TychoArrInt arr) {
    long n = arr.len; if (n < 0) n = 0;
    unsigned char *p = NULL;
    if (n > 0) {
        p = (unsigned char *)malloc((size_t)n);
        if (!p) tycho_oom();
        for (long i = 0; i < n; i++) p[i] = (unsigned char)(arr.data[i] & 0xFF);
    }
    return tycho_bytes_from_c(a, p, n);
}

/* FFI: copy a C-owned (ptr, len) long buffer (from an extern `-> [int]`, out-param
 * shim) into an arena array, then free it -- the [int] analogue of
 * tycho_bytes_from_c. NULL p yields the empty array (nothing to free). */
TychoArrInt tycho_arr_int_from_c(Arena *a, long *p, long n) {
    if (n < 0) n = 0;
    TychoArrInt r = tycho_arr_int_with_cap(a, p ? n : 0);
    if (p) { if (n > 0) memcpy(r.data, p, (size_t)n * sizeof(long)); r.len = n; free(p); }
    return r;
}

/* Preallocate to exact capacity `n` (no-op if already that big). Lets a caller
 * that knows the final size build a list with ZERO geometric growth -- no
 * abandoned buffers, no 2x slack -- the arena-friendly way to build a long-lived,
 * many-list structure (see bench/invindex). */
void tycho_arr_int_reserve(Arena *a, TychoArrInt *xs, long n) {
    if (n <= xs->cap) return;
    tycho_cap_check(n, sizeof(long));
    long *nd = (long *)arena_alloc(a, (size_t)n * sizeof(long));
    if (xs->len) memcpy(nd, xs->data, (size_t)xs->len * sizeof(long));
    if (xs->cap) arena_recycle(a, xs->data, (size_t)xs->cap * sizeof(long));
    xs->data = nd; xs->cap = n;
}

void tycho_arr_int_push(Arena *a, TychoArrInt *xs, long v) {
    if (xs->len == xs->cap) {
        long ncap = xs->cap ? xs->cap * 2 : 4;
        long *nd = (long *)arena_alloc(a, (size_t)ncap * sizeof(long));
        if (xs->len) memcpy(nd, xs->data, (size_t)xs->len * sizeof(long));
        if (xs->cap) arena_recycle(a, xs->data, (size_t)xs->cap * sizeof(long));  /* old buffer is dead+unique */
        xs->data = nd;
        xs->cap = ncap;
    }
    xs->data[xs->len++] = v;
}

/* pop: shrink + return the last element. int/float pass through (no heap); the
 * arena is unused. Dies on an empty array (a value must always be returned). */
long tycho_arr_int_pop(Arena *a, TychoArrInt *xs) {
    (void)a;
    if (xs->len == 0) { fprintf(stderr, "tycho: pop from an empty array\n"); exit(1); }
    return xs->data[--xs->len];
}

long tycho_arr_int_get(TychoArrInt xs, long i) {
    if (i < 0 || i >= xs.len) {
        fprintf(stderr, "tycho: index %ld out of bounds (len %ld)\n", i, xs.len);
        exit(1);
    }
    return xs.data[i];
}

void tycho_arr_int_set(TychoArrInt *xs, long i, long v) {
    if (i < 0 || i >= xs->len) {
        fprintf(stderr, "tycho: index %ld out of bounds (len %ld)\n", i, xs->len);
        exit(1);
    }
    xs->data[i] = v;
}

/* Grow hook for push-loop fusion: when a loop pushes to a local array, codegen
 * caches data/cap/len in C locals (registers) and writes elements directly --
 * the array descriptor never goes through memory in the hot path. This is the
 * slow path, called only when the cached cursor is full; it reallocs in `a` and
 * updates the caller's cached *data and *cap (geometric, recycling the old buffer --
 * same policy as tycho_arr_int_push). */
void tycho_arr_int_grow(Arena *a, long **data, long *cap, long len) {
    long nc = *cap ? *cap * 2 : 4;
    long *nd = (long *)arena_alloc(a, (size_t)nc * sizeof(long));
    if (len) memcpy(nd, *data, (size_t)len * sizeof(long));
    if (*cap) arena_recycle(a, *data, (size_t)*cap * sizeof(long));
    *data = nd; *cap = nc;
}

/* value-semantic copy: independent buffer in arena `a` */
TychoArrInt tycho_arr_int_copy(Arena *a, TychoArrInt src) {
    TychoArrInt r = tycho_arr_int_with_cap(a, src.len);
    r.len = src.len;
    if (src.len) memcpy(r.data, src.data, (size_t)src.len * sizeof(long));
    return r;
}

/* structural equality: same length and equal elements (value semantics) */
int tycho_arr_int_eq(TychoArrInt x, TychoArrInt y) {
    if (x.len != y.len) return 0;
    for (long i = 0; i < x.len; i++)
        if (x.data[i] != y.data[i]) return 0;
    return 1;
}

/* --- [float] arrays -------------------------------------------------------
 * Exactly TychoArrInt with double elements (a value word, no heap nesting), so
 * every op mirrors the int array. Equality is bitwise via ==, with the usual
 * float caveats. */

typedef struct { double *data; long len; long cap; } TychoArrFloat;

TychoArrFloat tycho_arr_float_with_cap(Arena *a, long cap) {
    TychoArrFloat r;
    r.len = 0;
    r.cap = cap;
    r.data = cap > 0 ? (double *)arena_alloc(a, (size_t)cap * sizeof(double)) : NULL;
    return r;
}

/* FFI: copy a C-owned (ptr, len) double buffer (from an extern `-> [float]`) into
 * an arena array, then free it. NULL p yields the empty array. */
TychoArrFloat tycho_arr_float_from_c(Arena *a, double *p, long n) {
    if (n < 0) n = 0;
    TychoArrFloat r = tycho_arr_float_with_cap(a, p ? n : 0);
    if (p) { if (n > 0) memcpy(r.data, p, (size_t)n * sizeof(double)); r.len = n; free(p); }
    return r;
}

void tycho_arr_float_reserve(Arena *a, TychoArrFloat *xs, long n) {
    if (n <= xs->cap) return;
    tycho_cap_check(n, sizeof(double));
    double *nd = (double *)arena_alloc(a, (size_t)n * sizeof(double));
    if (xs->len) memcpy(nd, xs->data, (size_t)xs->len * sizeof(double));
    if (xs->cap) arena_recycle(a, xs->data, (size_t)xs->cap * sizeof(double));
    xs->data = nd; xs->cap = n;
}

void tycho_arr_float_push(Arena *a, TychoArrFloat *xs, double v) {
    if (xs->len == xs->cap) {
        long ncap = xs->cap ? xs->cap * 2 : 4;
        double *nd = (double *)arena_alloc(a, (size_t)ncap * sizeof(double));
        if (xs->len) memcpy(nd, xs->data, (size_t)xs->len * sizeof(double));
        if (xs->cap) arena_recycle(a, xs->data, (size_t)xs->cap * sizeof(double));  /* old buffer is dead+unique */
        xs->data = nd;
        xs->cap = ncap;
    }
    xs->data[xs->len++] = v;
}

/* push-loop fusion grow hook (see tycho_arr_int_grow) */
void tycho_arr_float_grow(Arena *a, double **data, long *cap, long len) {
    long nc = *cap ? *cap * 2 : 4;
    double *nd = (double *)arena_alloc(a, (size_t)nc * sizeof(double));
    if (len) memcpy(nd, *data, (size_t)len * sizeof(double));
    if (*cap) arena_recycle(a, *data, (size_t)*cap * sizeof(double));
    *data = nd; *cap = nc;
}

double tycho_arr_float_pop(Arena *a, TychoArrFloat *xs) {
    (void)a;
    if (xs->len == 0) { fprintf(stderr, "tycho: pop from an empty array\n"); exit(1); }
    return xs->data[--xs->len];
}

double tycho_arr_float_get(TychoArrFloat xs, long i) {
    if (i < 0 || i >= xs.len) {
        fprintf(stderr, "tycho: index %ld out of bounds (len %ld)\n", i, xs.len);
        exit(1);
    }
    return xs.data[i];
}

void tycho_arr_float_set(TychoArrFloat *xs, long i, double v) {
    if (i < 0 || i >= xs->len) {
        fprintf(stderr, "tycho: index %ld out of bounds (len %ld)\n", i, xs->len);
        exit(1);
    }
    xs->data[i] = v;
}

TychoArrFloat tycho_arr_float_copy(Arena *a, TychoArrFloat src) {
    TychoArrFloat r = tycho_arr_float_with_cap(a, src.len);
    r.len = src.len;
    if (src.len) memcpy(r.data, src.data, (size_t)src.len * sizeof(double));
    return r;
}

int tycho_arr_float_eq(TychoArrFloat x, TychoArrFloat y) {
    if (x.len != y.len) return 0;
    for (long i = 0; i < x.len; i++)
        if (x.data[i] != y.data[i]) return 0;
    return 1;
}

/* --- [string] arrays ------------------------------------------------------
 * Like TychoArrInt, but the elements are char* whose bytes live in an arena.
 * The lifetime seam (see tycho_str_copy) nests here: every operation that
 * moves a string *into* the array (push/set/literal/copy) must copy the
 * bytes into the array's owning arena, and copying the array must deep-copy
 * each element too — otherwise a promoted array keeps pointers into a freed
 * scope. */

typedef struct { char **data; long len; long cap; } TychoArrStr;

TychoArrStr tycho_arr_str_with_cap(Arena *a, long cap) {
    TychoArrStr r;
    r.len = 0;
    r.cap = cap;
    r.data = cap > 0 ? (char **)arena_alloc(a, (size_t)cap * sizeof(char *)) : NULL;
    return r;
}

void tycho_arr_str_reserve(Arena *a, TychoArrStr *xs, long n) {
    if (n <= xs->cap) return;
    tycho_cap_check(n, sizeof(char *));
    char **nd = (char **)arena_alloc(a, (size_t)n * sizeof(char *));
    if (xs->len) memcpy(nd, xs->data, (size_t)xs->len * sizeof(char *));
    if (xs->cap) arena_recycle(a, xs->data, (size_t)xs->cap * sizeof(char *));
    xs->data = nd; xs->cap = n;
}

void tycho_arr_str_push(Arena *a, TychoArrStr *xs, const char *v) {
    if (xs->len == xs->cap) {
        long ncap = xs->cap ? xs->cap * 2 : 4;
        char **nd = (char **)arena_alloc(a, (size_t)ncap * sizeof(char *));
        if (xs->len) memcpy(nd, xs->data, (size_t)xs->len * sizeof(char *));
        if (xs->cap) arena_recycle(a, xs->data, (size_t)xs->cap * sizeof(char *));  /* dead spine; strings live on via nd */
        xs->data = nd;
        xs->cap = ncap;
    }
    xs->data[xs->len++] = tycho_str_copy(a, v);   /* copy bytes into owner arena */
}

/* push-loop fusion grow hook (see tycho_arr_int_grow): regrows the SPINE (the
 * char* pointer buffer) in `a`; the strings it points to were already copied
 * into `a` at each fused store, so the shallow pointer memcpy keeps them. */
void tycho_arr_str_grow(Arena *a, char ***data, long *cap, long len) {
    long nc = *cap ? *cap * 2 : 4;
    char **nd = (char **)arena_alloc(a, (size_t)nc * sizeof(char *));
    if (len) memcpy(nd, *data, (size_t)len * sizeof(char *));
    if (*cap) arena_recycle(a, *data, (size_t)*cap * sizeof(char *));
    *data = nd; *cap = nc;
}

char *tycho_arr_str_pop(Arena *a, TychoArrStr *xs) {
    if (xs->len == 0) { fprintf(stderr, "tycho: pop from an empty array\n"); exit(1); }
    xs->len--;
    return tycho_str_copy(a, xs->data[xs->len]);   /* deep-copy out: survives a later push/recycle */
}

char *tycho_arr_str_get(TychoArrStr xs, long i) {
    if (i < 0 || i >= xs.len) {
        fprintf(stderr, "tycho: index %ld out of bounds (len %ld)\n", i, xs.len);
        exit(1);
    }
    return xs.data[i];
}

/* Bounds-checked element-pointer projection for the SCALAR array families, so an
 * `inout` on a scalar element (`inc(&a[i])`) has a mutable lvalue -- the composite
 * arrays already emit a per-type tycho_arr_C<id>_ptr for this, but the built-in
 * int/float/str arrays had none, so gen_lvalue emitted a bogus tycho_arr_C<garbage>
 * _ptr and the C failed to compile. Mirrors the composite _ptr contract. */
long *tycho_arr_int_ptr(TychoArrInt *xs, long i) {
    if (i < 0 || i >= xs->len) { fprintf(stderr, "tycho: index %ld out of bounds (len %ld)\n", i, xs->len); exit(1); }
    return &xs->data[i];
}
double *tycho_arr_float_ptr(TychoArrFloat *xs, long i) {
    if (i < 0 || i >= xs->len) { fprintf(stderr, "tycho: index %ld out of bounds (len %ld)\n", i, xs->len); exit(1); }
    return &xs->data[i];
}
char **tycho_arr_str_ptr(TychoArrStr *xs, long i) {
    if (i < 0 || i >= xs->len) { fprintf(stderr, "tycho: index %ld out of bounds (len %ld)\n", i, xs->len); exit(1); }
    return &xs->data[i];
}

void tycho_arr_str_set(Arena *a, TychoArrStr *xs, long i, const char *v) {
    if (i < 0 || i >= xs->len) {
        fprintf(stderr, "tycho: index %ld out of bounds (len %ld)\n", i, xs->len);
        exit(1);
    }
    /* MM-9: the slot is being overwritten, so the OLD element is dead -- value
     * semantics gives every str-array element a unique owner and reads copy out
     * (cf. tycho_str_copy at every get/bind site), so nothing else references it.
     * Recycle it back to the arena so the NEXT set reuses the bytes instead of
     * the arena growing with the whole stream (the sliding-window eviction case).
     * Copy FIRST (v may alias old, e.g. `s[k] = s[k] + x`), THEN recycle. Only
     * recycle a buffer THIS arena owns -- never an interned literal or a
     * cross-arena string (arena_owns), and never the freshly-stored buffer. */
    char *old = xs->data[i];
    char *nw = tycho_str_copy(a, v);              /* copy bytes into owner arena */
    if (old && old != v && arena_owns(a, old - 8)) {
        size_t on = ((size_t)(8 + (size_t)tycho_str_len(old) + 1) + 7u) & ~(size_t)7u;
        arena_recycle(a, old - 8, on);
    }
    xs->data[i] = nw;
}

/* value-semantic copy: independent buffer AND independent element bytes in a */
TychoArrStr tycho_arr_str_copy(Arena *a, TychoArrStr src) {
    TychoArrStr r = tycho_arr_str_with_cap(a, src.len);
    r.len = src.len;
    for (long i = 0; i < src.len; i++) r.data[i] = tycho_str_copy(a, src.data[i]);
    return r;
}

/* structural equality: same length and byte-equal elements (value semantics) */
int tycho_arr_str_eq(TychoArrStr x, TychoArrStr y) {
    if (x.len != y.len) return 0;
    for (long i = 0; i < x.len; i++)
        if (tycho_str_cmp(x.data[i], y.data[i]) != 0) return 0;
    return 1;
}

/* split s on a (non-empty) separator into a fresh [string] in arena a.
 * n separators -> n+1 fields (leading/trailing/adjacent seps yield empty
 * fields); an empty s yields one empty field. An empty separator has no
 * well-defined splitting, so fail closed rather than guess. */
TychoArrStr tycho_str_split(Arena *a, const char *s, const char *sep) {
    long sl = ((const long *)s)[-1], pl = ((const long *)sep)[-1];   /* byte-safe: header lengths, not strlen/strstr */
    if (pl == 0) {
        fprintf(stderr, "tycho: split with an empty separator\n");
        exit(1);
    }
    TychoArrStr r = tycho_arr_str_with_cap(a, 4);
    long start = 0, i = 0;
    while (i + pl <= sl) {
        if (memcmp(s + i, sep, (size_t)pl) == 0) {   /* field is [start, i) */
            char *field = tycho_str_alloc(a, i - start);
            memcpy(field, s + start, (size_t)(i - start));
            tycho_arr_str_push(a, &r, field);
            i += pl; start = i;
        } else {
            i++;
        }
    }
    /* last field: [start, sl) -- byte-safe, no strlen on a tail that may hold a NUL */
    char *tail = tycho_str_alloc(a, sl - start);
    memcpy(tail, s + start, (size_t)(sl - start));
    tycho_arr_str_push(a, &r, tail);
    return r;
}

/* list_dir(path): the directory's entries (excluding "." and ".."), or an empty
 * array if it can't be opened. Order is the filesystem's (sort if you need it). */
TychoArrStr tycho_list_dir(Arena *a, const char *path) {
    TychoArrStr r = tycho_arr_str_with_cap(a, 8);
    DIR *d = opendir(path);
    if (!d) return r;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' || (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;   /* skip "." and ".." */
        tycho_arr_str_push(a, &r, tycho_str_from_c(a, de->d_name));   /* d_name is a bare C string */
    }
    closedir(d);
    return r;
}

/* args(): the program's command-line arguments as a string array. */
TychoArrStr tycho_args(Arena *a) {
    TychoArrStr r = tycho_arr_str_with_cap(a, tycho_argc > 0 ? tycho_argc : 1);
    for (int i = 0; i < tycho_argc; i++)
        tycho_arr_str_push(a, &r, tycho_str_from_c(a, tycho_argv[i]));   /* argv[i] is a bare C string */
    return r;
}

/* ----------------------------------------------------------- Map(string,int)
 * A TychoMapSI is a value (5 words, passed/copied by value like TychoArr*). Its
 * backing tables live in an arena. Open addressing, linear probe, power-of-two
 * capacity. keys[i]==NULL means empty. Delete is linear-probe BACKWARD-SHIFT
 * (later probe-chain entries slide back into the gap), so the table stays
 * tombstone-free and `used == len` — both count live entries and rehash grows on
 * load only. (The TYCHO_MAP_TOMB sentinel and the tombstone branches in
 * _slot/_find below are now vestigial: delete shifts instead of tombstoning, so a
 * tombstone is never created.) A 0-cap map is the empty literal; the first insert
 * allocates.
 *
 * Value semantics, same as the arrays: `m2 = map_set(m, k, v)` returns a NEW
 * map (deep copy + insert) so `m` is unchanged; the compiler rewrites the
 * uniquely-owned accumulator shape `m = map_set(m, ...)` to map_put in place
 * (cf. the in-place string append) so a build-up loop is amortized O(1) per
 * insert instead of O(n) deep-copy each step. `m = map_del(m, k)` is rewritten
 * the same way to an in-place backward-shift delete. Key bytes are copied into the
 * owning arena exactly as [string] elements are (the lifetime seam nests). */
typedef struct { char **ekeys; long *evals; unsigned char *elive; int *idx; long len, ecount, ecap, icap; } TychoMapSI;

/* the tombstone sentinel: a unique non-NULL char* that is never a real key. */
static char tycho_map_tomb_;
#define TYCHO_MAP_TOMB (&tycho_map_tomb_)
static int tycho_map_live(const char *p) { return p != NULL && p != TYCHO_MAP_TOMB; }

/* Insertion-order list (`nxt`/`prv`/`head`/`tail`): every map keeps its live keys
 * in the order they were first inserted, so keys()/`for k in m` iterate
 * deterministically in insertion order — independent of bucket layout or hash
 * seed. This is what lets a random per-process hash seed not change observable
 * output (fixpoint cA==cB and parity stay byte-identical). The order is an
 * intrusive doubly-linked list over the table SLOTS: nxt[s]/prv[s] index sibling
 * slots (-1 = end) and head/tail are slot indices, so insert is an O(1) append
 * and delete is an O(1) unlink — a delete-heavy map (e.g. an LRU) stays O(1) per
 * op instead of O(n) array-compaction. The list is rebuilt on rehash (slot
 * indices change) by walking the old list in order. It is slot-indexed, so one
 * pair of helpers serves every key type. */
static void tycho_ord_link(long *nxt, long *prv, long *head, long *tail, long s) {   /* append slot s to the tail */
    prv[s] = *tail; nxt[s] = -1;
    if (*tail >= 0) nxt[*tail] = s; else *head = s;
    *tail = s;
}
static void tycho_ord_unlink(long *nxt, long *prv, long *head, long *tail, long s) { /* remove slot s, keep order */
    if (prv[s] >= 0) nxt[prv[s]] = nxt[s]; else *head = nxt[s];
    if (nxt[s] >= 0) prv[nxt[s]] = prv[s]; else *tail = prv[s];
}

/* ---- hash seed (hash-flooding hardening) ----------------------------------
 * A random per-process key seeds the map hashes, so an attacker who controls
 * map keys cannot precompute colliding keys (an algorithmic-complexity DoS:
 * crafted collisions turn O(1) probes into O(n) chains). The seed is read once
 * from the OS at startup -- tycho_hash_seed_init() is emitted as the first
 * statement of main(), before any map is touched. keys()/`for k in m` iterate
 * in INSERTION order (independent of the seed), so the random seed never
 * changes a program's observable output -- only its internal bucket layout. */
static unsigned long tycho_hash_k0 = 0x736f6d6570736575UL;   /* default key; overwritten at startup */
static unsigned long tycho_hash_k1 = 0x646f72616e646f6dUL;
static unsigned long tycho_ik_seed = 0;

static void tycho_hash_seed_init(void) {
    unsigned long buf[3];
    FILE *f = fopen("/dev/urandom", "rb");
    int ok = (f && fread(buf, sizeof buf, 1, f) == 1);
    if (f) fclose(f);
    if (ok) {
        tycho_hash_k0 = buf[0]; tycho_hash_k1 = buf[1]; tycho_ik_seed = buf[2];
    } else {                                  /* fallback: time XOR pid (weaker, but never unseeded) */
        unsigned long t = (unsigned long)time(NULL) ^ ((unsigned long)getpid() << 16);
        tycho_hash_k0 ^= t; tycho_hash_k1 ^= t * 0x9e3779b97f4a7c15UL; tycho_ik_seed = t;
    }
}

/* SipHash-1-3 (1 compression round / 3 finalization rounds): a keyed PRF whose
 * output an attacker cannot predict without the per-process key. Fast enough to
 * be the default map hash; strong enough to defeat collision crafting. */
#define TYCHO_SIPROUND(v0,v1,v2,v3) do { \
    v0 += v1; v1 = (v1 << 13) | (v1 >> 51); v1 ^= v0; v0 = (v0 << 32) | (v0 >> 32); \
    v2 += v3; v3 = (v3 << 16) | (v3 >> 48); v3 ^= v2; \
    v0 += v3; v3 = (v3 << 21) | (v3 >> 43); v3 ^= v0; \
    v2 += v1; v1 = (v1 << 17) | (v1 >> 47); v1 ^= v2; v2 = (v2 << 32) | (v2 >> 32); \
} while (0)
static unsigned long tycho_siphash13(const unsigned char *in, unsigned long inlen) {
    unsigned long v0 = 0x736f6d6570736575UL ^ tycho_hash_k0;
    unsigned long v1 = 0x646f72616e646f6dUL ^ tycho_hash_k1;
    unsigned long v2 = 0x6c7967656e657261UL ^ tycho_hash_k0;
    unsigned long v3 = 0x7465646279746573UL ^ tycho_hash_k1;
    unsigned long b = inlen << 56;
    unsigned long whole = inlen - (inlen % 8);
    const unsigned char *end = in + whole;
    for (; in != end; in += 8) {
        unsigned long m = (unsigned long)in[0] | ((unsigned long)in[1] << 8) | ((unsigned long)in[2] << 16) | ((unsigned long)in[3] << 24)
                        | ((unsigned long)in[4] << 32) | ((unsigned long)in[5] << 40) | ((unsigned long)in[6] << 48) | ((unsigned long)in[7] << 56);
        v3 ^= m; TYCHO_SIPROUND(v0, v1, v2, v3); v0 ^= m;
    }
    for (unsigned long i = 0; i < inlen % 8; i++) b |= (unsigned long)in[i] << (8 * i);   /* tail bytes (no fallthrough switch) */
    v3 ^= b; TYCHO_SIPROUND(v0, v1, v2, v3); v0 ^= b;
    v2 ^= 0xff;
    TYCHO_SIPROUND(v0, v1, v2, v3); TYCHO_SIPROUND(v0, v1, v2, v3); TYCHO_SIPROUND(v0, v1, v2, v3);
    return v0 ^ v1 ^ v2 ^ v3;
}

static unsigned long tycho_si_hash(const char *s) {        /* keyed SipHash-1-3 */
    long n = ((const long *)s)[-1];   /* hash the true bytes (header length), not up to a NUL */
    return tycho_siphash13((const unsigned char *)s, (unsigned long)n);
}

TychoMapSI tycho_map_si_with_cap(Arena *a, long cap) {
    TychoMapSI m; m.len = 0; m.ecount = 0;
    if (cap <= 0) { m.ekeys = NULL; m.evals = NULL; m.elive = NULL; m.idx = NULL; m.ecap = 0; m.icap = 0; return m; }
    long ec = 8; while (ec < cap) ec *= 2;
    long ic = 8; while (ic < cap * 2) ic *= 2;
    m.ecap = ec; m.icap = ic;
    m.ekeys = (char **)arena_alloc(a, (size_t)ec * sizeof(char *));
    m.evals = (long *)arena_alloc(a, (size_t)ec * sizeof(long));
    m.elive = (unsigned char *)arena_alloc(a, (size_t)ec);
    m.idx   = (int *)arena_alloc(a, (size_t)ic * sizeof(int));
    for (long i = 0; i < ic; i++) m.idx[i] = 0;
    return m;
}
/* find k, return its ENTRY index or -1 (index table is tombstone-free). */
static long tycho_map_si_find(TychoMapSI m, const char *k) {
    if (m.icap == 0) return -1;
    unsigned long mask = (unsigned long)m.icap - 1;
    long i = (long)(tycho_si_hash(k) & mask); int e;
    while ((e = m.idx[i]) != 0) {
        if (tycho_str_cmp(m.ekeys[e - 1], k) == 0) return e - 1;
        i = (long)((i + 1) & mask);
    }
    return -1;
}
static void tycho_map_si_idx_put(TychoMapSI *m, long ei) {
    unsigned long mask = (unsigned long)m->icap - 1;
    long i = (long)(tycho_si_hash(m->ekeys[ei]) & mask);
    while (m->idx[i] != 0) i = (long)((i + 1) & mask);
    m->idx[i] = (int)(ei + 1);
}
static void tycho_map_si_idx_grow(Arena *a, TychoMapSI *m) {
    long ic = m->icap ? m->icap * 2 : 8;
    int *ni = (int *)arena_alloc(a, (size_t)ic * sizeof(int));
    for (long i = 0; i < ic; i++) ni[i] = 0;
    m->idx = ni; m->icap = ic;
    for (long e = 0; e < m->ecount; e++) if (m->elive[e]) tycho_map_si_idx_put(m, e);
}
static void tycho_map_si_compact(TychoMapSI *m) {   /* in-place, NO alloc: drop tombstones, keep order, rebuild index */
    long w = 0;
    for (long r = 0; r < m->ecount; r++) if (m->elive[r]) {
        if (w != r) { m->ekeys[w] = m->ekeys[r]; m->evals[w] = m->evals[r]; m->elive[w] = 1; }
        w++;
    }
    m->ecount = w;
    for (long i = 0; i < m->icap; i++) m->idx[i] = 0;
    for (long e = 0; e < m->ecount; e++) tycho_map_si_idx_put(m, e);
}
static long tycho_map_si_append(Arena *a, TychoMapSI *m, char *k, long v) {
    if (m->ecount == m->ecap) {
        long dead = m->ecount - m->len;
        if (dead > m->ecap / 2) tycho_map_si_compact(m);
        else {
            long nc = m->ecap ? m->ecap * 2 : 8;
            char ** nk = (char **)arena_alloc(a, (size_t)nc * sizeof(char *));
            long * nv = (long *)arena_alloc(a, (size_t)nc * sizeof(long));
            unsigned char *nl = (unsigned char *)arena_alloc(a, (size_t)nc);
            for (long e = 0; e < m->ecount; e++) { nk[e] = m->ekeys[e]; nv[e] = m->evals[e]; nl[e] = m->elive[e]; }
            m->ekeys = nk; m->evals = nv; m->elive = nl; m->ecap = nc;
        }
    }
    if (m->ecount >= 2147483000L) { fprintf(stderr, "tycho: [string:int] map exceeds 2^31 entries\n"); abort(); }
    long e = m->ecount++;
    m->ekeys[e] = k; m->evals[e] = v; m->elive[e] = 1;
    return e;
}
void tycho_map_si_put(Arena *a, TychoMapSI *m, const char *k, long v) {
    long e = tycho_map_si_find(*m, k);
    if (e >= 0) { m->evals[e] = v; return; }
    if ((m->len + 1) * 2 > m->icap) tycho_map_si_idx_grow(a, m);
    long ne = tycho_map_si_append(a, m, tycho_str_copy(a, k), v);
    m->len++;
    tycho_map_si_idx_put(m, ne);
}
long *tycho_map_si_slotptr(Arena *a, TychoMapSI *m, const char *k) {
    long e = tycho_map_si_find(*m, k);
    if (e >= 0) return &m->evals[e];
    if ((m->len + 1) * 2 > m->icap) tycho_map_si_idx_grow(a, m);
    long ne = tycho_map_si_append(a, m, tycho_str_copy(a, k), 0L);
    m->len++;
    tycho_map_si_idx_put(m, ne);
    return &m->evals[ne];
}
void tycho_map_si_del(TychoMapSI *m, const char *k) {
    if (m->icap == 0) return;
    unsigned long mask = (unsigned long)m->icap - 1;
    long i = (long)(tycho_si_hash(k) & mask), found = -1;
    while (m->idx[i] != 0) {
        if (tycho_str_cmp(m->ekeys[m->idx[i] - 1], k) == 0) { found = i; break; }
        i = (long)((i + 1) & mask);
    }
    if (found < 0) return;
    long ei = m->idx[found] - 1;
    m->elive[ei] = 0; m->len--;
    long g = found;
    for (;;) {
        m->idx[g] = 0;
        long j = g;
        for (;;) {
            j = (long)((j + 1) & mask);
            if (m->idx[j] == 0) return;
            long h = (long)(tycho_si_hash(m->ekeys[m->idx[j] - 1]) & mask);
            if (g <= j) { if (g < h && h <= j) continue; }
            else        { if (g < h || h <= j) continue; }
            break;
        }
        m->idx[g] = m->idx[j]; g = j;
    }
}
TychoMapSI tycho_map_si_copy(Arena *a, TychoMapSI src) {
    TychoMapSI r = tycho_map_si_with_cap(a, src.len ? src.len : 0);
    for (long e = 0; e < src.ecount; e++) if (src.elive[e]) tycho_map_si_put(a, &r, src.ekeys[e], src.evals[e]);
    return r;
}
TychoMapSI tycho_map_si_set(Arena *a, TychoMapSI m, const char *k, long v) {
    TychoMapSI r = tycho_map_si_copy(a, m); tycho_map_si_put(a, &r, k, v); return r;
}
TychoMapSI tycho_map_si_del_pure(Arena *a, TychoMapSI m, const char *k) {
    TychoMapSI r = tycho_map_si_copy(a, m); tycho_map_si_del(&r, k); return r;
}
long tycho_map_si_get(TychoMapSI m, const char *k, long dflt) {
    long e = tycho_map_si_find(m, k); return e < 0 ? dflt : m.evals[e];
}
int tycho_map_si_has(TychoMapSI m, const char *k) { return tycho_map_si_find(m, k) >= 0; }
TychoArrStr tycho_map_si_keys(Arena *a, TychoMapSI m) {
    TychoArrStr r = tycho_arr_str_with_cap(a, m.len);
    for (long e = 0; e < m.ecount; e++) if (m.elive[e]) tycho_arr_str_push(a, &r, m.ekeys[e]);
    return r;
}
int tycho_map_si_eq(TychoMapSI x, TychoMapSI y) {
    if (x.len != y.len) return 0;
    for (long e = 0; e < x.ecount; e++) if (x.elive[e]) {
        long s = tycho_map_si_find(y, x.ekeys[e]);
        if (s < 0 || y.evals[s] != x.evals[e]) return 0;
    }
    return 1;
}

/* ----------------------------------------------------------- Map(string,float)
 * Exactly TychoMapSI with `double` values (string keys, float values) — the
 * same open-addressing table, sharing tycho_map_live, tycho_si_hash, and its
 * (now-vestigial) tombstone sentinel. Only the value word's type differs. */
typedef struct { char **ekeys; double *evals; unsigned char *elive; int *idx; long len, ecount, ecap, icap; } TychoMapSF;

TychoMapSF tycho_map_sf_with_cap(Arena *a, long cap) {
    TychoMapSF m; m.len = 0; m.ecount = 0;
    if (cap <= 0) { m.ekeys = NULL; m.evals = NULL; m.elive = NULL; m.idx = NULL; m.ecap = 0; m.icap = 0; return m; }
    long ec = 8; while (ec < cap) ec *= 2;
    long ic = 8; while (ic < cap * 2) ic *= 2;
    m.ecap = ec; m.icap = ic;
    m.ekeys = (char **)arena_alloc(a, (size_t)ec * sizeof(char *));
    m.evals = (double *)arena_alloc(a, (size_t)ec * sizeof(double));
    m.elive = (unsigned char *)arena_alloc(a, (size_t)ec);
    m.idx   = (int *)arena_alloc(a, (size_t)ic * sizeof(int));
    for (long i = 0; i < ic; i++) m.idx[i] = 0;
    return m;
}
/* find k, return its ENTRY index or -1 (index table is tombstone-free). */
static long tycho_map_sf_find(TychoMapSF m, const char *k) {
    if (m.icap == 0) return -1;
    unsigned long mask = (unsigned long)m.icap - 1;
    long i = (long)(tycho_si_hash(k) & mask); int e;
    while ((e = m.idx[i]) != 0) {
        if (tycho_str_cmp(m.ekeys[e - 1], k) == 0) return e - 1;
        i = (long)((i + 1) & mask);
    }
    return -1;
}
static void tycho_map_sf_idx_put(TychoMapSF *m, long ei) {
    unsigned long mask = (unsigned long)m->icap - 1;
    long i = (long)(tycho_si_hash(m->ekeys[ei]) & mask);
    while (m->idx[i] != 0) i = (long)((i + 1) & mask);
    m->idx[i] = (int)(ei + 1);
}
static void tycho_map_sf_idx_grow(Arena *a, TychoMapSF *m) {
    long ic = m->icap ? m->icap * 2 : 8;
    int *ni = (int *)arena_alloc(a, (size_t)ic * sizeof(int));
    for (long i = 0; i < ic; i++) ni[i] = 0;
    m->idx = ni; m->icap = ic;
    for (long e = 0; e < m->ecount; e++) if (m->elive[e]) tycho_map_sf_idx_put(m, e);
}
static void tycho_map_sf_compact(TychoMapSF *m) {   /* in-place, NO alloc: drop tombstones, keep order, rebuild index */
    long w = 0;
    for (long r = 0; r < m->ecount; r++) if (m->elive[r]) {
        if (w != r) { m->ekeys[w] = m->ekeys[r]; m->evals[w] = m->evals[r]; m->elive[w] = 1; }
        w++;
    }
    m->ecount = w;
    for (long i = 0; i < m->icap; i++) m->idx[i] = 0;
    for (long e = 0; e < m->ecount; e++) tycho_map_sf_idx_put(m, e);
}
static long tycho_map_sf_append(Arena *a, TychoMapSF *m, char *k, double v) {
    if (m->ecount == m->ecap) {
        long dead = m->ecount - m->len;
        if (dead > m->ecap / 2) tycho_map_sf_compact(m);
        else {
            long nc = m->ecap ? m->ecap * 2 : 8;
            char ** nk = (char **)arena_alloc(a, (size_t)nc * sizeof(char *));
            double * nv = (double *)arena_alloc(a, (size_t)nc * sizeof(double));
            unsigned char *nl = (unsigned char *)arena_alloc(a, (size_t)nc);
            for (long e = 0; e < m->ecount; e++) { nk[e] = m->ekeys[e]; nv[e] = m->evals[e]; nl[e] = m->elive[e]; }
            m->ekeys = nk; m->evals = nv; m->elive = nl; m->ecap = nc;
        }
    }
    if (m->ecount >= 2147483000L) { fprintf(stderr, "tycho: [string:float] map exceeds 2^31 entries\n"); abort(); }
    long e = m->ecount++;
    m->ekeys[e] = k; m->evals[e] = v; m->elive[e] = 1;
    return e;
}
void tycho_map_sf_put(Arena *a, TychoMapSF *m, const char *k, double v) {
    long e = tycho_map_sf_find(*m, k);
    if (e >= 0) { m->evals[e] = v; return; }
    if ((m->len + 1) * 2 > m->icap) tycho_map_sf_idx_grow(a, m);
    long ne = tycho_map_sf_append(a, m, tycho_str_copy(a, k), v);
    m->len++;
    tycho_map_sf_idx_put(m, ne);
}
double *tycho_map_sf_slotptr(Arena *a, TychoMapSF *m, const char *k) {
    long e = tycho_map_sf_find(*m, k);
    if (e >= 0) return &m->evals[e];
    if ((m->len + 1) * 2 > m->icap) tycho_map_sf_idx_grow(a, m);
    long ne = tycho_map_sf_append(a, m, tycho_str_copy(a, k), 0.0);
    m->len++;
    tycho_map_sf_idx_put(m, ne);
    return &m->evals[ne];
}
void tycho_map_sf_del(TychoMapSF *m, const char *k) {
    if (m->icap == 0) return;
    unsigned long mask = (unsigned long)m->icap - 1;
    long i = (long)(tycho_si_hash(k) & mask), found = -1;
    while (m->idx[i] != 0) {
        if (tycho_str_cmp(m->ekeys[m->idx[i] - 1], k) == 0) { found = i; break; }
        i = (long)((i + 1) & mask);
    }
    if (found < 0) return;
    long ei = m->idx[found] - 1;
    m->elive[ei] = 0; m->len--;
    long g = found;
    for (;;) {
        m->idx[g] = 0;
        long j = g;
        for (;;) {
            j = (long)((j + 1) & mask);
            if (m->idx[j] == 0) return;
            long h = (long)(tycho_si_hash(m->ekeys[m->idx[j] - 1]) & mask);
            if (g <= j) { if (g < h && h <= j) continue; }
            else        { if (g < h || h <= j) continue; }
            break;
        }
        m->idx[g] = m->idx[j]; g = j;
    }
}
TychoMapSF tycho_map_sf_copy(Arena *a, TychoMapSF src) {
    TychoMapSF r = tycho_map_sf_with_cap(a, src.len ? src.len : 0);
    for (long e = 0; e < src.ecount; e++) if (src.elive[e]) tycho_map_sf_put(a, &r, src.ekeys[e], src.evals[e]);
    return r;
}
TychoMapSF tycho_map_sf_set(Arena *a, TychoMapSF m, const char *k, double v) {
    TychoMapSF r = tycho_map_sf_copy(a, m); tycho_map_sf_put(a, &r, k, v); return r;
}
TychoMapSF tycho_map_sf_del_pure(Arena *a, TychoMapSF m, const char *k) {
    TychoMapSF r = tycho_map_sf_copy(a, m); tycho_map_sf_del(&r, k); return r;
}
double tycho_map_sf_get(TychoMapSF m, const char *k, double dflt) {
    long e = tycho_map_sf_find(m, k); return e < 0 ? dflt : m.evals[e];
}
int tycho_map_sf_has(TychoMapSF m, const char *k) { return tycho_map_sf_find(m, k) >= 0; }
TychoArrStr tycho_map_sf_keys(Arena *a, TychoMapSF m) {
    TychoArrStr r = tycho_arr_str_with_cap(a, m.len);
    for (long e = 0; e < m.ecount; e++) if (m.elive[e]) tycho_arr_str_push(a, &r, m.ekeys[e]);
    return r;
}
int tycho_map_sf_eq(TychoMapSF x, TychoMapSF y) {
    if (x.len != y.len) return 0;
    for (long e = 0; e < x.ecount; e++) if (x.elive[e]) {
        long s = tycho_map_sf_find(y, x.ekeys[e]);
        if (s < 0 || y.evals[s] != x.evals[e]) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------- Map(int, V)
 * Int-keyed maps (TychoMapII: int->int, TychoMapIF: int->float). Same open-
 * addressing table as the string-keyed maps, but the key is a `long` value, so
 * there is no NULL/sentinel free in the key array (0 is a real key): an `occ`
 * byte per slot tracks 0=empty / 1=live instead (delete backward-shifts, so the
 * vestigial 2=tombstone is never set). Keys are plain values (no arena copy).
 * tycho_ik_hash mixes the bits so sequential int keys do not cluster. */
static unsigned long tycho_ik_hash(long k) {        /* seeded SplitMix64 finalizer */
    unsigned long x = ((unsigned long)k ^ tycho_ik_seed) + 0x9e3779b97f4a7c15UL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9UL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebUL;
    return x ^ (x >> 31);
}
/* Deep hash of a scalar array, for composite map keys. Order-SENSITIVE (the multiply
 * runs before the xor), seeded from the per-process key, folding each element's hash
 * so equal-by-== arrays hash equal. Composite-element arrays get a generated hash. */
unsigned long tycho_arr_int_hash(TychoArrInt x) {
    unsigned long h = tycho_hash_k0;
    for (long i = 0; i < x.len; i++) h = h * 1099511628211UL ^ tycho_ik_hash(x.data[i]);
    return h;
}
unsigned long tycho_arr_float_hash(TychoArrFloat x) {
    unsigned long h = tycho_hash_k0;
    for (long i = 0; i < x.len; i++) h = h * 1099511628211UL ^ tycho_ik_hash((long)((union { double _d; long _l; }){ ._d = x.data[i] })._l);
    return h;
}
unsigned long tycho_arr_str_hash(TychoArrStr x) {
    unsigned long h = tycho_hash_k0;
    for (long i = 0; i < x.len; i++) h = h * 1099511628211UL ^ tycho_si_hash(x.data[i]);
    return h;
}

/* ---- TychoMapII: COMPACT (indexed-dict) layout ----------------------------
 * Two arrays instead of one value-inline table: a small int32 INDEX table
 * (open-addressing, stores entry_index+1; 0 = empty) points into a DENSE
 * ENTRY array (ekeys/evals) kept in INSERTION ORDER. keys()/`for k in m`
 * walk the entries directly, so no separate nxt/prv order list is needed --
 * an empty index slot costs 4 B instead of a whole value+order-list slot.
 * Delete tombstones the entry (elive[e]=0, preserves order) and backward-
 * shifts the int32 index (kept tombstone-free); when appends find the entry
 * array full and >half-dead, an in-place, ALLOCATION-FREE compaction reclaims
 * the tombstones -- the churn bound that keeps a delete-heavy map (bench/lru)
 * from growing without bound under an arena that never frees. Insertion order
 * is the entries array (independent of hash seed), so a random per-process
 * seed stays observable-output-invisible (fixpoint/parity byte-identical).
 * Compact-dict design: docs/internals/compact-dict-map-design.md. */
typedef struct { long *ekeys; long *evals; unsigned char *elive; int *idx; long len, ecount, ecap, icap; } TychoMapII;
typedef struct { long *ekeys; double *evals; unsigned char *elive; int *idx; long len, ecount, ecap, icap; } TychoMapIF;

TychoMapII tycho_map_ii_with_cap(Arena *a, long cap) {
    TychoMapII m; m.len = 0; m.ecount = 0;
    if (cap <= 0) { m.ekeys = NULL; m.evals = NULL; m.elive = NULL; m.idx = NULL; m.ecap = 0; m.icap = 0; return m; }
    long ec = 8; while (ec < cap) ec *= 2;               /* entry capacity >= cap */
    long ic = 8; while (ic < cap * 2) ic *= 2;           /* index capacity, load <= 0.5 */
    m.ecap = ec; m.icap = ic;
    m.ekeys = (long *)arena_alloc(a, (size_t)ec * sizeof(long));
    m.evals = (long *)arena_alloc(a, (size_t)ec * sizeof(long));
    m.elive = (unsigned char *)arena_alloc(a, (size_t)ec);
    m.idx   = (int *)arena_alloc(a, (size_t)ic * sizeof(int));
    for (long i = 0; i < ic; i++) m.idx[i] = 0;
    return m;
}
/* find k, return its ENTRY index or -1. The index table is tombstone-free
 * (delete backward-shifts it), so every non-zero slot points at a LIVE entry. */
static long tycho_map_ii_find(TychoMapII m, long k) {
    if (m.icap == 0) return -1;
    unsigned long mask = (unsigned long)m.icap - 1;
    long i = (long)(tycho_ik_hash(k) & mask); int e;
    while ((e = m.idx[i]) != 0) {
        if (m.ekeys[e - 1] == k) return e - 1;
        i = (long)((i + 1) & mask);
    }
    return -1;
}
static void tycho_map_ii_idx_put(TychoMapII *m, long ei) {   /* place entry ei into the (tombstone-free) index */
    unsigned long mask = (unsigned long)m->icap - 1;
    long i = (long)(tycho_ik_hash(m->ekeys[ei]) & mask);
    while (m->idx[i] != 0) i = (long)((i + 1) & mask);
    m->idx[i] = (int)(ei + 1);
}
static void tycho_map_ii_idx_grow(Arena *a, TychoMapII *m) { /* double the index, re-hash live entries */
    long ic = m->icap ? m->icap * 2 : 8;
    int *ni = (int *)arena_alloc(a, (size_t)ic * sizeof(int));
    for (long i = 0; i < ic; i++) ni[i] = 0;
    m->idx = ni; m->icap = ic;
    for (long e = 0; e < m->ecount; e++) if (m->elive[e]) tycho_map_ii_idx_put(m, e);
}
static void tycho_map_ii_compact(TychoMapII *m) {           /* in-place, NO alloc: drop tombstones, keep order, rebuild index */
    long w = 0;
    for (long r = 0; r < m->ecount; r++) if (m->elive[r]) {
        if (w != r) { m->ekeys[w] = m->ekeys[r]; m->evals[w] = m->evals[r]; m->elive[w] = 1; }
        w++;
    }
    m->ecount = w;                                          /* == len */
    for (long i = 0; i < m->icap; i++) m->idx[i] = 0;
    for (long e = 0; e < m->ecount; e++) tycho_map_ii_idx_put(m, e);
}
static long tycho_map_ii_append(Arena *a, TychoMapII *m, long k, long v) {
    if (m->ecount == m->ecap) {
        long dead = m->ecount - m->len;
        if (dead > m->ecap / 2) tycho_map_ii_compact(m);    /* reclaim tombstones in place -> churn bound (bench/lru) */
        else {                                              /* grow entries by doubling */
            long nc = m->ecap ? m->ecap * 2 : 8;
            long *nk = (long *)arena_alloc(a, (size_t)nc * sizeof(long));
            long *nv = (long *)arena_alloc(a, (size_t)nc * sizeof(long));
            unsigned char *nl = (unsigned char *)arena_alloc(a, (size_t)nc);
            for (long e = 0; e < m->ecount; e++) { nk[e] = m->ekeys[e]; nv[e] = m->evals[e]; nl[e] = m->elive[e]; }
            m->ekeys = nk; m->evals = nv; m->elive = nl; m->ecap = nc;
        }
    }
    if (m->ecount >= 2147483000L) { fprintf(stderr, "tycho: [int:int] map exceeds 2^31 entries\n"); abort(); }
    long e = m->ecount++;
    m->ekeys[e] = k; m->evals[e] = v; m->elive[e] = 1;
    return e;
}
void tycho_map_ii_put(Arena *a, TychoMapII *m, long k, long v) {
    long e = tycho_map_ii_find(*m, k);
    if (e >= 0) { m->evals[e] = v; return; }                /* update existing */
    if ((m->len + 1) * 2 > m->icap) tycho_map_ii_idx_grow(a, m);
    long ne = tycho_map_ii_append(a, m, k, v);
    m->len++;
    tycho_map_ii_idx_put(m, ne);
}
/* find-or-insert k, return &entry value (#2). See tycho_map_si_slotptr; zero is 0. */
long *tycho_map_ii_slotptr(Arena *a, TychoMapII *m, long k) {
    long e = tycho_map_ii_find(*m, k);
    if (e >= 0) return &m->evals[e];
    if ((m->len + 1) * 2 > m->icap) tycho_map_ii_idx_grow(a, m);
    long ne = tycho_map_ii_append(a, m, k, 0L);
    m->len++;
    tycho_map_ii_idx_put(m, ne);
    return &m->evals[ne];
}
void tycho_map_ii_del(TychoMapII *m, long k) {
    if (m->icap == 0) return;
    unsigned long mask = (unsigned long)m->icap - 1;
    long i = (long)(tycho_ik_hash(k) & mask), found = -1;   /* locate k's index slot */
    while (m->idx[i] != 0) {
        if (m->ekeys[m->idx[i] - 1] == k) { found = i; break; }
        i = (long)((i + 1) & mask);
    }
    if (found < 0) return;
    long ei = m->idx[found] - 1;
    m->elive[ei] = 0; m->len--;                             /* tombstone the entry; entries array keeps insertion order */
    long g = found;                                         /* backward-shift the index to drop slot `found` (index stays tombstone-free) */
    for (;;) {
        m->idx[g] = 0;                                      /* g is the gap */
        long j = g;
        for (;;) {
            j = (long)((j + 1) & mask);
            if (m->idx[j] == 0) return;                     /* chain ended at an empty slot */
            long h = (long)(tycho_ik_hash(m->ekeys[m->idx[j] - 1]) & mask);   /* home slot of the entry at j */
            if (g <= j) { if (g < h && h <= j) continue; }  /* h cyclically in (g,j]: must stay */
            else        { if (g < h || h <= j) continue; }
            break;                                          /* entry at j can slide back into the gap */
        }
        m->idx[g] = m->idx[j]; g = j;
    }
}
TychoMapII tycho_map_ii_copy(Arena *a, TychoMapII src) {
    TychoMapII r = tycho_map_ii_with_cap(a, src.len ? src.len : 0);
    for (long e = 0; e < src.ecount; e++) if (src.elive[e]) tycho_map_ii_put(a, &r, src.ekeys[e], src.evals[e]);
    return r;
}
TychoMapII tycho_map_ii_set(Arena *a, TychoMapII m, long k, long v) {
    TychoMapII r = tycho_map_ii_copy(a, m); tycho_map_ii_put(a, &r, k, v); return r;
}
TychoMapII tycho_map_ii_del_pure(Arena *a, TychoMapII m, long k) {
    TychoMapII r = tycho_map_ii_copy(a, m); tycho_map_ii_del(&r, k); return r;
}
long tycho_map_ii_get(TychoMapII m, long k, long dflt) {
    long e = tycho_map_ii_find(m, k); return e < 0 ? dflt : m.evals[e];
}
int tycho_map_ii_has(TychoMapII m, long k) { return tycho_map_ii_find(m, k) >= 0; }
TychoArrInt tycho_map_ii_keys(Arena *a, TychoMapII m) {
    TychoArrInt r = tycho_arr_int_with_cap(a, m.len);
    for (long e = 0; e < m.ecount; e++) if (m.elive[e]) tycho_arr_int_push(a, &r, m.ekeys[e]);
    return r;
}
int tycho_map_ii_eq(TychoMapII x, TychoMapII y) {
    if (x.len != y.len) return 0;
    for (long e = 0; e < x.ecount; e++) if (x.elive[e]) {
        long s = tycho_map_ii_find(y, x.ekeys[e]);
        if (s < 0 || y.evals[s] != x.evals[e]) return 0;
    }
    return 1;
}

/* TychoMapIF: identical table, double values. */
TychoMapIF tycho_map_if_with_cap(Arena *a, long cap) {
    TychoMapIF m; m.len = 0; m.ecount = 0;
    if (cap <= 0) { m.ekeys = NULL; m.evals = NULL; m.elive = NULL; m.idx = NULL; m.ecap = 0; m.icap = 0; return m; }
    long ec = 8; while (ec < cap) ec *= 2;
    long ic = 8; while (ic < cap * 2) ic *= 2;
    m.ecap = ec; m.icap = ic;
    m.ekeys = (long *)arena_alloc(a, (size_t)ec * sizeof(long));
    m.evals = (double *)arena_alloc(a, (size_t)ec * sizeof(double));
    m.elive = (unsigned char *)arena_alloc(a, (size_t)ec);
    m.idx   = (int *)arena_alloc(a, (size_t)ic * sizeof(int));
    for (long i = 0; i < ic; i++) m.idx[i] = 0;
    return m;
}
/* find k, return its ENTRY index or -1 (index table is tombstone-free). */
static long tycho_map_if_find(TychoMapIF m, long k) {
    if (m.icap == 0) return -1;
    unsigned long mask = (unsigned long)m.icap - 1;
    long i = (long)(tycho_ik_hash(k) & mask); int e;
    while ((e = m.idx[i]) != 0) {
        if (m.ekeys[e - 1] == k) return e - 1;
        i = (long)((i + 1) & mask);
    }
    return -1;
}
static void tycho_map_if_idx_put(TychoMapIF *m, long ei) {
    unsigned long mask = (unsigned long)m->icap - 1;
    long i = (long)(tycho_ik_hash(m->ekeys[ei]) & mask);
    while (m->idx[i] != 0) i = (long)((i + 1) & mask);
    m->idx[i] = (int)(ei + 1);
}
static void tycho_map_if_idx_grow(Arena *a, TychoMapIF *m) {
    long ic = m->icap ? m->icap * 2 : 8;
    int *ni = (int *)arena_alloc(a, (size_t)ic * sizeof(int));
    for (long i = 0; i < ic; i++) ni[i] = 0;
    m->idx = ni; m->icap = ic;
    for (long e = 0; e < m->ecount; e++) if (m->elive[e]) tycho_map_if_idx_put(m, e);
}
static void tycho_map_if_compact(TychoMapIF *m) {   /* in-place, NO alloc: drop tombstones, keep order, rebuild index */
    long w = 0;
    for (long r = 0; r < m->ecount; r++) if (m->elive[r]) {
        if (w != r) { m->ekeys[w] = m->ekeys[r]; m->evals[w] = m->evals[r]; m->elive[w] = 1; }
        w++;
    }
    m->ecount = w;
    for (long i = 0; i < m->icap; i++) m->idx[i] = 0;
    for (long e = 0; e < m->ecount; e++) tycho_map_if_idx_put(m, e);
}
static long tycho_map_if_append(Arena *a, TychoMapIF *m, long k, double v) {
    if (m->ecount == m->ecap) {
        long dead = m->ecount - m->len;
        if (dead > m->ecap / 2) tycho_map_if_compact(m);
        else {
            long nc = m->ecap ? m->ecap * 2 : 8;
            long * nk = (long *)arena_alloc(a, (size_t)nc * sizeof(long));
            double * nv = (double *)arena_alloc(a, (size_t)nc * sizeof(double));
            unsigned char *nl = (unsigned char *)arena_alloc(a, (size_t)nc);
            for (long e = 0; e < m->ecount; e++) { nk[e] = m->ekeys[e]; nv[e] = m->evals[e]; nl[e] = m->elive[e]; }
            m->ekeys = nk; m->evals = nv; m->elive = nl; m->ecap = nc;
        }
    }
    if (m->ecount >= 2147483000L) { fprintf(stderr, "tycho: [int:float] map exceeds 2^31 entries\n"); abort(); }
    long e = m->ecount++;
    m->ekeys[e] = k; m->evals[e] = v; m->elive[e] = 1;
    return e;
}
void tycho_map_if_put(Arena *a, TychoMapIF *m, long k, double v) {
    long e = tycho_map_if_find(*m, k);
    if (e >= 0) { m->evals[e] = v; return; }
    if ((m->len + 1) * 2 > m->icap) tycho_map_if_idx_grow(a, m);
    long ne = tycho_map_if_append(a, m, k, v);
    m->len++;
    tycho_map_if_idx_put(m, ne);
}
double *tycho_map_if_slotptr(Arena *a, TychoMapIF *m, long k) {
    long e = tycho_map_if_find(*m, k);
    if (e >= 0) return &m->evals[e];
    if ((m->len + 1) * 2 > m->icap) tycho_map_if_idx_grow(a, m);
    long ne = tycho_map_if_append(a, m, k, 0.0);
    m->len++;
    tycho_map_if_idx_put(m, ne);
    return &m->evals[ne];
}
void tycho_map_if_del(TychoMapIF *m, long k) {
    if (m->icap == 0) return;
    unsigned long mask = (unsigned long)m->icap - 1;
    long i = (long)(tycho_ik_hash(k) & mask), found = -1;
    while (m->idx[i] != 0) {
        if (m->ekeys[m->idx[i] - 1] == k) { found = i; break; }
        i = (long)((i + 1) & mask);
    }
    if (found < 0) return;
    long ei = m->idx[found] - 1;
    m->elive[ei] = 0; m->len--;
    long g = found;
    for (;;) {
        m->idx[g] = 0;
        long j = g;
        for (;;) {
            j = (long)((j + 1) & mask);
            if (m->idx[j] == 0) return;
            long h = (long)(tycho_ik_hash(m->ekeys[m->idx[j] - 1]) & mask);
            if (g <= j) { if (g < h && h <= j) continue; }
            else        { if (g < h || h <= j) continue; }
            break;
        }
        m->idx[g] = m->idx[j]; g = j;
    }
}
TychoMapIF tycho_map_if_copy(Arena *a, TychoMapIF src) {
    TychoMapIF r = tycho_map_if_with_cap(a, src.len ? src.len : 0);
    for (long e = 0; e < src.ecount; e++) if (src.elive[e]) tycho_map_if_put(a, &r, src.ekeys[e], src.evals[e]);
    return r;
}
TychoMapIF tycho_map_if_set(Arena *a, TychoMapIF m, long k, double v) {
    TychoMapIF r = tycho_map_if_copy(a, m); tycho_map_if_put(a, &r, k, v); return r;
}
TychoMapIF tycho_map_if_del_pure(Arena *a, TychoMapIF m, long k) {
    TychoMapIF r = tycho_map_if_copy(a, m); tycho_map_if_del(&r, k); return r;
}
double tycho_map_if_get(TychoMapIF m, long k, double dflt) {
    long e = tycho_map_if_find(m, k); return e < 0 ? dflt : m.evals[e];
}
int tycho_map_if_has(TychoMapIF m, long k) { return tycho_map_if_find(m, k) >= 0; }
TychoArrInt tycho_map_if_keys(Arena *a, TychoMapIF m) {
    TychoArrInt r = tycho_arr_int_with_cap(a, m.len);
    for (long e = 0; e < m.ecount; e++) if (m.elive[e]) tycho_arr_int_push(a, &r, m.ekeys[e]);
    return r;
}
int tycho_map_if_eq(TychoMapIF x, TychoMapIF y) {
    if (x.len != y.len) return 0;
    for (long e = 0; e < x.ecount; e++) if (x.elive[e]) {
        long s = tycho_map_if_find(y, x.ekeys[e]);
        if (s < 0 || y.evals[s] != x.evals[e]) return 0;
    }
    return 1;
}

/* ---- str() of built-in containers (F5) ------------------------------------
 * Render an aggregate to a length-headered Tycho string for str()/println.
 * Format: arrays `[e0, e1]`, maps `[k: v]` (string keys raw, no quotes -- str
 * of a string is identity), elements joined by ", ". Maps iterate in insertion
 * order (the nxt/head list), so output is deterministic and seed-invariant.
 * Composite-element arrays/maps get a generated tycho_str_* helper instead;
 * these four fixed-element families live here (shared, one definition). Built
 * by folding tycho_str_concat -- O(n^2) in piece count, fine for a debug print. */
char *tycho_arr_int_str(Arena *a, TychoArrInt xs) {
    char *r = tycho_str_from_c(a, "[");
    for (long i = 0; i < xs.len; i++) {
        if (i) r = tycho_str_concat(a, r, tycho_str_from_c(a, ", "));
        r = tycho_str_concat(a, r, tycho_int_to_str(a, xs.data[i]));
    }
    return tycho_str_concat(a, r, tycho_str_from_c(a, "]"));
}
char *tycho_arr_float_str(Arena *a, TychoArrFloat xs) {
    char *r = tycho_str_from_c(a, "[");
    for (long i = 0; i < xs.len; i++) {
        if (i) r = tycho_str_concat(a, r, tycho_str_from_c(a, ", "));
        r = tycho_str_concat(a, r, tycho_float_to_str(a, xs.data[i]));
    }
    return tycho_str_concat(a, r, tycho_str_from_c(a, "]"));
}
char *tycho_arr_str_str(Arena *a, TychoArrStr xs) {
    char *r = tycho_str_from_c(a, "[");
    for (long i = 0; i < xs.len; i++) {
        if (i) r = tycho_str_concat(a, r, tycho_str_from_c(a, ", "));
        r = tycho_str_concat(a, r, xs.data[i]);   /* string element: raw (str is identity) */
    }
    return tycho_str_concat(a, r, tycho_str_from_c(a, "]"));
}
char *tycho_map_si_str(Arena *a, TychoMapSI m) {
    char *r = tycho_str_from_c(a, "["); int first = 1;
    for (long e = 0; e < m.ecount; e++) if (m.elive[e]) {
        if (!first) r = tycho_str_concat(a, r, tycho_str_from_c(a, ", ")); first = 0;
        r = tycho_str_concat(a, r, m.ekeys[e]);
        r = tycho_str_concat(a, r, tycho_str_from_c(a, ": "));
        r = tycho_str_concat(a, r, tycho_int_to_str(a, m.evals[e]));
    }
    return tycho_str_concat(a, r, tycho_str_from_c(a, "]"));
}
char *tycho_map_sf_str(Arena *a, TychoMapSF m) {
    char *r = tycho_str_from_c(a, "["); int first = 1;
    for (long e = 0; e < m.ecount; e++) if (m.elive[e]) {
        if (!first) r = tycho_str_concat(a, r, tycho_str_from_c(a, ", ")); first = 0;
        r = tycho_str_concat(a, r, m.ekeys[e]);
        r = tycho_str_concat(a, r, tycho_str_from_c(a, ": "));
        r = tycho_str_concat(a, r, tycho_float_to_str(a, m.evals[e]));
    }
    return tycho_str_concat(a, r, tycho_str_from_c(a, "]"));
}
char *tycho_map_ii_str(Arena *a, TychoMapII m) {
    char *r = tycho_str_from_c(a, "["); int first = 1;
    for (long e = 0; e < m.ecount; e++) if (m.elive[e]) {
        if (!first) r = tycho_str_concat(a, r, tycho_str_from_c(a, ", ")); first = 0;
        r = tycho_str_concat(a, r, tycho_int_to_str(a, m.ekeys[e]));
        r = tycho_str_concat(a, r, tycho_str_from_c(a, ": "));
        r = tycho_str_concat(a, r, tycho_int_to_str(a, m.evals[e]));
    }
    return tycho_str_concat(a, r, tycho_str_from_c(a, "]"));
}
char *tycho_map_if_str(Arena *a, TychoMapIF m) {
    char *r = tycho_str_from_c(a, "["); int first = 1;
    for (long e = 0; e < m.ecount; e++) if (m.elive[e]) {
        if (!first) r = tycho_str_concat(a, r, tycho_str_from_c(a, ", ")); first = 0;
        r = tycho_str_concat(a, r, tycho_int_to_str(a, m.ekeys[e]));
        r = tycho_str_concat(a, r, tycho_str_from_c(a, ": "));
        r = tycho_str_concat(a, r, tycho_float_to_str(a, m.evals[e]));
    }
    return tycho_str_concat(a, r, tycho_str_from_c(a, "]"));
}
