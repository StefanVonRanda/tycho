# Probe: flat state vs nested values — 2026-11-13

Question: Andrew Kelley's *Programming without Pointers* claims that a program's
whole state can be one root value whose elements contain no pointers — names as
`(offset, length)` into one blob, children as `(offset, count)` into one array —
and that this buys compact encoding, cache density and cheap teardown. Tycho
already removes language-level pointers. Which half of the claim does it already
have, which half is still missing at the representation level, and what does
that imply for the compiler's own data structures?

Host: Linux x86_64, AMD Ryzen AI MAX+ 395, Fedora 45, GCC 16.2.1. Compiler:
`tychoc1` 0.9.2 (`tychoc` 0.9.0 reproduces the same bytes and checksum).
Program: [`bench/flat-state/`](../../bench/flat-state/) — one music library in
two layouts, same corpus, same checksum.

## What the benchmark shows

11,673 live albums / 186,768 live tracks, nine interleaved runs, medians:

| | flat | nested |
|---|---:|---:|
| build peak RSS | 69.2 MiB | 121.4 MiB |
| arena high-water | 70.7 MiB | 145.7 MiB |
| wire | 3.31 MB | 4.43 MB |
| save | 15.9 ms | 32.6 ms |
| load | 5.9 ms | 12.3 ms |
| teardown after build | 1.33 ms | 21.38 ms |
| build phase | 10.05 ms | 6.10 ms |

1. **The element half transfers.** Fixed-size packed records with integer
   offsets are exactly what the talk promises: 43% less build memory, 25%
   smaller wire, 2.1× faster save and load, 16× cheaper teardown.
2. **The root half does not.** The flat `State` still owns four `[T]`
   descriptors and a pool; each descriptor is `{data*, len, cap}` in the
   generated C. The claim "one pointer in the root" is not expressible: Tycho's
   arrays are already the hidden pointers. What the flat layout removes is the
   *per-element* pointer, not the root's.
3. **The steady-state gap is smaller, but not free.** After load, RSS is 18.8
   vs 16.8 MiB, but loaded-state arena high-water is 17.0 vs 12.2 MiB, and the
   nested load makes 1.44 M allocations (48.4 MiB bumped) against 201k (19.0
   MiB). The emitted `h_get_str` copies each string twice — `tycho_str_copy`
   around `tycho_str_substr`, which itself copies — and every album owns a
   fresh `[Track]`. The 43% is mostly a build/serialization gap; do not quote
   it as a loaded-state ratio.
4. **Interning costs CPU.** Flat build is 1.65× slower: about 210k map lookups
   hash 2,072 unique names, and `buffer.write` appends byte by byte. Dense
   integer IDs would skip that; names materialized from scratch pay it.

## What the language already gives

- **Value semantics** — copying an `AlbumRec` copies 24 bytes; there is no
  ownership edge to duplicate or drop per element.
- **Packed structs + the little-endian bytes bridge** — records have an exact
  `size_of$`, so a flat element is a stable on-disk record
  ([spec §17.1b](../../docs/spec/12-aggregates.md)).
- **Slice-to-string conversion** — `to_str(src[at:at + n])` is bounds-checked
  and lowers to one copy (`tycho_str_substr`) in the emitted C. That is cheap
  per name next to a per-record allocation, but it is not free: the nested
  loader stores a string per field and pays two copies per string, while the
  flat loader copies the text blob once and re-derives names by offset.
- **`core:pool`** — generational handles make deletion safe without scanning
  every reference, and the slot stays fixed-size.
- **`core:intern`** — the build-side name deduplication is a library, not a
  language feature; the map is permanent, so scope it with the build.

## What fought back

- **No packed-array ↔ bytes form.** `to_bytes` takes a string, `[int]`, or one
  packed struct; `from_bytes$` takes one packed struct. `to_bytes([TrackRec])`
  is rejected with "takes a string, `[int]` or a packed struct", and
  `from_bytes$([TrackRec])` with "only a packed struct can be read from bytes".
  So save visits every record and stages every byte through `core:buffer`'s
  `[int]` (8 bytes per output byte). This is the largest avoidable cost in the
  probe and the one internal change with a measured target.
- **Per-record tax is real but small.** A throwaway probe (`/tmp/opencode/encodecost`)
  measured 500k × 16-byte records: pack 3.4–3.6 ms (6–7 ns/record), unpack
  1.3–1.5 ms (2–3 ns/record); one `io.read_bytes` of the same 8 MB was
  1.3–2.3 ms. The bridge's value is not the per-call dispatch; it is deleting
  the staging copy and enabling one bulk write.
- **Value semantics blocks zero-copy array views.** A `[Rec]` over a `bytes`
  file buffer would alias, so the bridge must copy. One memcpy is still the
  realistic target; mmap-zero-copy is not.
- **Handle sentinels.** `pool.Handle` is a distinct newtype, so "no handle" is
  `Handle(0)` plus a `bool`, not `-1`. Correct, slightly noisy.
- **`i32` offsets need `to_int` at every use.** Zero-cost after codegen, noisy
  in source; a `NameRef` accessor helper removes it.

## Why this reaches the compiler

The compiler's AST is the nested layout this probe measures. `Expr` and `Stmt`
are enums whose recursive variants carry `[Expr]` / `[Stmt]` payloads
(`compiler/ast/ast.ty`), and `ast.kids` (line 272) flattens them for walkers.
Consequences visible in one front-end run
(`TYCHO_ARENA_STATS=full ./tychoc1 compiler/main.ty --emit-c`):

- 1,148.6 ms wall, 45.0 MiB peak live, **879.5 MiB bumped over 25.6 M
  allocations**, 6.05 M arenas created.
- **121.5 ms in arena teardown = 10.6% of front-end wall** — the PWP teardown
  claim, inverted: the nested AST pays per-node teardown.
- `emit___flat_int_add` is the top bumper: **332.7 MiB / 11,543,428
  allocations**. It starts `parts := [eb]` on every binop, walks the `+` spine,
  then bails when `len(parts) < 128` — most calls allocate and discard.
- `_stmt_es` (line 2165) builds a fresh `conds` array for every `if`, and
  `_stmt_bs` (line 2190) builds a fresh `out` array (and arrays of body arrays)
  on every statement; `ast.kids` has 16 call sites in `emit.ty`.
- The full self-build (front-end through `cc` and link) took 8.9 s on this
  host; front-end allocation is not the top-level wall, but it is the part
  Tycho owns end to end.

A flat AST — one node table, `(kind, first_child, child_count)`, names interned,
side tables for payloads — would let each pass scan contiguous nodes, serialize
directly, and free the tree in one scope. That is the same move as the
benchmark's `TrackRec` array, applied to `Expr`/`Stmt`. It is not a 1.0 change:
it touches every pass and needs an ownership story for the side tables. The
contained version is to reuse per-walker scratch arrays instead of allocating
`parts`/`conds`/`out` per call.

## Verification

`sh bench/flat-state/run.sh` and `TYCHOC=./tychoc sh bench/flat-state/run.sh`
both complete with identical result lines
(`checksum=915173384 live=11673 tracks=186768`), identical wire sizes
(3,314,468 / 4,427,262 bytes), and `after_save == checksum`, `stale=1`.
Peak-RSS spread across the sessions run here: flat build 68.6–69.4 MiB,
nested build 120.6–121.7 MiB; load 16.1–17.0 / 18.7–19.1 MiB. Absolute phase times moved up
to ~2.5× between sessions on this host's `powersave` governor; the ratios above
held. Projections are labeled as projections; no compiler or corelib change was
made for this record (the follow-up bridge is a separate change; see the status
note below).

## Follow-ups, in order of payoff

1. **Bulk packed-array bridge** (`to_bytes([T])` / `from_bytes$([T])` for
   packed `T`): one copy, little-endian, length checked. Removes the `[int]`
   staging buffer and the per-record loop from flat-state save/load. Needs a
   spec decision because it is language surface.
2. **Reusable walker scratch** in `emit.ty` (`_flat_int_add.parts`,
   `_stmt_bs.out`, `_stmt_es.conds`): internal only, likely the cheapest
   allocation win, no surface change.
3. **Flat AST**: the real PWP analogue, but a multi-pass refactor and not a
   1.0 candidate.

**Status, end of 2026-11-13:** follow-up 1 landed (`to_bytes([T])`,
`from_bytes$([T])`, `push([int], bytes)`), and follow-up 2 landed for
`_flat_int_add` (14.1 M allocations / 551.7 MiB bumped, down from 25.6 M /
884.4 MiB); `_stmt_bs.out` and `_stmt_es.conds` remain. The table above is the
pre-bridge run; [`bench/flat-state/RESULTS.md`](../../bench/flat-state/RESULTS.md)
carries the post-bridge numbers (40.9 vs 112.7 MiB build, 3.31 vs 4.43 MB
wire). Follow-up 3 is unchanged.

This record alone changes none of `surface.lock`, `ROADMAP.md`, the compiler,
corelib or the spec; the bridge is a separate change with its own fixtures.
