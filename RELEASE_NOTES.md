<!--
Draft release notes. Edit this before publishing, then:
  make ci
  make release-check                    # builds twice; smoke-tests and compares the native tarballs
  scripts/release.sh <version> --mingw  # builds and smoke-tests the Windows tarball
  gh release create <version> dist/tycho-<version>-*.tar.gz dist/tycho-<version>-*.sha256 --notes-file RELEASE_NOTES.md
  gh release edit <version> --prerelease
Build one tarball per platform (there is no hosted CI); attach them all to the release.
Name the version in the globs: dist/ still holds the previous release's tarballs.
-->

Tycho 0.8.8 — pre-1.0, no stability guarantees (see the [README](README.md) for
what that means in practice). Prebuilt binaries are attached, so you can try the
language without building from source.

**This release combines the seven pieces of real-program feedback from the
`tycho-dupe` probe with the O1–O7 optimizer sweep, POD struct FFI, and the first
complete game path.** It adds no references and no escape hatches. The language
surface is unchanged in shape; `surface.lock` moved by addition only.

## Breaking

- **`strings.parse_int` is now the strict parser.** It returns
  `Result(int, IntErr)` and refuses empty input, trailing garbage and
  out-of-range values. The old fail-open scanner is now
  `strings.parse_int_prefix`; `parse_int_checked` is gone. Every call site,
  test, example, tool and doc was updated. This is the one change a 0.8.7
  program is likely to notice.
- **`eprintln(s)` is a builtin.** It writes `s` and a trailing newline to
  stderr. `eprint(s + "\n")` still works.
- **`path.real` is implemented through `real_checked`.** Its fail-closed
  behavior is unchanged; the new spelling returns a named `PathErr`.

## What the probe changed

- **`core:io` can now classify a file.** `FileKind`, `Stat`, `stat`, `lstat`
  and `same_file` (device + inode on POSIX, handle identity on Windows) give a
  filesystem tool one syscall for kind, size, mtime and identity. Symlinks,
  hard links, FIFOs, sockets and devices now have answers instead of a C shim.
- **`core:path` can say why a resolution failed.** `real_checked` returns
  `NotFound` / `Denied` / `TooLong` / `BadPath` / `Failed`; `real` remains the
  fail-closed convenience.
- **`path.quote`** gives a POSIX shell-safe single-quoted path for text reports.
- **`tycho-dupe`** is in `tools/tycho-dupe`, with `make dupe-check`. It is the
  probe program, not a demo: content hashing, filesystem identity, JSON output,
  error paths and both compilers.
- **The copy/borrow boundary is written down.** The reference, guide and spec
  now say exactly which values are borrowed and which copy: heap descriptors
  share storage; `[N]T`, `vector[N]T` and inline-array structs copy the whole
  value.
- **`inout` forwarding is documented** with a compiling example.
- **`tools/tycho-dupe/FRICTION-OUTSIDE.md`** records what went right and what
  remains, with status notes for each finding.

## FFI

**Plain scalar-field structs can be `inout` out-parameters.** A non-packed,
non-aligned struct of numeric fields now crosses as a normal `S_Name *`; the C
function fills it in place. Use fixed-width fields (`i32`, `u8`, `f32`) to match
the C header. The same POD struct can cross **by value** and as a **return**;
a dynamic `[POD]` parameter crosses as `(const S_Name *, len)`, borrowed for the
call. This covers SDL_Rect/SDL_FRect/Vector2-style APIs without shimming the
values themselves. Strings, maps, handles, packed/align structs and non-POD
composites remain rejected — use `--shim` for those. `tests/ffi` covers the round
trips and all rejection paths. The windowed `examples/gfx` demo passes
`SDL_FRect` and `SDL_Rect` by value and a `[FVec]` (`SDL_FPoint` layout) array
to `SDL_RenderLines`, exercising the real SDL layouts on the GPU path. C unions
stay non-crossable; the shim decodes `SDL_Event` into a tagged POD `Event`.
The same example queues signed-16-bit PCM through an SDL audio stream, so sound
needs no callback ABI; `SDL_AUDIODRIVER=dummy` makes the test lane silent. Its
asset manifest (`assets/manifest.json`) is parsed with `core:json`; each PNG is
decoded with `core:image`, uploaded as an SDL texture, and looked up through a
`[string: int]` ID cache. One scoped `TexSet` handle owns every texture;
`close(texset)`, `close(audio)` and `close(win)` release the textures, stream
and window in order. `core:image` frees its per-image handle header and keys
the double-free guard by a monotonic live-set ID, so loading the PNG atlas does
not retain one header per decode. Individual typed handles still cannot
live in collections, but a collection handle covers the homogeneous cache. A PNG glyph atlas is decoded by `core:image`, uploaded as an SDL texture, and
drawn glyph-by-glyph for `SCORE n` and `GAME OVER`, with no SDL_ttf. The reusable
layer now lives in `core:gfx` (`corelib/gfx`, auto-linked shim); `examples/gfx/pong`
is a second caller using the same package alongside Snake. `present` is
parameterless, with overlays drawn through the separate `draw_lines` call;
key-to-action mapping and centered text measurement live in `core:gfx`. A
`gfx.Clock` provides fixed-step simulation with a capped catch-up, so both games
simulate by time rather than frame count. Value `Sprite`/`Anim` types and
`draw_sprite`/`anim_frame`/`draw_anim` add sprite-sheet animation; the examples
use four-frame apple and ball sheets. A value `Mixer` with `mixer`/`add`/
`finish`/`play_mix` mixes overlapping PCM in Tycho; both games play a two-note
score jingle without audio callbacks. `repeat`/`play_loop` queue finite PCM
loops for music; the caller re-queues when it wants more. Snake now has a title
menu, three speeds, pause/restart, a persistent high score and letterboxed
resize, with `package.sh` producing a runnable directory.

## Compiler optimizations

**O1 ships in 0.8.8: immortal allocation + literal lifetime elision.** String
literals now carry a negative length header; the runtime masks it, returns an
immortal literal unchanged on copy, and skips it on recycle. Every copy, push
and assign path inherits the elision without a compiler branch.

On 6,000,000 literal pushes into a `[string]` field: **207,460 KB / 61 ms →
114,056 KB / 28 ms** (median of five), byte-identical output. The result is in
`bench/opt/RESULTS.md`.

Making O1 real also exposed a latent escape-analysis bug: `return Env(m)` now
marks `m` as escaping in both compilers, so a mutated map/array local returned
through a zero-cost newtype no longer keeps its storage in a scope that is
freed on the way out. `tests/newtype_agg` is the regression.

The O1–O7 optimization phases are all in 0.8.8; O1 was the prerequisite for
O6, and O7 closes the allocation gap on the flat array pipeline.

**O2: read-only pass-by-pointer in the reference compiler.** It passes
non-heap fixed arrays of four or more elements as `const T*` on direct calls.
On 5,000,000 calls through `fn first(xs: [256]int)`: **140 ms → 66 ms** (median
of ten, `-O2 -fno-inline`), same RSS. Function values, lambdas and spawns
bridge the ABI; FFI, `inout` and `sink` are unchanged.

The native archive ships `tychoc1`, the self-hosted compiler. It already
lowers `[N]T` to a 24-byte descriptor, so it never copied the 2 KB payload:
the same fixture measures **10 ms** with the packaged compiler. O2 therefore
closes the reference compiler's ABI-copy gap and brings `./tychoc` to the
representation the shipped compiler already had; the packaged binary's
performance is unchanged.

**O3: last-use moves into `push`.** A loop-local declared at the current loop
depth is a fresh buffer each iteration. When its only read is `push(dst, x)`,
the declaration is promoted into the destination array's arena and the push
stores it without a deep copy; a heap rebind and a direct `sink` call get the
same relaxation. A local from outside the loop, or a push into a caller-owned
`sink` array, still copies. On `bench/opt/movebuild` (500,000 four-string loop
locals): `tychoc1` **91 ms → 79 ms**, `./tychoc` **96 ms → 87 ms**, same RSS.
`tests/move_loop_local.ty` is the regression for both sides of the boundary.

**O4: borrowed in-scope slices.** A string slice used as a call argument is a
16-byte `(parent, length)` view instead of a copy. Binding, returning, `push`,
map keys and FFI still materialize through the copy seam. On 100 slices of a
12.8 MB string: `tychoc1` **33 ms / 42.9 MB → 7 ms / 30.6 MB**, `./tychoc`
**32 ms / 43.0 MB → 6 ms / 30.7 MB**. `tests/pkg/slice_view` covers both the
view and the materialization sides.

**O5: destination passing for `t = f(t, ...)`.** When `t` is read once in the
RHS and immediately rebound, both compilers generate a variant of `f` whose
matching parameter is owned by the callee. Inside a `return` the variant may
move a field or match payload of the old value into the result instead of
deep-copying it, and a recursive call that consumes the changed child selects
the same variant. The result is built in the assignment target's arena, so a
moved field and the result share a lifetime; a call whose argument is still
live, or whose result goes anywhere else, keeps the copying function.
`TYCHOC_NO_DEST_PASS=1` is the control.

A 12,000-insert binary tree drops from **2,252,644 KB / 500 ms to 8,908 KB /
3 ms** in `tychoc1` (`./tychoc`: 2,252,484 KB / 493 ms → 9,112 KB / 3 ms). A
16,384-leaf editor rope edited 10,000 times through `t = set(t, i, v)` drops
from **8,966,776 KB / 1,800 ms to 7,584 KB / 3 ms** (`./tychoc`: 8,966,596 KB /
1,783 ms → 7,748 KB / 4 ms). Output is byte-identical. `tests/dest_pass.ty`
pins the unique, shared and snapshot cases; the fixtures are under
`bench/opt/treeupdate` and `bench/opt/editorupdate`.

**O6: conservative shared-bit CoW / path copying.** A same-arena `b := a` of a
composite map, or of a struct whose mutable fields are maps, shares the map and
sets a conservative shared bit; the first write through either copy duplicates
the map's arrays and marks nested maps shared. The bit is the sign of the map's
`ecap`, so the descriptor layout is unchanged, and a stale bit can only
over-copy. Arrays, scalar maps and non-shareable aggregates keep the old
deep-copy path; this is a shape-gated implementation optimization, not a value
model change. `TYCHOC_NO_COW=1` is the control.

A 2,000-word persistent trie built with `t = insert(t, s, 0)` drops from
**1,673,340 KB / 473 ms to 12,640 KB / 4 ms** in `tychoc1` (`./tychoc`:
1,673,124 KB / 482 ms → 12,448 KB / 5 ms), with output `5954 1981` in every
build. The destructive `bench/trie` row is unchanged (58.6 MB vs 58.9 MB before
O6). `tests/cow_maps.ty` covers snapshots, nested maps, `delete`, string values
and missing-key defaults; `bench/trie/run.sh` runs the control and CoW builds
and asserts both the checksum and the RSS drop.

The first O6 cut also used the shallow-read path for scalar `[int:int]` reads
inside an `inout` helper, which marked the map shared and made the next write
copy all 200k entries. `bench/lru` grew from ~20 MB to 121 GB and OOM-killed the
host. Scalar/string values have no nested mutable part to protect, so the share
path is now gated on a share helper. LRU is back to **20.4 MB / 139 ms**;
`tests/cow_scalar_share.sh` pins the emitted C and `bench/lru/run.sh` fails at
256 MB instead of allowing the blow-up.

**O7: loop-grown array and map-literal presizing.** A fresh empty dynamic array
immediately followed by a counted loop that pushes into it is presized to the
loop's proven upper bound, removing the geometric growth copies
(`TYCHOC_NO_PRESIZE=1` is the control). The reference compiler now also presizes
map literals to their pair count, matching `tychoc1`
(`TYCHOC_NO_MAP_PRESIZE=1`). On `bench/prongB/arr_pipeline.ty`: `tychoc1`
**6,544 KB / 30 ms → 4,280 KB / 24 ms**, `./tychoc` **6,496 KB / 26 ms →
4,300 KB / 20 ms**. On `bench/opt/smallmap/main.ty`, `./tychoc` **10 ms → 6 ms**.

Bounds-check elision and SROA were measured, not assumed: the existing
monotone-index elision already covers the fixture's loops, fixed arrays and
structs are already inline, and a small dynamic array in loop scratch measured
within noise of the inline form. No second map representation was added. The
shapes are pinned by `tests/presize_loop.ty`.

## Verification

`make ci N=0` — CI GREEN (3107s, serialized; includes the live server gate).
`make test` — 1161 passed, 0 failed. `make corelib` — 48 ok.
`make parse-check` — all green, 1521 files. `make bench` — all benchmarks within
bounds under both `./tychoc` and `./tychoc1`. The fuzz lanes were run separately
at 60 seeds each: differential, reject and leak all green. `make asan-self`
(inside `make ci`) compiled 1169 files with ASan+UBSan clean. The full sweep was
not recorded by `make status` only because the README changed during the run;
the code itself was unchanged.

0.8.8 is pre-1.0 and there are **no stability guarantees**: anything here may
change before 1.0. See [CHANGELOG.md](CHANGELOG.md) for the accumulating record.
