# Static-site generation — head-to-head (tycho vs C vs Go)

The arena thesis on a **realistic composing workload** rather than a
micro-benchmark: render *N* Markdown pages to HTML. The same block-level renderer
(`# `/`## ` headings, `- ` lists, paragraphs, `& < >` escaping) is written three
times — `site.ty`, `site.c`, `site.go` — and each is fed an identical generated
corpus (`gensite.ty` writes `<i>.md` for `i` in `0..N-1`). Every rendered byte is
folded into an FNV-1a-32 checksum, so **all three must agree** — the comparison is
fair by construction. `bench/peakrss` reports peak RSS + best-of-3 wall.

This is the same workload as the `examples/site` dogfood, reduced to its
allocation-heavy core (the per-page render) so it measures the **language's memory
model**, not corelib (the dogfood already proves the corelib composition). The
renderer is inlined in all three — no corelib imports — so the work is identical.

## Scorecard (N = 5000, this machine)

```
  tycho      1.5 MB     123 ms   pages=5000 bytes=8645000 fnv=4062399490
  c         1.5 MB      63 ms   pages=5000 bytes=8645000 fnv=4062399490
  go        8.2 MB      86 ms   pages=5000 bytes=8645000 fnv=4062399490
```

Identical checksum → identical work. (Run it yourself: `make bench-site`.)

## The story — peak RSS is flat across a 20× scale

| pages  | tycho      | C         | Go        |
|-------:|----------:|----------:|----------:|
|  1,000 |  1.5 MB   |  1.5 MB   |  6.9 MB   |
|  5,000 |  1.5 MB   |  1.3 MB   |  7.8 MB   |
| 20,000 |  **1.5 MB** |  1.6 MB   |  8.5 MB   |

**tycho holds a flat ~1.5 MB while the corpus grows 20×.** Each page is read and
rendered inside the loop body, whose per-scope arena is reclaimed every iteration,
so the working set is one page regardless of *N* — matching C exactly, with **zero
manual `free` and no GC** in the source. Go grows 6.9 → 8.5 MB: a runtime floor
(~6–7 MB of GC structures + stacks) plus GC headroom that tracks the live set. On
this workload tycho's resident set is ~5× smaller than Go's and dead flat.

## The honest trade-off — wall-clock

| pages  | tycho   | C      | Go     |
|-------:|-------:|-------:|-------:|
|  1,000 |  40 ms |  21 ms |  33 ms |
|  5,000 | 199 ms |  98 ms | 142 ms |
| 20,000 | 492 ms | 249 ms | 363 ms |

tycho is ~2× C and ~1.4× Go on wall-clock here. The string-building rebuilds each
growing buffer through arena allocations, where C reuses one `realloc`'d buffer
and Go's `strings.Builder` amortizes growth. **The arena's win on this workload is
memory, not raw speed** — tycho delivers C-flat memory automatically, and pays a
constant-factor on time. (Single-run wall above; the scorecard uses best-of-3, so
its numbers are a touch lower.)

## Caveats

- Peak RSS is `ru_maxrss` via `bench/peakrss`; Go's figure includes its runtime
  baseline, so part of the 5× gap is Go's floor, not pure workload garbage — but
  tycho's *flatness across scale* is the load-bearing claim, and it is unambiguous.
- The renderer is a deliberate Markdown subset; real generators do more, but the
  allocation shape (read → build a transient string per item → discard) is the
  same one most batch text tools have.
- Not in `make ci` (needs a Go toolchain; `make bench-site` runs it, skipping any
  absent compiler).
