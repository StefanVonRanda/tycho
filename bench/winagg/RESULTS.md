# Windowed group-by — head-to-head (hier vs C vs Rust vs Go)

A **churn / transient** workload — the honest complement to the inverted index
(`bench/invindex`, a *build-and-hold* workload). A deterministic 10 M-event stream
(`seed = (seed*1103515245 + 12345) % 2^31`) is processed in 40 fixed windows of
250 000 events. Each window builds a fresh `[int: [int]]` (group id → that window's
values, via `push(m[gid], v)`), reduces it to a running checksum, then **discards it**.
The whole window map dies at the end of each loop iteration. Every language runs the
identical algorithm over the identical stream and prints the same oracle:

    windows=40 checksum=505098931

Peak RSS (`bench/peakrss.c`), best-of-five, `-O2` / `rustc -O` / `go build`. Each
language uses its idiomatic per-key-growing-list map: hier `[int: [int]]`, C a
hand-rolled open-addressing table of `realloc`'d lists, Rust `HashMap<i64, Vec<i64>>`,
Go `map[int64][]int64`.

| impl | peak RSS | time |
| ---- | -------: | ---: |
| C    | **5 MB** | **128 ms** |
| hier | 7 MB | 190 ms |
| Rust | 5 MB | 238 ms |
| Go   | 11 MB | 330 ms |

## Reading this honestly

This is the case the implicit arena is *built* for, and it shows — but read the two
axes separately, because they support different-strength claims.

1. **Peak memory — the clean arena claim (hasher-independent).** hier holds **7 MB:
   one window**, not the stream. It is ~1.4× C (5 MB) and **beats GC'd Go (11 MB)**.
   The proof that this is genuine per-window reclamation, not luck: peak tracks the
   *window size*, not the *window count*. At 6 windows × 600 k events hier peaks at
   17 MB; at 40 windows × 250 k it peaks at 8 MB — 6.7× more windows, yet peak *fell*
   with the smaller window. The arena frees each window in bulk (one pointer reset), so
   the stream length never accumulates. Contrast `invindex`, where the index is held to
   the end and hier pays ~1.7× C — the arena's bulk-free advantage never fires there;
   here it is the whole game.

2. **Time — competitive (C–Go band), but with a hasher caveat, so no clean "beats
   Rust".** hier (190 ms) sits between C (128 ms) and Go (330 ms), and below
   out-of-the-box Rust (238 ms). The arena's teardown is the cheap part: dropping a
   window is **one arena reset**, where Rust's RAII must `drop` all ~8 000 `Vec`s + the
   table per window, Go's GC must trace the garbage, and C must `free` each list. That
   O(1)-per-window teardown is real and is why a manually-freed or GC'd language does
   not pull away despite other advantages. **But** the hashers differ and that confounds
   the time column: C uses a trivial multiplicative hash, hier its int hash, **Rust the
   default SipHash (cryptographic, DoS-resistant — and slow)**. A Rust dev reaching for
   `FxHashMap`/`ahash` would likely drop below hier. So the defensible time claim is
   *"the bulk-free model keeps hier in the C–Go band on a churn workload"*, not that it
   out-runs tuned Rust. (C's lead is its trivial hash + no teardown traversal at all —
   `free` of a never-reallocated-past list is cheap, and its peak is lowest.)

## The two workloads bracket the model's envelope

- **invindex (build-and-hold):** arena's worst case — ~1.7× C memory on naive growth
  (the held index can't be bulk-freed mid-build), closing to ~1.07× C with a `reserve`
  count-fill pass.
- **winagg (churn / transient):** arena's best case — peak stays flat at one window
  (~par C, beats Go), and the O(1)-per-window bulk-free teardown keeps time in the C–Go
  band, ahead of default-hasher Rust and Go.

Neither is "hier wins everywhere." Together they map the boundary honestly: the
value-semantic implicit arena is competitive-to-excellent when lifetimes are
scope-aligned and churned (its design point), and pays a sizing penalty only on
long-lived hold-and-grow structures — recoverable there with the same capacity hint
every language keeps for the hot path.

## Reproduce

    cc -O2 -o /tmp/peakrss bench/peakrss.c
    ./hierc bench/winagg/winagg.hi -o /tmp/h && /tmp/peakrss /tmp/h
    cc -O2 -o /tmp/c bench/winagg/winagg.c && /tmp/peakrss /tmp/c
    rustc -O -o /tmp/r bench/winagg/winagg.rs && /tmp/peakrss /tmp/r
    ( cd bench/winagg && go build -o /tmp/g winagg.go ) && /tmp/peakrss /tmp/g
