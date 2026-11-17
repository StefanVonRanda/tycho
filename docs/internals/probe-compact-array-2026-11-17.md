# Compact array descriptor probe, 2026-11-17

**Verdict: go, with conditions. An 8-byte prefixed-pointer `[T]` cuts peak RSS by 37% on a struct with three mostly empty array fields, 13% on `bench/dijkstra`-shaped adjacency and 11% on nested `[Item]` in `[Order]`. Time is flat on two workloads and 10-30% slower on the third. Two things must be designed before the emitter work starts: zero-copy slice views, and the runtime's own typedefs. A `TYCHOC_NO_COMPACT_ARRAY` control is not practical.**

This is the measurement half of plan phase 3.1
([plan-goose-transfers.md](plan-goose-transfers.md)). The emitter change has
not been started. Host: the macOS arm64 laptop, `cc -O2`, other agents running
on the machine at the same time, so the times are noisy.

## What the probe models

[`bench/compact_array/probe.c`](../../bench/compact_array/probe.c) builds as
two binaries from one source:

- **wide** (today): `{data, len, cap}`, 24 bytes, the same layout as
  `TychoArrInt` (`runtime/tycho_rt.c` ~2127) and the emitter's per-type
  `struct ct_ { T *data; tycho_int len; tycho_int cap; }`.
- **compact** (`-DCOMPACT`): one pointer to a buffer laid out as `{len, cap}`
  followed by the elements. An empty array is `NULL` and allocates nothing.
  `len(xs)` becomes `xs ? xs->len : 0`.

Both binaries use the same arena model, copied from `tycho_rt.c`: 64 KiB bump
blocks (`TYCHO_BLOCK_DEFAULT`), sizes rounded to 8 bytes, growth `0 -> 4 -> x2`
with the old buffer recycled the way `arena_recycle` does it (an uncapped
per-class bucket below 128 B and a 32-entry best-fit list above). The compact
buffer's 16-byte header is counted in its size, so it is recycled and reused
with the elements.

Workloads, one checksum each, identical across the two layouts:

| workload | shape |
|---|---|
| `struct3` | 1M `Rec { id, tags, refs, hist }` in a `[Rec]`. `tags` is 60% empty, else 1-3 ints. `refs` is 80% empty, else 1-8. `hist` has 0-2. Then 5 read passes. |
| `adj` | `bench/dijkstra`: n = 300k, degree 4, `[[Edge]]`, then Dijkstra with an array-backed binary heap. |
| `orders` | 1M `Order { id, items: [Item], total }` in an `[Order]`, 20% with no items, else 1-5 `Item {sku, qty, price}`. Then 5 total passes. |

Run with `sh bench/compact_array/run.sh`. It builds `bench/peakrss.c`, runs
each binary five times, takes the median, and fails if the checksums differ.

## Results

Median of 5. Two full runs shown, because the machine was shared.

| workload | wide MB | compact MB | delta | wide ms (run 1 / run 2) | compact ms (run 1 / run 2) |
|---|---:|---:|---:|---:|---:|
| `struct3` | 199.7 | 125.4 | **-37%** | 57 / 67 | 63 / 87 |
| `adj` | 41.2 | 35.7 | **-13%** | 66 / 76 | 62 / 75 |
| `orders` | 168.0 | 149.0 | **-11%** | 55 / 70 | 54 / 70 |

Where the bytes go:

- `struct3`: `Rec` goes from 80 to 32 bytes. Most of its arrays are empty, and
  an empty array costs nothing in compact, so the whole 48-byte saving per
  record is kept. Peak RSS includes the doubling `[Rec]` spine (old and new
  copies are both live during a grow), which is why the RSS saving is a bit
  smaller than the struct saving.
- `adj`: each inner `[Edge]` header is 24 -> 8 bytes (300k x 16 = 4.8 MB),
  less the 16-byte prefix every non-empty buffer now carries. The edges
  themselves (300k x 4 x 16 B, plus growth slack) do not change.
- `orders`: `Order` goes from 48 to 32 bytes, but 80% of orders have items, and
  each of those buffers grows 16 bytes. The net saving is smaller than in
  `struct3`.

The rule is that compact wins in proportion to the number of empty arrays, and
by about 16 bytes per non-empty array value that is stored rather than local.
An array of arrays where every inner array is long gains almost nothing.

**Time.** `struct3` was slower in both runs, by 10% and 30%. The likely cause
is the `NULL` test inside every `len()` in the read loop, plus one extra load
to reach `len` through the pointer. The other two workloads are flat within
noise. A real emitter can hoist `len` and `data` out of a loop the way
push-loop fusion already caches `*data`/`*cap`, so this should be re-measured
on emitted code, not assumed.

## Implementation sketch

**Runtime (`runtime/tycho_rt.c`, `corelib/tycho.h`).** `TychoArrInt`,
`TychoArrFloat` and `TychoArrStr` (~2127, ~2311, ~2404) and every function that
takes or returns them by value (`_with_cap`, `_push`/`_push_slow`, `_grow`,
`_reserve`, `_copy`, `tycho_bytes_from_intarr`, about 78 `.data/.len/.cap`
uses) change to a `Hdr *`. `with_cap(0)` returns `NULL`. `_grow` moves from
`(T **data, tycho_int *cap, len)` to `(Hdr **)`. `arena_recycle` sizes gain
`+ sizeof(Hdr)`.

**Emitter (`compiler/emit/emit.ty`).**

- Array type definition (~9774, `struct ct_ { et *data; ... }`) becomes a
  pointer typedef to a header struct.
- Every emitted `.data[`, `.len`, `.cap` (about 19 `.data[`, 34 `.len`, 9 `.cap`
  in string templates) is rewritten through three macros (`TY_ALEN`,
  `TY_ADATA`, `TY_ACAP`), so the layout lives in one place.
- Push-loop fusion grow hooks (~9791) take `Hdr **`. The cached `data` pointer
  must be reloaded after every grow, because the header moves with the buffer.
- Bounds checks read `len` through a possibly `NULL` pointer. That is a cheap
  test, but every site needs it.
- **SoA arrays** (~9687, `{ f0*, f1*, ..., len, cap }`) are a separate
  representation and can stay as they are in the first cut.

**Zero-copy slice views are the blocker.** `xs[a:b]` passed to a read-only
parameter is a zero-copy view today
([arrays-slices.md](../reference/arrays-slices.md) line ~282): a
`{data + a, b - a}` pair in the same 24-byte type. A pointer into the middle of
a buffer has no `{len, cap}` in front of it, so the compact type cannot express
it. There are two ways out:

1. Read-only array parameters take a separate 16-byte `{data, len}` view type,
   and the call site converts. This adds a second ABI to every borrowed array
   parameter.
2. Views copy. This regresses a documented zero-copy guarantee.

Option 1 is the right one, and it is most of the design work.

**FFI POD-array crossing.** This is easy. A `[T]` of a scalar or POD struct
already crosses as `(const T*, length)` ([ffi.md](../reference/ffi.md)). The
marshal becomes `(xs ? (T *)(xs + 1) : NULL, xs ? xs->len : 0)`. C callers
already have to accept `NULL` with length 0. `[string]` is the same with
`char **`.

**ilp32.** On i386, wide is 4 + 8 + 8 = 20 bytes (int64 alignment is 4) and
compact is 4, so the relative saving is bigger. The header stays
`{int64 len, int64 cap}` = 16 bytes. Element data at `hdr + 1` is then
8-aligned, which keeps `double` and `int64` elements aligned as they are today.
`make ilp32` covers this.

**Bootstrap.** `bootstrap/tycho_compiler.c` mentions the runtime array types
about 1,483 times, and the compiler is heavily built on arrays. The runtime ABI
changes, so the bootstrap must be regenerated with the new compiler, and
`make bootstrap-check` and `make fixpoint-check` must pass. The sequence is:
land the runtime and emitter together, build `tychoc1` with the old bootstrap
(it emits new-layout C), regenerate, and check the fixpoint.

**`TYCHOC_NO_COMPACT_ARRAY` control: not practical.** The layout is fixed in
`tycho_rt.c` and `corelib/tycho.h`, which are compiled once and linked against
every program and every corelib shim (`make shim-check`). A per-compile switch
would need two builds of the runtime selected at link time, plus a dual-mode
emitter for all of the sites above. The plan's fallback applies: gate the change
by measurement and the full test set instead, and keep this probe as the
before/after model.

## Go/no-go

**Go**, if these conditions are met:

1. The slice-view ABI (option 1 above) is designed first. It decides whether
   this is a one-week or a one-month change.
2. All array access goes through `TY_ALEN`/`TY_ADATA`, so the layout is in one
   place.
3. The `struct3` time cost is measured again on emitted code with `len`/`data`
   hoisted. If it stays above 10% on the bench suite time rows, that is a
   reason to stop.

Not measured here: the bench suite memory rows (`bench/suite.py`) need the real
emitter.
