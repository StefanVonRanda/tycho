# flat-state — measured 2026-11-13

One logical state, two layouts, identical semantics and checksum. Full
description and run instructions: [README.md](README.md).

Machine: **AMD Ryzen AI MAX+ 395** (32 threads, Zen 5), Fedora Linux 45,
`cc` GCC 16.2.1. Compiler: `tychoc1` 0.9.2 (`tychoc` is a byte-identical copy
of the same build). `RUNS=9`, layouts interleaved and order alternated;
peak RSS via `bench/peakrss`; the table shows medians. All runs emit
`checksum=915173384 live=11673 tracks=186768`.

## Results

| layout | peak RSS build | peak RSS load | wire | build | query | save | load | teardown (build) | teardown (load) |
|--------|---------------:|--------------:|-----:|------:|------:|-----:|-----:|-----------------:|----------------:|
| flat   | 40.9 MiB | 13.8 MiB | 3,314,468 B | 10.36 ms | 4.07 ms | 7.67 ms | 3.69 ms | 0.91 ms | 0.27 ms |
| nested | 112.7 MiB | 18.9 MiB | 4,427,262 B | 7.19 ms | 3.90 ms | 25.94 ms | 12.03 ms | 18.50 ms | 18.28 ms |

Arena high-water for the build (same runs, `TYCHO_ARENA_STATS=full`): **46.3
MiB flat, 141.5 MiB nested**. The same setting on load gives **9.4 vs 17.0
MiB**, with 11.7k allocations flat against 1.44 M nested. Wall including
process startup and all phases: 29 vs 48 ms build, 9 vs 17 ms load.

Ratios, flat / nested:

| metric | ratio | reading |
|--------|------:|---------|
| build peak RSS | 0.36 | −64% |
| load peak RSS | 0.73 | −27% |
| wire bytes | 0.75 | −25% |
| save | 0.30 | 3.4× faster |
| load | 0.31 | 3.3× faster |
| build phase | 1.44 | 1.44× slower |
| query | 1.04 | parity |
| teardown, build | 0.049 | 20× cheaper |
| teardown, load | 0.015 | 67× cheaper |

## Reading it honestly

**Memory is a build-time win and a loaded-state win.** During build, the
nested program creates a string per track field (196,608 track titles alone),
a `[Track]` per album with growth slack, and 24-byte `Track` values. The flat
program has one text blob, one 16-byte-per-track array, and one pool of 24-byte
album records. Hence 112.7 vs 40.9 MiB. After a load the gap is 18.9 vs 13.8
MiB RSS and 17.0 vs 9.4 MiB arena: the nested loader still allocates 1.44 M
times (48.4 MiB bumped) because `get_str` copies each length-prefixed string
and every album owns a fresh `[Track]`; the flat loader now allocates 11.7k
times and copies each packed array once.

**Save is one bulk pass per array, with two compactions left.** Names and
artists convert and append whole. Albums still need staging because the pool
has holes and the wire rewrites `track_off` compacted; tracks still need
staging because a removed album leaves its range behind in `st.tracks`. Both
staged arrays then go through one `to_bytes([packed T])` call. Flat is 7.67 vs
25.94 ms on a wire 25% smaller, which is now mostly the two staging copies plus
`core:buffer`'s `[int]` to `bytes` conversion; nested converts per field. The
layout difference is real, but the tool still shapes the absolute time.

**Teardown is the loudest signal.** The nested state owns 11,673 album arenas
and their track arrays; the flat state owns a handful of arrays and a pool.
In the same run the runtime reports 18.50 vs 0.91 ms after build, and 18.28 vs
0.27 ms after load. This is the PWP talk's "O(1) deinit" claim showing up in a
Tycho workload: flat state frees by scope, nested state frees by node.

**Build CPU is the cost, and it is interning.** The flat build is 10.36 vs
7.19 ms. `intern_of` hashes every candidate name — about 210k calls for 2,072
unique strings. A workload that already has dense integer IDs would not pay
this; a workload that materializes names from scratch does. Do not quote the
flat layout as a build speedup.

**Query is not a win here.** Both layouts hash the same bytes; the flat path
converts two `i32`s per name. The medians are 4.07 vs 3.90 ms, inside the
run-to-run spread, and the flat path stays slower on the loaded state. At this
corpus size the checksum walk is not where the layout pays.

**Deletion is a policy, not free.** The flat build tombstones the pool slot and
leaves the removed album's track range in the global array (196,608 records
built, 186,768 live). Save writes only live albums, and load compacts and
rebases offsets. That is 158 KB of dead records in build — a deliberate
tradeoff for stable offsets, not an accident.

## The bridge

Before V2b, `to_bytes` accepted a string, `[int]`, or one packed struct, and
`from_bytes$` accepted one packed struct, so save and load both visited every
record. Three changes are measured in the numbers above:

- `push(dst: [int], src: bytes)` appends a whole byte source in one call and
  evaluates the source once.
- `to_bytes([int])` and `buffer.as_bytes` allocate and fill once instead of
  twice.
- `to_bytes(xs: [packed T])` and `from_bytes$([T])(b)` move the whole array in
  one allocation and one memcpy, little-endian per record, refusing a length
  that is not a whole number of records by name.

Their combined effect: save 15.86 → 7.67 ms, load 5.85 → 3.69 ms, build peak
RSS 69.2 → 40.9 MiB, load peak RSS 16.8 → 13.8 MiB, loaded arena 12.2 → 9.4
MiB and 201k → 11.7k allocations.

What the bridge did not remove: `core:buffer` is still `[int]`, so every wire
byte is staged in an 8-byte cell and then converted to `bytes` for the write,
and the compacted arrays are a second full copy of the track bytes. A
bytes-native buffer, or writing each packed array directly, would delete one of
those copies. That is a projection, not a measurement; the bench as committed
is the number to quote.

The compiler-side analogue is in
[`docs/internals/probe-flat-state-2026-11-13.md`](../../docs/internals/probe-flat-state-2026-11-13.md):
the AST is the nested layout, `_flat_int_add` alone bumped 551.7 MiB over
14.1 M allocations after its allocation fix (down from 884.4 MiB over 25.6 M),
and teardown remains a front-end cost.

## Reproduce

```sh
sh bench/flat-state/run.sh                 # 9 interleaved runs
TYCHOC=./tychoc sh bench/flat-state/run.sh # same build, same checksum and bytes
make bench-flat-state
```

The harness prints min..max spreads next to the medians. Absolute times track
this host's `powersave` governor and move by up to ~2.5× between sessions; in
the final build the RSS spread was 40.4–41.2 / 112.4–113.2 MiB and the wire
size, teardown ratio and checksum did not move.
