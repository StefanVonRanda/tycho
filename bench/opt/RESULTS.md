# Compiler-optimization results

## O1 — immortal allocation + literal lifetime elision (SHIPPED 0.8.8)

Date: 2026-10-20. Host: Fedora Linux 44 x86-64, AMD Strix Halo (Ryzen AI MAX+ 395).
Compiler: `tychoc 0.8.8` at the working tree.

Fixture: `bench/litarr_build.ty` — 6,000,000 `push(b.tags, "literal")` calls into
a projected `[string]` field. Output is `6000000` in every build.

Method: build the fixture with the pre-change runtime and with the immortal
runtime, then run each binary five times under `bench/peakrss.c` and take the
median. The fixture, compiler source and machine are otherwise identical.

```text
cc -O2 -o /tmp/peakrss bench/peakrss.c
./tychoc1 bench/litarr_build.ty -o /tmp/lit_before   # pre-change tree
./tychoc1 bench/litarr_build.ty -o /tmp/lit_after    # immortal runtime
for i in 1 2 3 4 5; do /tmp/peakrss /tmp/lit_before; done
for i in 1 2 3 4 5; do /tmp/peakrss /tmp/lit_after;  done
```

| build | output | peak RSS (median of 5) | wall time (median of 5) |
|---|---:|---:|---:|
| copy before | 6000000 | 207,460 KB | 61 ms |
| immortal pointer | 6000000 | **114,056 KB** | **28 ms** |

That is **45% less peak RSS and 54% less wall time** on a literal-heavy builder.

### What shipped

- `TYCHO_LIT` stores a **negative** length header. `tycho_str_len` masks it to
  the real length; no other representation changed.
- `tycho_str_copy` returns an immortal pointer unchanged: strings are immutable,
  so sharing is value-identical.
- `tycho_str_recycle` skips immortal pointers; `tycho_str_slot_set`,
  `tycho_arr_str_set`, `push`/`pop`, concat, equality, split, hash and print all
  read through `tycho_str_len`.
- No compiler branch was needed. The existing copy sites inherit the elision;
  both compilers benefit.

### Gates

`make ci N=0` green; `make test` 1156 passed / 0 failed; `make corelib` 46 ok;
`make parse-check` 1505 files green; `make bench` all bounds; `make docs-fences`,
`make check-links`, `make surface-check`, `make goldens-check` green.

## O2 — read-only pass-by-pointer (SHIPPED 0.8.8)

Date: 2026-10-20. Host: Fedora Linux 44 x86-64, AMD Strix Halo (Ryzen AI MAX+ 395).
Compiler: `tychoc 0.8.8`.

Fixture: `bench/opt/bigparam/main.ty` — 5,000,000 calls to
`fn first(xs: [256]int) -> int`, which reads one element. The emitted C is
compiled with `-O2 -fno-inline` so the call cannot be inlined away and the ABI
copy is the measured cost.

```text
./tychoc bench/opt/bigparam/main.ty --emit-c -o /tmp/bp_after
TYCHOC_NO_PTR_PARAMS=1 ./tychoc bench/opt/bigparam/main.ty --emit-c -o /tmp/bp_before
./tychoc1 bench/opt/bigparam/main.ty --emit-c -o /tmp/bp_shipped
cc -O2 -fno-inline -std=c11 -o /tmp/bp_after.bin /tmp/bp_after.c
cc -O2 -fno-inline -std=c11 -o /tmp/bp_before.bin /tmp/bp_before.c
cc -O2 -fno-inline -std=c11 -o /tmp/bp_shipped.bin /tmp/bp_shipped.c
```

| build | output | peak RSS (median of 10) | wall time (median of 10) |
|---|---:|---:|---:|
| value copy before | 5000000 | 1,400 KB | 140 ms |
| pointer borrow | 5000000 | 1,400 KB | **66 ms** |
| packaged self-hosted (`tychoc1`) | 5000000 | 1,412 KB | **10 ms** |

That is **53% less wall time** in the reference compiler; RSS is unchanged
because the array is one live value either way. The medians are over ten runs
(two batches of five) to damp host frequency drift. With default `-O2` the tiny
callee is inlined and the copy disappears, which is why the fixture disables
inlining: the ABI matters for calls C would otherwise not inline.

The pointer ABI is bridged through named-function value thunks, lambda thunks
and spawn trampolines; FFI and `inout`/`sink` keep their existing ABI.

**What the release actually contains:** the native archive ships `tychoc1`
(the self-hosted compiler), not the reference compiler. `tychoc1` already
lowers `[N]T` to a 24-byte dynamic-array descriptor, so it passes the array
without copying its 2 KB payload and measures 10 ms here. O2 is therefore a
reference/bootstrap-compiler parity change: it removes the 2 KB copy from
`./tychoc` and brings it to the representation the shipped compiler already
had. The packaged binary's behavior and performance are unchanged.

## O3 — last-use moves into `push` (SHIPPED 0.8.8)

Date: 2026-10-20. Host: Fedora Linux 44 x86-64, AMD Strix Halo (Ryzen AI MAX+ 395).
Compilers: `tychoc 0.8.8` and the packaged `tychoc1 0.8.8`.

Fixture: `bench/opt/movebuild/main.ty` — 500,000 loop-local `Item`s with four
128-byte strings each, built only to be pushed into an outer `[Item]`. The
control `TYCHOC_NO_MOVE_PUSH=1` keeps the old allocation in the loop scratch
and the deep copy into the array.

```text
./tychoc1 bench/opt/movebuild/main.ty -o /tmp/mv_after
TYCHOC_NO_MOVE_PUSH=1 ./tychoc1 bench/opt/movebuild/main.ty -o /tmp/mv_before
./tychoc bench/opt/movebuild/main.ty -o /tmp/mvr_after
TYCHOC_NO_MOVE_PUSH=1 ./tychoc bench/opt/movebuild/main.ty -o /tmp/mvr_before
```

| build | output | peak RSS (median) | wall time (median) |
|---|---:|---:|---:|
| `tychoc1`, value copy | 500000 | 327,000 KB | 91 ms |
| `tychoc1`, adopt | 500000 | 327,000 KB | **79 ms** |
| `./tychoc`, value copy | 500000 | 327,000 KB | 96 ms |
| `./tychoc`, adopt | 500000 | 327,000 KB | **87 ms** |

That is **13% less wall time** in the shipped self-hosted compiler and **9%**
in the reference compiler. RSS is unchanged: both builds retain the same final
array; the copy is a transient in the loop scratch, so it costs bandwidth and
allocator work rather than peak live memory.

The loop-local gate is `declared at the current loop depth`; a local from
outside the loop still copies. `tests/move_loop_local.ty` covers the local
adoption and the cross-arena `sink` push, which must copy because the caller's
array outlives the callee's scratch.

## O4 — borrowed in-scope slices (SHIPPED 0.8.8)

Date: 2026-10-20. Host: Fedora Linux 44 x86-64, AMD Strix Halo (Ryzen AI MAX+ 395).

Fixture: `bench/opt/sliceparse/main.ty` — a 12.8 MB string sliced 100 times
with `s[0:len(s)]`; each slice is consumed by `len` and discarded. The control
`TYCHOC_NO_SLICE_VIEW=1` copies the 12.8 MB field; the optimized path stores a
16-byte `(parent, length)` view.

```text
./tychoc1 bench/opt/sliceparse/main.ty -o /tmp/sl_after
TYCHOC_NO_SLICE_VIEW=1 ./tychoc1 bench/opt/sliceparse/main.ty -o /tmp/sl_before
./tychoc  bench/opt/sliceparse/main.ty -o /tmp/slr_after
TYCHOC_NO_SLICE_VIEW=1 ./tychoc bench/opt/sliceparse/main.ty -o /tmp/slr_before
```

| build | output | peak RSS (median of 5) | wall time (median of 5) |
|---|---:|---:|---:|
| `tychoc1`, copy | 1280000000 | 42.9 MB | 33 ms |
| `tychoc1`, view | 1280000000 | **30.6 MB** | **7 ms** |
| `./tychoc`, copy | 1280000000 | 43.0 MB | 32 ms |
| `./tychoc`, view | 1280000000 | **30.7 MB** | **6 ms** |

That is a **79% wall-time drop and 29% lower peak RSS** in the shipped
compiler. The view itself is 16 bytes in the call arena; the 12.8 MB payload
is never copied. A binding, return, `push`, map key or FFI call goes through
`tycho_str_copy`, so it still copies. `tests/pkg/slice_view` covers both the
view path (`println`, `len`, `strings.to_upper`) and the materialization path
(binding, return, `push`).

## O5 — destination passing through owned self-rebuilds (SHIPPED 0.8.8)

Date: 2026-10-20. Host: Fedora Linux 44 x86-64, AMD Strix Halo (Ryzen AI MAX+ 395).
Compilers: `tychoc 0.8.8` and the packaged `tychoc1 0.8.8`.

`t = f(t, ...)` where `t` is read once in the RHS and immediately rebound is a
self-rebuild: the caller cannot observe the old value again. Both compilers
generate a variant of `f` whose corresponding parameter is OWNED. Inside that
variant a field or match payload of the old value may occupy the result's
owning slot instead of being deep-copied; the move is only legal inside a
`return`, so it is terminal, and the result is built in the assignment
target's arena, so the moved buffer and the result share a lifetime. Recursive
calls that consume the changed child select the same variant. `TYCHOC_NO_DEST_PASS=1`
is the control and generates no variants.

Fixture `bench/opt/treeupdate/main.ty` inserts 12,000 keys into a binary tree.
The control reconstructs both children at every level (O(n) per insert, O(n^2)
for the build); the owned variant moves the unchanged child and walks only the
changed path.

Fixture `bench/opt/editorupdate/main.ty` is a 16,384-leaf rope edited 10,000
times through `t = set(t, i, v)`. The control copies the whole line per
keystroke; the owned variant moves the untouched sibling and allocates one
path node per edit.

```text
cc -O2 -o /tmp/peakrss bench/peakrss.c
./tychoc1 bench/opt/treeupdate/main.ty -o /tmp/tree_after
TYCHOC_NO_DEST_PASS=1 ./tychoc1 bench/opt/treeupdate/main.ty -o /tmp/tree_before
./tychoc  bench/opt/editorupdate/main.ty -o /tmp/ed_after
TYCHOC_NO_DEST_PASS=1 ./tychoc bench/opt/editorupdate/main.ty -o /tmp/ed_before
for i in 1 2 3 4 5; do /tmp/peakrss /tmp/tree_before; done
for i in 1 2 3 4 5; do /tmp/peakrss /tmp/tree_after;  done
```

`treeupdate` (12,000 inserts; output `12000 393127152` in every build):

| build | peak RSS (median of 5) | wall time (median of 5) |
|---|---:|---:|
| `tychoc1`, control | 2,252,644 KB | 500 ms |
| `tychoc1`, owned variant | **8,908 KB** | **3 ms** |
| `./tychoc`, control | 2,252,484 KB | 493 ms |
| `./tychoc`, owned variant | **9,112 KB** | **3 ms** |

`editorupdate` (10,000 edits over 16,384 leaves; output
`-7183164655047012628` in every build):

| build | peak RSS (median of 5) | wall time (median of 5) |
|---|---:|---:|
| `tychoc1`, control | 8,966,776 KB | 1,800 ms |
| `tychoc1`, owned variant | **7,584 KB** | **3 ms** |
| `./tychoc`, control | 8,966,596 KB | 1,783 ms |
| `./tychoc`, owned variant | **7,748 KB** | **4 ms** |

That is a **99.6% peak-RSS drop on the tree** and **99.9% on the editor rope**,
with identical output and no change to the source programs. `tests/dest_pass.ty`
pins the boundary: a snapshot taken before the call stays independent, a call
whose result is bound to another name still copies, and a heap struct field
moves into the rebuilt aggregate only on the unique path.

## O6 — conservative shared-bit CoW / path copying (SHIPPED 0.8.8)

Date: 2026-10-20. Host: Fedora Linux 44 x86-64, AMD Strix Halo (Ryzen AI MAX+ 395).
Compilers: `tychoc 0.8.8` and the packaged `tychoc1 0.8.8`.

The shared bit is the **sign of a composite map's `ecap`**, so no map
descriptor grew and the existing 64-byte layout is unchanged. A same-arena
`b := a` of a shareable map, or of a struct whose mutable fields are maps, marks
both descriptors shared. The first write through either (`put`, `del`,
`reserve`, `slotptr`) copies the map's key/value/live/index arrays into the
destination arena, clears the bit, and marks every copied value's nested maps
shared; a composite read from a shared map returns a shallow value and marks
the source. A stale bit can only over-copy, never under-copy — no reference
count is involved. Arrays have no shared bit and still deep-copy; scalar maps
and non-shareable aggregates keep the old path. `TYCHOC_NO_COW=1` is the
control.

Fixture `bench/trie/path/main.ty` builds a 2,000-word persistent trie with
`t = insert(t, s, 0)`. The control copies each map and its subtree per insert;
the CoW build copies only the maps on the changed path and shares untouched
children. Output is `5954 1981` in every build.

```text
./tychoc1 bench/trie/path/main.ty -o /tmp/path_after
TYCHOC_NO_COW=1 ./tychoc1 bench/trie/path/main.ty -o /tmp/path_before
./tychoc  bench/trie/path/main.ty -o /tmp/path_ref_after
TYCHOC_NO_COW=1 ./tychoc bench/trie/path/main.ty -o /tmp/path_ref_before
for i in 1 2 3 4 5; do /tmp/peakrss /tmp/path_before; done
for i in 1 2 3 4 5; do /tmp/peakrss /tmp/path_after;  done
```

| build | output | peak RSS (median of 5) | wall time (median of 5) |
|---|---:|---:|---:|
| `tychoc1`, control | 5954 1981 | 1,673,340 KB | 473 ms |
| `tychoc1`, CoW | 5954 1981 | **12,640 KB** | **4 ms** |
| `./tychoc`, control | 5954 1981 | 1,673,124 KB | 482 ms |
| `./tychoc`, CoW | 5954 1981 | **12,448 KB** | **5 ms** |

That is a **99.2% peak-RSS drop and 99.1% wall-time drop** on the persistent
trie, with byte-identical output. The destructive `bench/trie` row is unchanged
(58.6 MB in the same run, against 58.9 MB recorded before O6), so mutation
through `inout` did not regress. `tests/cow_maps.ty` pins snapshots, nested
maps, `delete`, string values and missing-key defaults; `tests/dest_pass.ty`,
`tests/map_missing_composite.ty` and `tests/inout_string.ty` stay green.

The first O6 cut also took the shallow-read path for scalar `[int:int]` reads
inside an `inout` helper. That marked the map shared, and the next write copied
all 200k entries: `bench/lru` went from ~20 MB to **121 GB** and OOM-killed the
host. A scalar/string value has no nested mutable part to protect, so the share
path is now gated on the value having a share helper. LRU is back to **20.4 MB /
139 ms** (no-cow control 20.9 MB), while the persistent trie above still gets
its path-copy win. `tests/cow_scalar_share.sh` asserts neither compiler emits
`get_share` for the scalar shape, and `bench/lru/run.sh` fails above 256 MB.

## O7 — loop-grown array and map-literal presizing (SHIPPED 0.8.8)

Date: 2026-10-20. Host: Fedora Linux 44 x86-64, AMD Strix Halo (Ryzen AI MAX+ 395).
Compilers: `tychoc 0.8.8` and the packaged `tychoc1 0.8.8`.

`ys := []T` immediately followed by a counted loop that pushes into `ys` is
presized to the loop's proven upper bound:
`tycho_arr_*_reserve(owner, &ys, len(xs))`. The proof is the same one
bounds-check elision uses (start 0, unit step, `i < len(src)`, no reassignment
of `src`/`i`, no whole-`src` call). Over-reserving is safe: `reserve` only
grows, and a conditional or repeated push still grows. `TYCHOC_NO_PRESIZE=1` is
the control.

Fixture `bench/prongB/arr_pipeline.ty` (100,000-element `xs`, 200 passes that
build a fresh `ys` and sum it); output `218777` in every build:

| build | peak RSS (median of 5) | wall time (median of 5) |
|---|---:|---:|
| `tychoc1`, control | 6,544 KB | 30 ms |
| `tychoc1`, presized | **4,280 KB** | **24 ms** |
| `./tychoc`, control | 6,496 KB | 26 ms |
| `./tychoc`, presized | **4,300 KB** | **20 ms** |

That is **~34% less peak RSS and ~20% less wall time** on the flat array
pipeline.

A map literal is now presized to its pair count in the reference compiler too
(`tychoc1` already did this). Fixture `bench/opt/smallmap/main.ty` rebuilds a
six-entry map literal 200,000 times: `./tychoc` **10 ms → 6 ms** with
`TYCHOC_NO_MAP_PRESIZE=1` as the control; `tychoc1` is unchanged because it
already presized. Output `20001100000` in every build.

**Bounds and SROA, measured.** The existing monotone-index elision already
removes the checks in the fixture's `len(xs)` loops (`TYCHOC_NO_BOUNDS_ELISION=1`
is its control); a `n := len(A)`-bound probe measured 15 vs 16 ms at four
million accesses, so no new elision shape was added. SROA was evaluated:
fixed arrays and structs are already inline, and a two-element dynamic array
built in loop scratch measured 6 ms / 1.9 MB against 7 ms / 1.9 MB for the
inline fixed array — no gap for non-escaping small values. A small-map SROA
would need a second map representation; the small-map loop is already flat and
the demand gate does not justify it. O7 ships the allocation presizing only,
where the loop bound proves an upper bound and over-reserving is safe.
`tests/presize_loop.ty` pins both shapes.


## Data-layout experiments (post-0.8.9, not shipped)

These are the first checks for the Goose-inspired data-layout work. They are
observations, not language changes.

Host: Fedora Linux 44 x86-64, AMD Strix Halo (Ryzen AI MAX+ 395). Compiler:
`tychoc1 0.8.9`. Commands:

```text
./tychoc1 bench/opt/entityids/main.ty -o /tmp/entityids
./tychoc1 bench/opt/layouts/main.ty -o /tmp/layouts
cc -O2 -o /tmp/peakrss bench/peakrss.c
for i in 1 2 3 4 5; do /tmp/peakrss /tmp/entityids raw; done
for i in 1 2 3 4 5; do /tmp/peakrss /tmp/entityids pool; done
for i in 1 2 3 4 5; do /tmp/peakrss /tmp/layouts aos; done
for i in 1 2 3 4 5; do /tmp/peakrss /tmp/layouts soa; done
```

`core:pool` is already present and packs index plus generation into a stable
handle. It is not a raw-index replacement for a hot loop:

| 200k entities, 12 passes | peak RSS (median) | wall time (median) |
|---|---:|---:|
| raw array index | 12,956 KB | 4 ms |
| `core:pool` handle + `get` | 23,508 KB | 10 ms |

The generation check is the cost. **Decision:** keep the pool for sparse
lifetimes, entity graphs and editor/UI selection; use raw indices or handles
only at boundaries in the per-frame hot loop. Do not make generation checks
implicit in array indexing.

For a full-field particle update, contiguous AoS beat SoA:

| 200k particles, 24 steps | peak RSS (median) | wall time (median) |
|---|---:|---:|
| `[Particle]` (AoS) | 23,712 KB | 6 ms |
| `soa [Particle]` | 23,448 KB | 11 ms |

**Decision:** do not change Tycho's default layout or add a language-level
inline record/SoA mode from these numbers. SoA is a tool for field-selective
systems; the existing explicit `soa` syntax is enough. The AoS default stays.

The package item is implemented as `make shadercross`, which runs the pinned
translator build. It is a build-dependency convenience, not a benchmark.

### What stuck

- **Typed generation IDs: conditional.** `core:pool` already implements the
  index+generation handle. Keep it for sparse lifetimes, graphs, editor/UI
  selection and boundaries; keep raw indices in the per-frame entity hot loop.
  No implicit generation check on arrays.
- **A Goose-style layout change: no.** The full-field update benchmark favors
  the existing AoS default. Explicit `soa` remains available for field-selective
  systems; no language-level inline/variable-record mode is justified by this
  evidence.
- **Package automation: yes.** `make shadercross` builds and installs the pinned
  SDL_shadercross runtime; this removes the manual setup step for `core:gfxgpu`.

Items 4–5 are deliberately not shipped. A fourth caller is only useful if a
physics or UI requirement forces a shared package; the current three software
callers and two GPU callers do not. References or inline variable-size records
are a language-model change, and the entity/layout measurements do not support one.
