# dbquery — hier vs C vs Go over real SQLite

The first head-to-head on a **real C library**, not a pure-language microbench:
hier driving in-memory SQLite through its FFI, against the same program in C
(direct libsqlite3) and Go (cgo → the same libsqlite3).

## Workload

SQLite work is held **constant** across all three ports — what we measure is the
**host-side data handling around it**, which is where the memory models differ
(hier's arena vs C's stack/manual vs Go's GC):

- **Setup:** insert 60,000 rows `(id, cat=id%32, amt=(id·K)%1000, label="u"+id%1000)`
  via one prepared statement in a transaction.
- **Measured loop:** 240 query-rounds. Round `q` selects the rows with `id%240==q`
  (~250 rows), and for each row groups `amt`/count by `cat` and sums label lengths
  — building **transient** per-round tables that are discarded at round end. That
  churn is what exercises per-round arena reclaim / `free` / GC.
- **Output:** a single deterministic checksum (`495113400`) — identical across all
  three ports is the cross-language correctness check.

## Results (60k rows, 240 rounds; `sh bench/dbquery/run.sh`)

| lang | peak RSS | wall | checksum |
|------|---------:|-----:|----------|
| **hier** | **4.4 MB** | ~360 ms | 495113400 |
| C        | 4.3 MB | ~350 ms | 495113400 |
| Go (cgo) | 7.8 MB | ~390 ms | 495113400 |

Stable across runs (peak RSS varies <0.2 MB; wall is cache-noisy ±10 ms).

Re-measured at `-O3` (2026-06-07): hier 4.3 MB/354 ms, C 4.3 MB/352 ms, Go 7.4 MB/387 ms
— unchanged within noise. This workload is SQLite-dominated, so host-code `-O3` barely
moves it (`run.sh` now builds the C port at `-O3`; the hier port already builds via the
`-O3` driver default).

## What it shows

- **hier ≈ C on a real database workload** — within ~0.1 MB of peak RSS and ~3% of
  wall time, with **zero manual memory management** (no `free`, no GC). The
  value-semantic arena gives a C-class footprint on real I/O-bound work.
- **hier < Go** — ~1.8× lighter peak RSS than the GC port doing the identical
  SQLite work, and competitive on time.
- The per-round grouping tables and the arena-copied `label` strings are reclaimed
  per round, so memory stays flat over all 240 rounds rather than growing.

## Honest caveats

- This workload is **SQLite-dominated**: the absolute footprints are small and
  converge, because libsqlite3's own pages dwarf the host structures. So this is
  *not* where the arena's dramatic wins live — those are the pure-language churn
  workloads in `bench/prongB` (trees, array pipelines), where the naive baseline
  is hundreds of MB. Here the point is narrower and arguably more important for
  adoption: **on real work behind a real C library, hier costs what C costs and
  beats the GC, for free.**
- hier pays a real cost C doesn't: every `sx_col_text` return is **arena-copied**
  (the FFI lifetime rule — SQLite's text pointer is valid only until the next
  `step()`), and each row crosses the FFI boundary. Here it's lost in the noise;
  on a hotter loop it would show. C reads the column pointer in place.
- Go uses cgo against the same libsqlite3 (no driver module), so the comparison is
  host-language data handling, not driver quality.
