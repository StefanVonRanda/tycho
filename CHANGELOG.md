# Changelog

All notable changes are recorded here against the version that shipped them.
The version constant lives in `src/tychoc.c` (`TYCHO_VERSION`, printed by
`tychoc --version`); bump both together. Per-release publishing notes stay in
`RELEASE_NOTES.md`; this file is the accumulating record.

Names used below: `surface.lock` is the frozen record of keywords, builtins and
corelib signatures (checked by `make surface-check`); `tychoc` is the reference
compiler, `tychoc1` the self-hosted compiler. A *lane* is one step of the local
gate (`make ci`); `FRICTION n` is entry n in the defect log
(`docs/internals/FRICTION.md`); a *probe* is a small program written against the
docs alone to test them. See [STATUS](STATUS.md) for the full terms box.

## [0.8.8] — 2026-10-21

### Breaking

- **`strings.parse_int` is now the strict parser.** It returns
  `Result(int, IntErr)` and refuses empty input, trailing garbage and
  out-of-range values. The fail-open prefix scanner was renamed
  `strings.parse_int_prefix`; `parse_int_checked` is gone. Every call site,
  test, example, tool and doc was updated, and the dangerous name is no longer
  the obvious one.
- **`eprintln(s)` is now a builtin.** It writes `s` and a trailing newline to
  stderr; `eprint(s + "\n")` still works. The builtin has the same one-string
  signature as `eprint`.
- **`path.real` is now implemented through `real_checked`.** The old
  fail-closed spelling and all its callers keep their behavior; a missing,
  denied or over-long path is available as a named `PathErr` instead of a bare
  `""`.

### Core library

- `core:io` gained `FileKind`, `Stat`, `stat`, `lstat` and `same_file`
  (device + inode identity; handle identity on Windows), plus one-stat
  `lstat`-based walking.
- `core:path` gained `real_checked` (`NotFound` / `Denied` / `TooLong` /
  `BadPath` / `Failed`) and `quote` (POSIX shell-safe single quoting for text
  reports).
- `surface.lock` is additive: 116 keywords, 42 builtins, 435 corelib functions.

### Documentation

- The parameter copy/borrow boundary is now explicit in
  `reference/functions.md`, `reference/basics.md` and `spec/11-functions.md`,
  including the full-copy cost of `[N]T`, `vector[N]T` and inline-array
  structs.
- `inout` forwarding (`bump(&n)` from an `inout` parameter) is documented with
  a compiling example.
- `tycho-dupe` (`tools/tycho-dupe`) is the duplicate-finder probe, with
  `make dupe-check` and `FRICTION-OUTSIDE.md`.

### FFI

- **Plain scalar-field structs can be `inout` out-parameters.** A non-packed,
  non-aligned struct whose fields are numeric scalars now crosses as a normal
  `S_Name *`; the C function fills it in place and Tycho reads it back by value.
  Fixed-width fields (`i32`, `u8`, `f32`) match C widths. Strings, arrays, maps,
  handles, packed/align structs and non-POD composites remain rejected; use
  `--shim` for those. `tests/ffi` covers the round trip and the rejection of a
  string-field and a packed struct.
- **POD structs by value/return, and `[POD]` parameters.** The same
  non-packed scalar-field struct may cross by value and as an extern return; a
  dynamic array of one crosses as `(const S_Name *, len)` and is borrowed for the
  call. This covers SDL_Rect/SDL_FRect/Vector2-style APIs without a shim for the
  values themselves. `tests/ffi` checks an added struct, a returned struct and a
  `[struct]` sum; non-POD and packed structs stay rejected. `examples/gfx` now
  passes `SDL_FRect` and `SDL_Rect` by value and a `[FVec]` (`SDL_FPoint`
  layout) array to `SDL_RenderLines`, so the windowed Snake exercises the real
  SDL struct layouts on the GPU path. C unions stay non-crossable by design:
  `examples/gfx` decodes `SDL_Event` into a tagged POD `Event` in the shim.
- **The gfx example now has stream audio.** Tycho generates signed-16-bit PCM
  into `bytes` and queues it through the SDL audio stream; no callback crosses
  the boundary. The lane uses `SDL_AUDIODRIVER=dummy` for a silent, deterministic
  test, while a normal run opens the default playback device.
- **The gfx example now has a texture cache and asset manifest.**
  `assets/manifest.json` declares the textures; Tycho parses it with
  `core:json`, decodes each PNG with `core:image`, uploads SDL textures, and
  fills a `[string: int]` ID cache. One scoped `TexSet` handle owns the whole
  set; `close(texset)` destroys every texture together. This avoids putting
  individual typed handles into collections, which is still unsupported.
- **The gfx example now draws its own text.** `assets/font.png` is a 16×6 glyph
  atlas decoded by `core:image` and uploaded as an SDL texture; each glyph is
  drawn with its own source/destination `FRect`. `SCORE n` and `GAME OVER` need
  no SDL_ttf or external font runtime.
- **The gfx layer is now `core:gfx`.** `corelib/gfx` owns the `Window`, `Audio`
  and `TexSet` handles, events, framebuffer/text helpers, manifest loading and
  snapshots; its `gfx_shim.c` is auto-linked. `examples/gfx/pong` is the second
  caller, using the same package alongside Snake. `present` is now parameterless;
  overlays use the separate `draw_lines` call, so a demo-specific clip/point
  overlay is no longer part of the present API. `default_controls`/`action`
  centralize key-to-action mapping, and `text_width`/`draw_text_centered` keep
  text layout in the package instead of in each game. `Clock`/`clock_start`/
  `clock_advance`/`clock_tick` add fixed-step simulation with a catch-up cap;
  both games use it instead of frame counts. Value `Sprite`/`Anim` types plus
  `draw_sprite`/`anim_frame`/`draw_anim` add sprite-sheet animation; Snake uses
  a four-frame apple sheet and Pong a four-frame ball sheet. Value `Mixer` plus
  `mixer`/`add`/`finish`/`play_mix` mix overlapping PCM in Tycho; both games use
  a two-note score jingle, still with no audio callback. `repeat`/`play_loop`
  queue a finite number of PCM repetitions for music; the caller re-queues when
  it wants more. Snake is now a complete small game loop: title menu, three
  speeds, pause, restart, persistent high score, and resize letterboxing through
  the value `View` transform; `package.sh` builds a runnable directory.

### Fixed

- **`core:image` no longer retains a handle header per decode.** The previous
  guard kept a 40-byte DEAD-sentinel header for every decoded image so a second
  free could be named. The shim now uses a monotonically numbered live-set
  handle, frees the header with the pixels, and still rejects a stale handle
  before reading freed memory. Snake and Pong runs under Valgrind and
  LeakSanitizer now have no definite or indirect leaks.
- **O6 no longer marks scalar maps shared on a read.** The first cut emitted
  `get_share` for `[int:int]` reads inside an `inout` helper. The read set the
  map's shared bit, so the next write copied the whole map; `bench/lru` grew
  from ~20 MB to 121 GB and OOM-killed the host. A scalar/string value has no
  nested mutable part to protect, so the shallow-read path is now gated on the
  value having a share helper. LRU is back to **20.4 MB / 139 ms** (control
  20.9 MB), and the persistent trie still measures **12.4 MB / 4 ms** against
  **1.63 GB / 468 ms** for its copying control. `tests/cow_scalar_share.sh`
  asserts neither compiler emits `get_share` for the scalar-map shape;
  `bench/lru/run.sh` now fails above 256 MB instead of letting the host OOM.

### Compiler optimizations

- **O1 shipped: immortal allocation + literal lifetime elision.** `TYCHO_LIT`
  stores a negative length header; `tycho_str_len` masks it, `tycho_str_copy`
  returns an immortal literal unchanged, and `tycho_str_recycle` skips it.
  Every copy/push/assign path inherits the elision without a compiler branch.
  On 6,000,000 literal pushes into a `[string]` field: **207,460 KB / 61 ms →
  114,056 KB / 28 ms** (median of five), byte-identical output. The result is
  in `bench/opt/RESULTS.md`.
- **Escape analysis now sees a zero-cost newtype wrap.** `return Env(m)` (and
  the same shape through any single-argument return value) marks `m` as
  escaping in both compilers. This fixes a latent use-after-free when a local
  map/array was mutated and returned through a newtype; O1 exposed it as a
  wrong count in `tests/newtype_agg`.
- **O2: read-only pass-by-pointer in the reference compiler.** It passes
  non-heap fixed arrays of four or more elements as `const T*` on direct calls,
  instead of a full C value copy. Named-function values, lambdas and spawns
  bridge the ABI; FFI, `inout` and `sink` are unchanged. On 5,000,000 calls
  through `fn first(xs: [256]int)`: **140 ms → 66 ms** (median of ten,
  `-O2 -fno-inline`), same RSS. `TYCHOC_NO_PTR_PARAMS` is the control.
  The packaged `tychoc1` already lowers `[N]T` to a 24-byte descriptor and
  measures **10 ms** on the same fixture, so O2 is a bootstrap/reference
  parity change, not a change to the shipped binary's performance.
- **O3: last-use moves into `push`.** A loop-local declared at the current loop
  depth is a fresh buffer each iteration, so its single read can hand the value
  over. When that read is `push(dst, x)`, the declaration is promoted into the
  destination array's arena and the push stores it without a deep copy;
  `TYCHOC_NO_MOVE_PUSH=1` restores the old copy path. The same relaxation
  applies to heap rebinds and direct `sink` calls. A local declared outside the
  loop, or a push into a caller-owned `sink` array, still copies. On
  `bench/opt/movebuild` (500,000 four-string loop locals): **91 ms → 79 ms**
  in `tychoc1` and **96 ms → 87 ms** in `./tychoc`, same RSS.
  `tests/move_loop_local.ty` covers both the adopt and the cross-arena copy.
- **O4: borrowed in-scope slices.** A string slice used as a call argument is a
  16-byte `TychoStrView` (tagged pointer) in the call arena instead of a copy
  of the field. `println`, `len`, user calls and `substr` accept views;
  binding, return, `push`, map keys and FFI materialize through
  `tycho_str_copy`/`tycho_str_cstr`. `TYCHOC_NO_SLICE_VIEW=1` restores the
  copy path. On 100 slices of a 12.8 MB string: **33 ms / 42.9 MB → 7 ms /
  30.6 MB** in `tychoc1`, and **32 ms / 43.0 MB → 6 ms / 30.7 MB** in
  `./tychoc`. `tests/pkg/slice_view` covers both paths.
- **O5: destination passing for `t = f(t, ...)`.** When `t` is read once in
  the RHS and immediately rebound, both compilers generate a variant of `f`
  whose matching parameter is OWNED. The variant may move a field or match
  payload of the old value into the result instead of deep-copying it, and a
  recursive call that consumes the changed child selects the same variant.
  Moves are legal only inside a `return`, and the result is built in the
  assignment target's arena, so the moved buffer and the result share a
  lifetime. `TYCHOC_NO_DEST_PASS=1` is the control; a call whose argument is
  still live, or whose result goes anywhere else, keeps the original function
  and copies. On `bench/opt/treeupdate` (12,000 inserts): `tychoc1`
  **2,252,644 KB / 500 ms → 8,908 KB / 3 ms**, `./tychoc` **2,252,484 KB /
  493 ms → 9,112 KB / 3 ms**. On `bench/opt/editorupdate` (10,000 edits over a
  16,384-leaf rope): `tychoc1` **8,966,776 KB / 1,800 ms → 7,584 KB / 3 ms**,
  `./tychoc` **8,966,596 KB / 1,783 ms → 7,748 KB / 4 ms**. Byte-identical
  output; `tests/dest_pass.ty` covers the unique, shared and snapshot cases.
- **O6: conservative shared-bit CoW / path copying.** A same-arena `b := a` of
  a composite map, or of a struct whose mutable fields are maps, now shares the
  map and sets a conservative shared bit; the first write through either copy
  duplicates the map's arrays and marks nested maps shared. The bit is the sign
  of the map's `ecap`, so no descriptor grew, and a stale bit can only
  over-copy. Arrays, scalar maps and non-shareable aggregates keep the old
  deep-copy path; this is an implementation optimization, not a value-model
  change. `TYCHOC_NO_COW=1` is the control. On `bench/trie/path/main.ty`
  (2,000-word persistent trie, output `5954 1981`): `tychoc1`
  **1,673,340 KB / 473 ms → 12,640 KB / 4 ms**, `./tychoc` **1,673,124 KB /
  482 ms → 12,448 KB / 5 ms**. The destructive `bench/trie` row is unchanged
  (58.6 MB vs 58.9 MB before O6); `tests/cow_maps.ty` pins snapshots, nested
  maps, `delete`, string values and missing-key defaults.
- **O7: loop-grown array and map-literal presizing.** A fresh empty dynamic
  array immediately followed by a counted loop that pushes into it is presized
  to the loop's proven upper bound, removing the geometric growth copies;
  `TYCHOC_NO_PRESIZE=1` is the control. The reference compiler now also presizes
  map literals to their pair count (`TYCHOC_NO_MAP_PRESIZE=1`), matching
  `tychoc1`. On `bench/prongB/arr_pipeline.ty`: `tychoc1` **6,544 KB / 30 ms →
  4,280 KB / 24 ms**, `./tychoc` **6,496 KB / 26 ms → 4,300 KB / 20 ms**. On the
  six-entry map literal in `bench/opt/smallmap/main.ty`, `./tychoc` **10 ms →
  6 ms**. Bounds elision and SROA were measured: the existing elision already
  covers the fixture's loops, fixed arrays/structs are already inline, and a
  small dynamic array in loop scratch measured within noise of the inline form,
  so no second representation was added. `tests/presize_loop.ty` pins both
  shapes.
- `ROADMAP.md` now carries O1–O7 for widening the domain without adding
  references: O1 above, read-only pointer parameters, last-use moves, borrowed
  string/bytes slices, destination passing, conservative CoW, and
  allocation/bounds work. All seven are in 0.8.8.

## [0.8.7] — 2026-10-18

### Breaking

Every item here makes a program that 0.8.6 accepted stop compiling. Each one
names its rule in the diagnostic. `surface.lock` moved, by addition only: seven
corelib functions joined it (see *Core library*), no frozen signature changed
and the keyword list is unchanged at 116.

- **An out-of-range literal is refused wherever it adapts** (`e7c485d2`,
  `5fdcb3b9`, audit D06). `x + 300` at a `u8`, a `300` passed to a variadic or
  UFCS parameter, a lambda body, a `m.get` default, a subscript assignment, a
  value `match` arm, a `send`, or the broadcast scalar of a sized-int array
  wrapped silently in the self-hosted compiler and printed the wrapped value.
  Every site now says `300 does not fit u8 (its range is 0..255)`, the
  reference compiler's wording; an `f32` array or vector element says
  `f32 literal out of range`.
- **An empty literal `[]` is refused for a `[N]T` or `vector[N]T`**
  (`e7c485d2`, D05): `a fixed-size array of length 2 needs 2 elements, got 0`.
  The reference compiler emitted an uninitialised C aggregate; the self-hosted
  one built a length-0 "fixed" array.
- **A `[N]T` whose `N` is an `int` const keeps its length in the self-hosted
  compiler** (`e7c485d2`, D07). A wrong-arity literal, a `push` onto it
  (`cannot push to a [3]int -- its length is fixed; use a bounded[3]int or a
  [int]`), a zero or negative `N` (`a fixed-size array length must be
  positive`) and a mismatched fixed-array argument all compiled and ran with the
  wrong `len`.
- **An f-string hole is one expression** (`e7c485d2`, D24). `f"{price:.2f}"`,
  `f"{n, 99}"` and `f"{n )))}"` parsed the first expression, discarded the
  rest and printed the bare value; they are refused. A space after `{`, which
  the spec itself writes, is accepted.
- **The identifier prefix `Ztycho_` is reserved** (`e7c485d2`, D26):
  `identifier prefix 'Ztycho_' is reserved for compiler temporaries`. Compiler
  temporaries used to share the user namespace, so a local named `_fc0`
  (reference) or `_mx36_0_0` (self-hosted) was silently replaced by the
  compiler's own and the program printed a wrong answer.
- **A handle `free:` destructor that returns an aggregate is refused**
  (`e7c485d2`, D52). `extern fn close_file(h: File) -> [int]` passed the
  `free:` check and failed later in `cc` with `too few arguments`.
- **The compiler CLIs refuse an empty `-o ""` and a trailing bare `-L` or `-I`**
  (`e7c485d2`, `026d6c17`, D33, D34): `tychoc: -o needs a non-empty output
  name`. The self-hosted compiler built a default-named binary from the first
  and swallowed `-lm` on the second.

### Language

- **Negative literals adapt like positive ones in the self-hosted compiler**
  (`5fdcb3b9`). `x: f32 = -0.5`, `x: i8 = -5`, `[-1, 2.0]` at a `[2]float`,
  `g: vector[4]f32 = [-0.5, 0.25, 1.0, 7.0]` and `x * -0.5` at an `f32` were
  refused by `tychoc1` (`element 1 of a vector[4]f32 array is the wrong type`)
  and accepted by `tychoc`; eleven such programs now compile under both.
- **`to_u8`..`to_i64` of a float are total in the self-hosted compiler**
  (`e7c485d2`, D22, D27): NaN, infinity and out-of-range input give the
  spec's wrapped result instead of aborting, and `to_u64(1e19)` fits.
- **A `u64` shift count of 2^63 or more is zero** (`e7c485d2`, D37), as the
  spec says, not `tycho: negative shift count`.
- **A generic enum's variant can be constructed through a package qualifier**
  in the self-hosted compiler (`e7c485d2`, D83): `box.Has(41)` and
  `box.Nothing$(int)` died with an unlocated internal message.

### Compiler

- **The 2026-09-25 defect hunt is closed: 90 findings, all fixed**
  (`e7c485d2`, `026d6c17`; the ledger is
  [`docs/internals/audit-2026-09-25.md`](docs/internals/audit-2026-09-25.md),
  FRICTION 133–200). Eleven finder agents on one surface each, every finding
  handed to a verifier told to refute it. Beyond the items above, the compiler
  half:
  - **Division by zero traps cleanly in the self-hosted compiler** (D04, D08):
    `/` and `%` on every sized integer and on element-wise array arithmetic
    were bare C operators, so a zero divisor was SIGFPE (a literal `0`,
    SIGILL) instead of `tycho: division by zero`, and `i64` MIN / -1 trapped.
  - **An `if`/`elif` chain is emitted flat** (D03, D14, D23): the self-hosted
    compiler needed 5.3 GB for 800 arms and was OOM-killed at 1250; the
    reference compiler nested one C block per arm, segfaulted near 7000 and
    clang refused 255+. A flat `+` chain and a many-hole f-string likewise
    emit at constant bracket depth (D17); a 256-term sum built under clang
    now.
  - **Compile time on nested calls is no longer exponential** (D13, D15,
    D19, D31, D35, FRICTION 198, 200): nested generic method chains and
    constructors, `program_in`'s per-field copy of the struct text (900
    structs × 20 fields needed 4 GB), one long f-string, lambdas in a function
    with many locals, and the reference front end's quadratic procedure
    lookup.
  - **A constant array literal is a static table plus `memcpy`** (D18) in
    both emitters, not one store per element inside a statement expression —
    a 15,625-element table took gcc 21 s.
  - **Enum variants reach C under prefixed names** (D28), so a variant named
    `EOF`, `NULL`, `default` or `while` builds, and `enum Shape {Circle(..)}`
    beside `enum Shape_Circle` no longer collides on a typedef.
  - **A `packed` struct field passed as `inout` goes through an aligned
    local** (D21) in both emitters; the emitted C dereferenced it at natural
    alignment.
  - **Composite map keys with a float leaf hash `+0.0` and `-0.0` alike**
    (D29); a map held two `==`-equal keys. The self-hosted compiler refuses a
    sized-int or `f32` leaf as the reference compiler does, instead of calling
    a hash function it never defined (D30).
  - **The self-hosted compiler lowers `pow(x, y)` and `for x in s` over an
    `soa`** (D25, D40); it type-checked both and died in its emitter.
  - **`-g` escapes quotes, backslashes and control bytes in `#line` paths**
    (D12), in both compilers.
  - **The reference compiler**: a `StructDef*` held across a nested generic
    instantiation that reallocated the table was a heap use-after-free on a
    195-byte valid program (D02); `read_file` overflowed on a pipe, FIFO or
    `/dev/stdin` (D11); the resolver's depth counter was never reset after a
    recovered error, so enough ordinary errors turned every later line into
    `expression too deeply nested` (D35); a type name containing `__` was
    mistaken for mangling (D36); `argument 2 of math__clamp` names `clamp`
    (D38); `--symbols` exits non-zero on an invalid program (D32); a
    cross-package "did you mean" no longer suggests a `_`-private symbol the
    next compile refuses (D60).
- **The builtin-collision warning described the wrong dispatch**
  (`026d6c17`, D10). Inside `package lib`, an unqualified `pow(1.0, 2.0)` has
  always called `lib`'s own `pow` under both compilers; the reference
  compiler's warning said `every unqualified pow(...) calls the BUILTIN, so
  this procedure is unreachable`. It now says what happens: `` `pow` collides
  with the builtin of the same name -- unqualified calls in this package
  select this procedure. Rename it if this package needs the BUILTIN; external
  callers can use `pkg.pow(...)` ``. In `package main` the builtin wins
  (`tests/shadow_builtin_selection.ty`); `tests/package_builtin_shadow.sh`
  pins both. `total := total(xs)`, a local named after the function that
  initialises it, compiles instead of emitting C that did not (D09).
- **Diagnostics that did not state their rule now do** (`b9df4aa1`), in both
  compilers: `x := if c: "a" else: "b"` said `expected newline` and now says
  `` a value `if` puts each branch on its own line -- `x := if c:`, then the
  value indented on the next line, then `else:` and its value indented below
  it; a one-line `if c: a else: b` is not a form ``. `f"{g(\"x\")}"` said
  `unterminated string in an interpolated hole` and now says `` a `\"` inside
  an f-string hole: the hole is Tycho source, not string text, so a string in
  it takes plain quotes -- f"{g("x")}", not f"{g(\"x\")}" ``. Both came from
  an outside probe that learned the rules by trial.
- **The shipped `tychoc` (which is `tychoc1`) called itself `tychoc1`**
  (`212d8a5b`). A missing source file said `nope.ty: error: no 'main'
  procedure` because the builtin `read_file` returns `""` for a file it cannot
  open; it says `tychoc: cannot open <path>` and exits 1. Twenty-five driver
  messages and the usage line carried the internal name and listed the
  internal modes; `--help` is now the reference compiler's text byte for
  byte, and `--emit-c` with no `-o` writes the C to stdout as `./tychoc` does
  (`b9df4aa1`) instead of printing usage.
- **The self-hosted compiler's int- and enum-keyed map probe emitted
  `((k == q))`** (`b7ce7614`), which clang reads as a mistyped assignment:
  `equality comparison with extraneous parentheses` in every build of a
  program with such a map — 76 warning lines across the fixture corpus, none
  from the reference compiler.
- **A long simple-int sum is emitted flat by the reference compiler**
  (unreleased). `x+x+...+x` over 128 or more int literals or locals parses
  left-nested and `tychoc` wrapped one parenthesis pair per `+`: clang
  refuses 256 levels (`-fbracket-depth`) and Apple clang's frontend crashed
  on 1400. FRICTION 146 recorded this fixed, but only the self-hosted
  compiler had the flat path; `tests/expression_scale.sh` passed under
  `make` on macOS only because make raises the stack limit. The reference
  now has the same flat emission; both compilers emit the 1400-term sum at
  depth 4.
- **A `packed` field passed `inout` to a void call no longer warns in the
  user's build** (unreleased). The reference compiler closed the
  statement expression with `memcpy(...);; }`, and clang's `-Wunused-value`
  fires on a statement expression that ends in a null statement; `make
  emit-warn` caught it on `tests/packed_inout.ty`, the one fixture that
  exercises the path. The stray semicolon is gone; the self-hosted compiler
  never emitted it.
- **Value storage: append state belongs to the binding, not the string**
  (`6c76904d`). The in-place `acc = acc + e` optimisation tracked length and
  capacity in sidecar C locals declared with the variable, so an append
  through an `inout string` in a separately called helper, or in a `bytes` or
  match-arm accumulator in the self-hosted compiler (D72, D80), fell back to
  quadratic concatenation — `tycho-db`'s torn-log recovery needed 7.5 GB for a
  789 KB log. Both emitters now pass a per-binding append state, forwarded
  through `inout` calls, that grows in the owner's arena and invalidates its
  capacity when the binding is reassigned (`tests/inout_append.sh` bounds the
  growth and requires a pure-concat control to fail). The self-hosted
  compiler's redundant AST staging is gone and its statement builder is
  shared; `tests/compiler_storage.sh` checks the driver and emitter API on a
  deep snapshot and `bench/allocator/compiler_peak.py` records the before and
  after. The plan and every measurement are in
  [`docs/internals/plan-value-storage-places.md`](docs/internals/plan-value-storage-places.md).
- **A `recv(ch)` used as an expression returned a payload built in the
  callee's freed scope** in the self-hosted compiler (`e7c485d2`, D81) —
  SIGSEGV or a corrupted `Option` once it escaped the function. Overwriting a
  `[string]` element and a variadic pack in a loop grew without limit (D84).

### Runtime

- **Overwriting a heap value in a long-lived map reclaims the old value**
  (`e7c485d2`, D82), in both compilers. `tycho-kvsrv` grew about 1 KB of RSS
  per `PUT` to the same key without limit.
- **Size arithmetic on untrusted counts is checked before it allocates**
  (`e7c485d2`, `026d6c17`, D39, D41, FRICTION 178): `reserve(xs, n)` on a
  composite-element array and `channel(T, cap)` with a huge capacity wrapped
  `n * sizeof(elem)` to a tiny allocation and then wrote past it. Both abort
  by name now (`tests/abort/reserve_composite_overflow.ty`,
  `channel_capacity_overflow.ty`); map `reserve` bounds its capacity before
  doubling.
- **Directory listing builds its names in one arena pass** (`6c76904d`):
  names grow upward and the index downward inside one block, instead of one
  temporary string per entry copied by `push`. `tests/dir_packing.c` compares
  it with the original against the same tree; `make dir-packing` reports the
  numbers.
- **The arena block header is 8-byte aligned on ILP32** (`6c76904d`), and a
  block capacity that would overflow `SIZE_MAX` is an out-of-memory abort, not
  a wrap. `tests/allocator_contract.sh` exercises ownership, statistics modes
  and the 32-bit build separately from generated code.

### Core library

- **Seven functions joined `surface.lock`** (`e7c485d2`):
  `io.rename(src, dst) -> Result(void, IoErr)`;
  `io.lock_exclusive(p) -> Result(bool, IoErr)`, a process-lifetime exclusive
  lock; `httpd.has_header(r, name) -> bool`;
  `tls.connect_timeout(host, port, ms)` and `tls.set_timeout_ms(conn, ms)`,
  which were documented but package-private, so no program could call them
  (D56); `zip.create_checked(entries) -> Result(bytes, string)`, which refuses
  a name of 64 KiB or more and more than 65535 entries instead of wrapping
  them through `to_u16`/`to_u32` into an archive whose own reader listed
  forged entries (D64); and `zip.extract_entry(b, e: Meta) -> bytes`, so
  reading every entry is one directory pass rather than a re-read of the
  central directory per entry (D69).
- **Interior NULs fail closed at every remaining C-string boundary**
  (`e7c485d2`, `026d6c17`, FRICTION 164): `core:crypto`'s whole hex surface —
  `sha256`, `sha512`, `hmac`, the PBKDF2 salt, AEAD, Ed25519 sign and
  verify, `x25519`, `key_from_hex` — truncated at the first NUL, so verify,
  HMAC and AEAD-decrypt accepted input that was never signed (D43, D44);
  `sqlite.open` opened a different file and `exec` ran a shorter statement
  (D46, D85); stream lines and child-process output cross the FFI as
  length-carrying `bytes` (D49). `SECURITY.md` no longer calls NUL a
  Tycho-string limitation; `base64`, `hex` and `url` decode preserve `0x00`.
- **`core:httpd` refuses framing it cannot decode** (`e7c485d2`, D54, D57,
  D88): any `Transfer-Encoding` and an empty `Content-Length` were framed as
  bodyless and the body was read as the next request. `parse_request` now
  rejects whitespace before a header colon, obs-fold continuations and extra
  request-line tokens, as RFC 9112 requires. `BodyTooLarge` separates a
  declared body over budget (413) from an oversized header (431); `tycho-httpd`
  answered 431 to both.
- **`core:http` refuses a CR or LF in `content_type`** (`e7c485d2`, D47,
  D55); it was written straight into the request headers.
- **`core:toml`** (`e7c485d2`, D61, D63, D66): a parent table declared after
  its subtable and the subtables of an array-of-tables were silently dropped
  from valid documents; quoted keys and headers were split on the dot inside
  the quotes, and a malformed literal string or a lone surrogate escape became
  a value. Parsing is now bounded — 64 KiB of input, 512 entries or headers,
  128 levels — and says which bound (`TOML document exceeds 65536 bytes`)
  instead of exhausting memory.
- **`core:regex` caps a subject at 4,096 bytes** (`e7c485d2`, D42):
  `tycho: core:regex: subject exceeds the 4096-byte match limit`. A pattern
  under the repetition-product ceiling still took 40+ seconds to match; the
  ceiling bounded compile memory, not match time. A group index is checked
  against the compiled capture count (D48).
- **`core:compress`** (`e7c485d2`, D65, D70): a length of 4 GiB or more was
  truncated to `uInt` and reported success on `len mod 2^32` bytes;
  `decompress` returned only the first gzip member and accepted trailing
  garbage as `Ok`. Both are refused.
- **`io.copy(src, dst)` on the same file** (`e7c485d2`, D45) truncated it to
  zero bytes and returned `Ok`; it is detected by device and inode, through a
  symlink too. `path.resolve_under` refuses a dangling symlink inside the root
  (D62) — a later write landed outside it. The `net` shim validates a
  descriptor before narrowing it to `int`, so `net.close_fd(fd + 2^32)` fails
  instead of closing an unrelated descriptor (D59). `core:datetime` bounds the
  numeric fields of a POSIX TZ string (D51).
- **`json.parse` and `markdown.inline` were quadratic** (`e7c485d2`, D67,
  D68): the duplicate-key scan over every prior member, and a `]` found and
  re-found for every unmatched `[`.
- **`core:tls` verifies the hostname through `X509_VERIFY_PARAM_set1_host`**
  (unreleased). `SSL_set1_host` is deprecated in OpenSSL 4.0, which Homebrew
  now installs as `openssl`, and `make shim-warn` went red on the warning;
  the replacement is the call it wrapped, present in OpenSSL 1.0.2 onward and
  LibreSSL. `make tls-verify` holds all four legs.
- **`datetime.offset_at` answers 0 for a TZ string libc accepts beyond
  24 hours** (unreleased). `"EST25"` is refused by the rule parser and fell
  through to libc, where glibc answers UTC and macOS libc answers -90000; the
  fallback is bounded to ±86400 on every platform, as the corelib golden
  already asserted.
- **`math.sign` compared `z == z`** to tell zero from NaN (`0162f82d`). At
  `T = int` that is `if (h_z == h_z)` and clang warned
  `self-comparison always evaluates to true` in the build of every program
  calling it on an `int`. It is `x == z` now, the same test for every input.
- **`zip.create` builds its archive as `bytes`** (`6c76904d`), not as an
  `[int]` of one machine word per byte with each entry's compressed payload
  retained a second time; `make allocator-peaks` records the peak before and
  after.

### Tools

- **`tools/tycho-db`** (`e7c485d2`, D71–D79, FRICTION 168–175): a checkpoint
  rewrote the store in place, so a crash or full disk during it destroyed
  every committed row — it writes and syncs a sibling file, renames it
  atomically, syncs the directory and then rewinds the WAL. A mutation whose
  WAL append failed was still applied in memory and served; it is validated,
  appended, then applied. Nothing locked the store, and two processes on one
  database lost 300 of 300 acknowledged commits; `open_db` takes an exclusive
  `.lock` for the life of the process. `store.decode` trusted row arity and
  types, so a one-byte corruption panicked mid-query; it validates shape,
  tags, encodings and trailing bytes. One idle connection pinned the serial
  server forever; a line has a one-second deadline and a session a 60-second
  cap. The sorted-array KEY index copied every shifted entry (689 KB of
  inserts grew the server to 1.6 GB); it is a type-tagged hash index. An
  indexed cross-type comparison returned no rows where the scan reported an
  error; both report it.
- **`tycho-db`'s line cap counts payload bytes** (`6c76904d`): `MAX_LINE`
  (8192) is checked on the first line before the buffer is split, excludes
  the LF and an optional CR, and a pipelined tail is not part of the line. The
  old newline-first branch let a late newline bypass the cap;
  `tools/tycho-db/run.sh` sends 8191, 8192, 8193 and 10000-byte statements
  under both line endings.
- **`tools/tycho-docs`** (`7364230c`, `0218f234`, `43c498e2`) renders `docs/`
  as a browsable site — fences, tables, quotes and nested inline spans — and
  `make docs-site` / `make docs-site-check` build and check it. It is the
  tree's docs site generator; see *Documentation*.
- **`tycho-httpd`'s `Range` digit limit counts significant digits**
  (`e7c485d2`, D58); a 16-character position with leading zeros was treated
  as 10^15 and answered 416 for a satisfiable range.
- **`make debug-check` accepts both gdb spellings** (`e7c485d2`, D90): gdb
  15 prints `No line 999 in file`, gdb 17 `No compiled code for line 999`; the
  gate required the second and `make ci` was red on Ubuntu 24.04.

### Documentation

- **Every user-facing page was rewritten for a reader, not a reviewer**
  (`4bc2f281`, then `4c733ed0`, `08f55a24`, `e92536a2`, `6959541a`,
  `901e9274`, `c4ca258e` and some thirty voice commits): the README puts the
  first success before the trade-offs, names the on-ramp for non-C readers and
  answers the package-manager question plainly; the index layers the reading
  path; "honest", "measured" and "experimental" give way to the receipt
  itself; benchmark tables are stamped as rounded snapshots (`cd19dd7f`);
  stale pass counts and code locators are dropped from `platforms.md`,
  `performance.md` and `architecture.md`.
- **"Thinking in Tycho", an idioms page, leads the docs** (`91d75dc4`,
  `4f18f963`).
- **The reference gained `or_else` and `cli.parse_checked`** (`778f6cb8`) —
  the 0.8.6 headline was only in spec §14.6.1, and an outside probe found it
  by grepping the spec. FRICTION 98 and 128 said OPEN after both were closed
  (`c005a03b`).
- **The defect hunt is recorded in full** (`396ecbde`, `dd4708f1`): 90
  findings, 255 verifier and note items, the refuted list, and the closure
  status per finding.
- **The memory model page states what arenas do and do not promise**
  (`6c76904d`): scope exit releases ownership, not necessarily resident
  memory; reuse is not an OS release or a latency guarantee. `STATUS.md`
  reads "evidence for the arena model" where it read "the thesis is proven".
- **The docs site is rebuilt** on the landing page's design tokens, serif
  headings, a 4px spacing scale and a shared masthead (`bd803d54`,
  `f477235e`, `03ee3f35`, `26539459`, `cb8acc3b`); one shell, navigation and
  full-text search across the whole site, with search in a global header modal
  (`41756958`, `337d8da4`, `3b0c7eec`); a plain-language introduction and a
  newcomer editorial flow on the homepage (`52b98452`, `25aa9f12`,
  `63fba1ed`); an author-to-reader voice with research references
  (`12e12311`); and an outlined footer whose glyph seams are masked
  (`280429ab`, `f3c6ac3a`). The contrast gate `[c5]` is retargeted at the
  rebuilt palette (`65efc4c4`).

### Verification

- The fixture corpus is 1222 `.ty` files under `tests/`, from 1156; 719 are
  rejection fixtures, from 686; `reject-expect` is 657, from 636.
- **`make emit-warn`, lane `[3d2b/13]`** (`94dd9756`): the warnings the
  *emitted* C raises in a user's build. Every suite compiles with `tychoc1`,
  rolls its own `cc` line and reads `cc`'s stderr only when the build fails,
  so the two `-Wunused-value` warnings `core:strings` put into every clang
  build (FRICTION 128) were seen by no lane for three days. It emits every
  corelib test and every positive fixture with both compilers — 682
  programs — and compiles each `-fsyntax-only` under named `-Werror=` flags
  probed against the host `cc` (`unused-value`, `tautological-compare`,
  `parentheses-equality`); a probe per flag must be refused or the lane
  "asserts nothing". Built against the parent it reported all ten programs.
  81 lanes, from 80.
- **`make test` builds both compilers and runs 27 new scripted checks**
  beside the fixtures, from one (`e7c485d2`, `026d6c17`, `6c76904d`): the
  audit's scale and guard tests (`elif_chain`, `nested_blocks_scale`,
  `expression_scale`, `struct_defs_scale`, `array_literal_scale`,
  `emitter_nested_calls`, `emitter_memory_scale`, `audit_runtime_guards`,
  `http_header_guard`, `compress_limits`, `cli_flags`,
  `package_builtin_shadow`, `package_diagnostic_name`, `packed_inout_ffi`,
  `debug_path`, `stream_source`, `depth_recovery`, `proc_signature_scale`,
  `name_registry_scale`, `place_store_order`, `recv_loop_scale`,
  `map_overwrite_scale`, `loop_temp_recycle`, `accumulator_scope`) and the
  storage work's (`inout_append`, `compiler_storage`, `allocator_contract`).
  Six new abort fixtures under `tests/abort/` pin the division and overflow
  traps. `TYCHO_TEST_JOBS` bounds the fixture runner's parallelism separately
  from the fixtures' own `TYCHO_THREADS`.
- **`parse-check` leg `[4e]` pins the driver's surface against `./tychoc`**
  (`212d8a5b`, `b9df4aa1`): the missing-source message and exit, the bare
  run's stream and exit, `--help` byte-identical, the `wrote` line's newline,
  `--emit-c` to stdout and no `tychoc1` on an unknown flag. Against the 0.8.6
  archive's binary it fails all five.
- **Nine scale tests run on macOS** (unreleased): the tests added with the
  defect hunt called `timeout` and `ulimit -v` directly, so on a Mac they
  exited 127 and `make friction-check-light` reported their FRICTION entries
  STALE. They source `scripts/shlib.sh` now and `ulimit -v` is a no-op where
  the shell has none, so their memory bound is enforced on Linux only.
  `scripts/entrypoints.sh` sorts its warning record under `LC_ALL=C`; the
  locale order put `../wal/wal.ty` on different lines on Linux and macOS.
  `tools/tycho-db/run.sh` reads a file's mode through `file_mode`, a new
  `scripts/shlib.sh` helper over GNU `stat -c` and BSD `stat -f`; the BSD
  one printed a usage message and the gate compared an empty string.
- **`make test-fast` runs on macOS** (unreleased): `tools/prunner` hardcoded
  `detect_leaks=1`, which Apple's ASan has no LeakSanitizer for, so every
  sanitizer binary exited 134 and all 362 fixtures failed. It takes the
  same per-OS gate `tests/run.sh` has.
- **`make ci-linux`** (unreleased) runs the whole gate on Linux from this
  machine: a fresh clone of HEAD in a throwaway `debian:trixie` podman
  container with the packages platforms.md lists and a comma-decimal locale.
  Nothing hosted runs this repository's gate; this is its Linux half.
- **`make prunner-check`, lane `[2b/13]`** (unreleased): `tools/prunner`
  runs the hello fixtures to all green. `--only SUBSTR` filters its job list.
- **`tests/expression_scale.sh` fixes its own stack limit** (unreleased), so a
  nesting-depth pin cannot pass by inheriting `make`'s raised RLIMIT_STACK,
  which is how FRICTION 146 read FIXED for three weeks on macOS while the
  reference compiler still nested; the entry carries the correction.
- **`CI_SERIAL=1`** runs the four lanes and the three fuzzers one after
  another for a shared machine (`6c76904d`); the default is unchanged.
- **The allocator measurement suite, `bench/allocator/`, is opt-in**
  (`6c76904d`): `make allocator-contract`, `allocator-baseline`,
  `allocator-packing`, `allocator-dual`, `dir-packing`, `allocator-directory`,
  `allocator-callers`, `allocator-peaks`, `allocator-compiler` and
  `compiler-storage`. Each compares a change with historical, presized and
  smaller-native controls plus ablations, separates profiles from plain
  timing and RSS, and never changes the production runtime; the results and
  the "no second integration" decision are in the storage plan.

## [0.8.6] — 2026-09-25

### Breaking

Every item here makes a program that 0.8.5 accepted stop compiling. Each one
names its rule in the diagnostic.

- **`or_else` is a keyword** (`529fe940`). An identifier spelled `or_else`
  must be renamed. `surface.lock` records it: 116 keywords, from 115.
- **136 corelib functions left the public surface** (`f6f1025a`, `37a2c54b`).
  They were internal helpers — `json.parse_value`, `bignum.mag_cmp`,
  `csv.quote_field` and the like — that were frozen as public API only because
  their names lacked a leading underscore. They are `_`-prefixed now, which is
  the language's package-private spelling, and a call from outside the package
  says so: `package 'bignum' has no symbol 'mag_cmp'; did you mean '_mag_cmp'?`.
  Every function a program outside its package was found calling stayed public
  and is now documented. `surface.lock` goes from 559 to 423 corelib functions.
- **`io.open_lines` returns `Result(LineReader, IoErr)`**, and `io.read_line` /
  `io.close_lines` take a `LineReader` (`44b02447`). See *Core library* below
  for the migration.
- **`reserve` and `pop` on a fixed-size `[N]T` or `vector[N]T` are refused**
  (`79eb8bbf`, `9b319cc4`): `reserve does not apply to a [4]int — its length is
  fixed`, the same wording `push` already used. Before this the reference
  compiler emitted C that did not build, and the self-hosted compiler accepted
  `pop` on a `[4]int` and silently shortened it.
- **Handles: four misuses are compile errors** (`ffi`, spec §25):
  - a use of `h` after a `close(h)` statement that closes it on every path —
    later uses passed null to C, and a second close was a silent no-op
    (`e98238be`);
  - `close(h)` on a handle *parameter* — the caller's scope exit then ran the
    destructor a second time (`20038444`);
  - an opener whose result is not bound directly to a variable (`use(open(p))`,
    or `open(p)` as a bare statement) — the resource had no owner and leaked for
    the life of the process (`4fa16643`);
  - a handle whose `free:` does not name an `extern fn` taking that handle
    type — a misspelling failed later inside `cc` with no Tycho line, and a
    destructor declared at the wrong type compiled silently (`17ddcbde`).
- **A dead import is reported per file** (`f53ff8f8`). An import one file of a
  package declares and only a sibling uses is now an unused import, and a type
  named through a package that only a sibling imports is refused whatever order
  the files are parsed in.

### Language

- **`or_else` — an early return that maps the error** (`529fe940`, `4cb884a9`,
  spec §14.6.1). `x := f() or_else e: h` unwraps `Ok`/`Some` like `or_return`;
  on `Err(e)` it binds the error to `e` and **returns** `h` from the enclosing
  function. `h` has the function's return type, which need not be a `Result`:
  `or_else _: false` in a `bool` function returns `false`. It replaces the
  four-line `match f(): Ok(v): x = v / Err(e): return Err(Wrap(e))`. Refused: a
  non-`Result`/`Option` operand, a named binding on an `Option` (it carries no
  error), a handler of the wrong type, a block after the `:`, and `or_else` in
  a function that returns nothing — use `or_return` or a `match` there. Both
  compilers implement it and agree. 79 call sites in the tree were migrated to
  it, beside 45 forwarding matches moved to `or_return` (`f6f1025a`,
  `fdda3c5a`).
- **`size_of$(T)` accepts an `align(N)` struct** (`09e85894`), so `align` can be
  observed from inside the language. The value is implementation-defined, with
  one guarantee: it is a multiple of `N`. A `packed` struct's size is exact, as
  before.
- **`math.min`, `math.max` and `math.clamp` work lane-wise on a `vector[N]T`**
  (`2b44b3e5`), one compare-and-blend per call. Every argument is the same vector
  type. No signature in `surface.lock` moved.
- **Floating point is no longer contracted into FMA** (`f10b8bec`). The emitted
  C is compiled with `-ffp-contract=off`, in both compilers. On aarch64 — where
  FMA always exists and `cc` fused `a*b + c` by default — results could differ
  from x86-64 in the last bit: `tycho-sheet`'s shortest-decimal render printed
  `0.30000000000000006` for `0.1+0.2` on aarch64 Linux. A program that relied on fused rounding on
  an ARM host now rounds twice, as its source says.

### Compiler

- **A generic instantiation error names the whole chain** (`c97bc5b8`), from the
  outermost call — the only line the reader is sure to own — inward, capped at
  eight frames. The reference compiler used to name only the innermost call.
- **Diagnostics that did not state their rule now do** (`571ba19d`, `91a2b294`):
  a value `if`/`match` used as an operand says it is allowed only as the whole
  right-hand side of `:=`, `=` or `return`; the ordering error lists the types
  that order (every sized integer, `f32`, `char`); `strings.len(...)` says `len`
  is a builtin.
- **The over-wide vector lint gave x86-64 advice on ARM** (`e99b8484`). On an
  aarch64 host it told the reader to pass `--target x86-64-v3`, which `cc` then
  refused. It now leads with the remedy that works everywhere — a narrower
  vector — and names the x86-64 flag as x86-64's.
- **Every build importing `core:strings` drew two `-Wunused-value` warnings**
  from the reference compiler (`4e1cfe23`), pointing at a generated file that is
  deleted after the build.
- **A lex error inside an f-string hole named line 1 of no file** (`5f70aa1f`);
  it names the owning file and line now. The self-hosted compiler's parser also
  reports more than one error per file in three more shapes, and every
  parse-only path names the file (`c54bf04d`).
- **The self-hosted compiler (the one that ships) had these defects fixed:**
  - `x[a:b]` on a named struct or enum crashed in emit with an internal message
    and no line; it is a checker error now (`00bed88e`).
  - An `or_return` / `or_else` payload returned straight to the caller was built
    in the function's own arena, which the return had just freed — the caller
    read garbage (`da3f0fa9`).
  - `to_bytes(local)` kept inside a returned value pointed into the freed scope
    (`5c6ad957`). It surfaced as `core:httpd` returning a corrupted request
    body.
  - A local `bounded[N]T` grew past its capacity instead of trapping on the
    `push` that overflowed it; a `parallel for` over a channel with more than one
    reduction is supported (`9a53576d`).
  - Indentation depth is capped at 256, as the reference compiler has always
    done — the verdict on a deeply nested file used to depend on the process's
    stack limit (`85427292`).
  - A pkg-config flag containing a space survives into the C build
    (`0ac392ea`).

### Core library

- **BREAKING: `io.open_lines` returns `Result(LineReader, IoErr)`** instead of a
  bare `ptr` that is null on failure, and `io.read_line` / `io.close_lines` take
  that `LineReader` rather than a `ptr`. Three frozen signatures changed, and
  `surface.lock` records them.

  The old shape answered instead of refusing: a null the caller had to remember
  to check (see [`docs/quiet-results.md`](docs/quiet-results.md) §6). Seventeen
  of the twenty-nine functions in `core:io` already returned `Result`; the
  streaming opener was the odd one out. Failure now says *which* failure:
  `BadPath` for an interior NUL (nothing was attempted, so retrying is wrong),
  otherwise `NotFound` / `IsDir` / `Failed`, classified the way `is_dir`
  classifies.

  **`LineReader` is a plain struct with a private C pointer, not a `handle`.**
  A `handle` is affine and only an `extern fn` may return one, so using one
  would move the opener into C and take the interior-NUL guard with it — and
  that guard has to stay on the Tycho side, because that is the only side that
  knows the string's length.

  **Two costs, unchanged from the `ptr` this replaces.** A `LineReader` is an
  ordinary value, so `b := r` gives two names for one C reader and closing both
  is a double free — close exactly one, exactly once. And cleanup is manual:
  Tycho has no `defer`, so a forgotten `close_lines` leaks the C-side reader
  until the process exits. An affine `handle` would make the first
  unrepresentable; the bare `ptr` did not, and this does not either.

  Migration is mechanical — the null check becomes the `Err` arm:

  ```text
  # before -- shown for migration; the `before` half no longer compiles
  r := io.open_lines(p)
  if is_null(r):
      return
  # ... io.read_line(r) ...
  io.close_lines(r)

  # after
  match io.open_lines(p):
      Err(e):
          return
      Ok(r):
          # ... io.read_line(r) ...
          io.close_lines(r)
  ```

- **`net.listen` used a backlog of 16** (`95b54149`). Linux drops a SYN when
  the accept queue is full and the client retries; macOS and the BSDs reset the
  connection. Under a burst on macOS every Tycho server refused connections. It
  is `SOMAXCONN` now.
- **A server worker could block in `accept()` forever and never see SIGTERM**
  (`c845dcdf`) — about one shutdown in eight on macOS. Several workers polling
  one listener raced on its shared non-blocking flag.
- **`TYCHO_BLOCK`'s guard was undefined behaviour on Windows**, where `long` is
  32 bits (`fc003e91`).
- **Subnormal float literals became `0.0` on aarch64 Windows** (`55d3abfd`).
  The reference compiler's `_GNU_SOURCE` switched mingw to its own `printf`,
  which formats a subnormal through the x87 80-bit layout.
- **`core:os` refuses to spawn a `.bat` or `.cmd` on Windows** (`b1b71c27`).
  `cmd.exe` re-parses the command line and defeats the quoting the spawn builds.
  A PATH-resolved name with no extension is not caught.

### Security

- **`core:tls` did not guard an interior NUL in `host`** (`8e1208f5`).
  `"good.example.com\0anything"` connected to, announced (SNI) and verified
  `good.example.com` while the caller's own checks saw the whole string. It
  fails closed now, like every other corelib package that hands a string to C.
- **The weblog example parsed an untrusted byte count without checking it**
  (`63ac1f42`) — the fourth parser of the fail-open sweep, which had fixed three.

### Fixed

- **The HTTP round-trip tests and example read one segment and called it the
  response** (`0d8a76df`, `95b54149`). Under load the body arrived separately and
  came back empty, or indexed an empty string and aborted. They read to EOF.
- **`tycho-sim` built a different world from the same seed on gcc and clang**
  (`86c362e9`). Three draws in one argument list are three unspecified side
  effects (spec §13.4); each is its own statement now.
- **`tycho-make` misreported an assignment whose value holds a `:`**
  (`b830bb47`), and refuses the makefile features it does not implement by name
  rather than misreading them (`5ace4f42`).
- **Building on Fedora with gcc 16 failed to link** `tychoc1` and the shipped
  tools: `-static-pie` needs `-fPIE` beside it (`3b8fc954`, `3310a50f`).
- `tycho-debug` treats `C:` and `c:` as the same drive; `tycho-httpd` names its
  15-digit `Content-Length` ceiling (`ece2797b`).

### Release and platforms

- **A `darwin-arm64` archive** (`a90eec42`). Both compilers build native Mach-O
  arm64 with no source change, and `make ci` runs green on macOS arm64 — first
  run 2026-09-19 (`95b54149`). `make release-content` checked nothing on a Mac
  until then: it exited on the first missing *Windows* tool before reaching the
  native archive. Each leg now states its own prerequisites.
- **`make platform-check`** executes the toolchain on every target
  `scripts/release_cross.sh` builds and records the verdicts, and the cross build
  writes `UNTESTED-PLATFORM.txt` from those verdicts instead of from a hardcoded
  list (`7eeb1763`, `506b5d79`). Measured once each: linux-x86_64, linux-arm64
  and macos-arm64 1065/1065, windows-x86_64 289/289, windows-arm64 288/289 and
  then 289/289 after `55d3abfd`.
- **`make preflight`** reports what the current machine can run — sanitizer
  runtimes, static libc, the 32-bit toolchain, optional tools — before `make ci`
  fails on one of them (`63b87529`).

### Tools

- `tools/tycho-du`, a disk-usage reporter over a typed `handle Dir`, written
  against the docs alone by an outside probe; the tree's worked example of
  `handle` with `--shim` (`5071b61f`).
- `tools/tycho-grade`, a gain/lift colour grader for TGA, BMP and QOI; the
  tree's first user of `vector[N]T`, swizzling and lane names (`2a878a94`).

### Documentation

- Tycho is described as a **data-oriented systems language**, not a
  general-purpose one (`d74577da`, `d624c6dc`).
- `STATUS.md` states where the project is, with every number reproducible by a
  command (`2e561727`). The README leads with the one idea, the costs and the
  limits (`f8a4c4c1`, `e8d7ce1b`).
- The reference pages carry nine rules the compiler enforces and did not
  document (`a3570400`); the handle documentation had three statements that were
  not true (`36fe264d`).
- The citation checker fails on a bare `:N` line reference, which no gate
  policed (`89d1ec3d`).

### Verification

- The fixture corpus is 1156 `.ty` files under `tests/`, from 1096; 686 are
  rejection fixtures, from 638.
- New lanes: the bootstrap fixpoint — one more generation of the self-hosted
  compiler emits byte-identical C (`3d5046b5`); the three parity fuzzers the spec
  cites (`38eacd25`); every public corelib function named in the catalogue
  (`1a1afa3a`, `37a2c54b`); the probe watchdog's own controls (`efb52c5e`).
- `make ci` runs under clang as well as gcc, on macOS as well as Linux, and
  honours a `TYCHOC` override that five gates ignored (`9458575f`, `480eaedd`,
  `ca974029`). Its steps are timed and its lanes repacked (`26040420`,
  `5b333977`).
- The uninitialised-arena sweep, the one class no sanitizer lane sees, covered
  corelib, the examples and 27 tools: 972 runs, 0 reads (`599ab843`).

## [0.8.5] — 2026-09-06

### Language

- **`vector[N]T` — a fixed array whose arithmetic is one machine instruction**
  rather than a loop the optimiser may or may not vectorise. Generic over the
  count, a power of two from 2 to 64, with `int`, `float` and `f32` elements
  (`0dbac5ca`). Indexing, `len`, `==`, `str` and the array literal are inherited
  from the fixed-array rules, so nothing about the surface is new except the
  type. `vector` stays an ordinary identifier everywhere else.
  - **Know your baseline before you reach for it.** Plain x86-64 is SSE2 and a
    register is 16 bytes, so a `vector[4]float` is 32 and every operation is
    split in half. Measured on one dot product: 15 instructions at the default
    `cc` line, 6 with `-mavx` (`599259ce`). At a width the target lacks, a
    vector is slower than the scalar code it replaced, and the compiler now says
    so at the declaration — one line naming the type's size, the target's
    register width and the `--target` level that fixes it (`948627db`,
    `004312e5`). Passing `--target` silences it.
- **`packed struct Name:` — a byte-exact C layout**, no padding between fields
  and none trailing (`5fff6d5c`). A field must be a fixed-width value type; the
  attribute is refused on anything that is not a struct.
- **A bridge between a packed struct and its bytes** (`51dcb45b`): `to_bytes(v)`
  gives exactly `size_of$(T)` bytes, `from_bytes$(T)(b)` reads them back, and
  `size_of$(T)` is the size so a caller can slice its own window. The wire order
  is **little-endian on every host**, not the host's own, because every binary
  record worth writing this way already specifies one. The length check is exact
  — a short buffer is refused by name, not padded.
- **`align(N)` as a declaration attribute** — the opposite direction to
  `packed`. `N` is a literal power of two from 1 to 8 (`ef566988`). **8 is the
  ceiling**: the arena guarantees 8, and raising it to 16
  cost 20–30% more arena bytes across the benchmarks for no measurable time. A
  request the allocator cannot honour is refused at compile time rather than
  silently rounded down.
- **Simultaneous assignment: `(x, y) = (y, x)`** (`04d7d9bc`). The targets are
  places, so a field, an array element or a map value may stand on the left, and
  the evaluation order is specified: every right-hand side, and every index
  appearing in a target, is evaluated before any target is written. So
  `(a, b) = (b, a + b)` is a Fibonacci step, not two statements in sequence, and
  `(i, xs[i]) = (2, 99)` stores at the old `i`. Arity and element types must
  match exactly, as in Go and Odin.
- **Field swizzling: `v.(x, y)`** (`d9d818f5`). Two to eight components of one
  value, and it *is* a tuple of them — so it reads, binds, destructures and
  stands on the left of an assignment, where it inherits the rule above:
  `v.(x, y, z) = v.(z, x, y)` rotates rather than smearing. A component is a
  struct field name, or an integer lane index for an array, a `bounded` or a
  `vector[N]T`, or one of the lane names below.
- **Lane names: `.x .y .z .w` and `.r .g .b .a`** on a value that stores its
  elements inline — `vector[N]T`, `[N]T` and `bounded[N]T` (`0c5ff854`). They
  are two spellings of the same four lanes, capped at four because four names
  cannot address a wider set, and `v.x` **is** `v[0]`: the resolver rewrites the
  node to an index, so reading, writing, swizzling and destructuring are
  inherited rather than restated, and `v.(x, y)` and `v.(0, 1)` are the same
  program. A struct wins the collision — a struct with a field named `x` never
  reaches the lane rule — and mixing the two spellings in one swizzle is
  allowed, which is where this diverges from Odin.

### Compiler

- **`--target <level>` raises the x86-64 ISA baseline, opt-in.** The levels are
  `x86-64-v2`, `x86-64-v3`, `x86-64-v4`, and `baseline` for the default.
  `baseline` and the absence of the flag add nothing to the `cc` line, so an
  unflagged build is byte-identical to one made before the flag existed. Any
  other level adds `-march=<level>`, and the binary must not be assumed to run
  on a machine below that level. An unknown level, and a level the C compiler
  rejects, are both hard errors. Both compilers accept it, both list it under
  `--help`, and `docs/spec/15-program.md` is normative (commit 2260bbfd).
- **`vector[N]T` now lowers to a machine vector in the compiler that ships.**
  The self-hosted compiler routed every vector to the arena-backed dynamic
  array, so it emitted no vector instructions at all and paid an allocation per
  operation — the feature was documented and effectively absent from every
  shipped binary (`07ee7be0`). A constant lane index also costs no bounds check
  now, in both compilers (`599259ce`).
- **The shipped compiler stopped accepting programs the language does not
  allow.** The C bootstrap enforces 495 user-facing rules; a survey found 262 of
  them reached by no test anywhere in the tree, and the self-hosted compiler was
  missing a share of those it had never been asked about. Roughly 270 new
  rejection fixtures were written and the gaps behind them closed — parse rules,
  `for`-statement rules, operand typing for `and`/`or`/`not`/`~`/`-`,
  parallel-`for` rules, f-string interpolation typing, malformed f-string
  bodies, array operations that used to be left to the C compiler to complain
  about, and a non-`$Name` type parameter. The list of divergences the tree
  tolerated by name is now **empty**; it held 25 entries at 0.8.0.
- **The map-key rule diverged from the spec in five directions** and is fixed
  (`9d66462a`). A written `vector[N]T`, `[N]int`, `bounded[N]int`, `[float]` or
  a tuple key was wrongly refused; a struct with a fieldless-enum field was
  wrongly accepted. Only an explicitly *annotated* composite key reached the
  rule, which is why nothing noticed — `m := [a: 5]` infers and always worked.
- **Float-to-integer conversions are total, as the spec says** (`15f0df78`).
  `to_u8`..`to_i64` emitted a bare C cast, undefined for a NaN, an infinity or
  an out-of-range value; they are checked at run time now (non-finite is 0,
  otherwise truncate toward zero and wrap). An out-of-range sized-integer
  literal is no longer truncated in silence.
- Diagnostics name the rule that was broken rather than the types involved, and
  name the file the reader named rather than guessing.

### Core library

- **A double free of a `core:crypto`, `core:tls`, `core:http` or `core:image`
  handle dies by name** instead of segfaulting (`fc8120c2`). These four hand out
  a bare `ptr`, which is not affine, so the compiler cannot see the second free:
  freeing a key twice was `SIGSEGV`, and using one after free printed a garbage
  number and exited 0. Each handle now carries a magic word and a dead sentinel,
  and every entry point that takes one exits 1 with
  `tycho: double free of <handle>` or `tycho: <handle> used after free`. A
  run-time diagnostic, not a compile error.
- `net.wait_readable(fds, ms)` — poll a **set** of descriptors and get back the
  readable subset, in the order given (`d48dc822`). Built on `poll(2)`, so a
  descriptor at or above `FD_SETSIZE` works. A closed peer counts as readable; an
  empty set, a negative descriptor or a negative timeout is an error rather than
  a silent timeout.
- `path.real`, `path.under`, `path.resolve_under`; `io.list_checked`;
  `http.post_bytes`; `tls.connect_timeout`, `tls.set_timeout_ms`;
  `sqlite.query_null`, `sqlite.query_null_params`; `httpd.read_request_resume`.
- **The format parsers stopped failing open.** `core:csv`, `core:json`,
  `core:toml`, `core:cli` and `core:markdown` refused to answer a different
  question than they were asked; TOML gained date and time validation, and
  percent-decoding was corrected.
- **The numeric edges are refused rather than answered wrongly** in
  `core:bignum`, `core:decimal`, `core:pool` and `core:raster`.
- A handful of undocumented byte-poking helpers in `core:zip` and `core:raster`
  (`zip.le16`, `raster.rd_u32_le` and their siblings) were removed when those
  records moved to packed structs.

### Security

- **`core:http` refuses `file://` URLs.** A URL taken from anywhere untrusted
  could previously read a local file through the HTTP client (`9124527b`). The
  same commit adds a decompression-bomb ceiling to `core:zip` and closes
  backslash traversal in `core:path`. `tycho-fetch` grew `--local` for that
  case, and refuses a `file://` URL by name.
- **`tycho-httpd` refuses ambiguous request framing** — a request carrying both
  `Content-Length` and `Transfer-Encoding`, or two disagreeing lengths, is a
  request-smuggling primitive and is now rejected rather than guessed at
  (`41cb3581`).
- **The example server refuses a symlink escape and bounds idle connections**
  (`c59c311f`). A symlink under the document root could point outside it; an
  idle peer could hold a connection indefinitely.
- **Paths reaching `system()` are shell-quoted** (`d08ac74e`). Three sites
  interpolated attacker-shaped text into a command line: the `cc` link line in
  the compiler, every `tycho_run()` in the `tycho` driver, and the URI-derived
  path in the LSP server on `didOpen`/`didChange`. A directory named
  `x;touch pwned;y` executed, and a path with a space simply failed to build.
- **`tycho-rsa` has been removed** (`0888bf28`). A pure-Tycho RSA cannot be
  constant-time — the modular exponentiation leaks the key through timing — and
  padding it and seeding it from the OS CSPRNG did not change that. Use
  `core:crypto`, which is OpenSSL. `SECURITY.md` says so.

### Fixed

- **The Windows archive shipped the wrong compiler, and four executables in it
  could not start at all.** It cross-built the C bootstrap rather than the
  self-hosted compiler — 3x slower codegen on `bench/treewalk.ty` — and every
  `.exe` it has ever shipped (`tychoc.exe`, `tychofmt`, `tycho-lsp`,
  `tycho-debug`) was linked `-pthread` with no `-static`, leaving an import of a
  `libwinpthread-1.dll` the archive does not carry. Windows refuses to start
  them. Both are fixed on `main` (`7534812f`); the archive is now read and its
  contents asserted, rather than only rebuilt (`e185caa2`).
  - **The published v0.8.0 Windows archive is still affected.** Nothing in it
    runs. Until 0.8.5 is tagged, build from source on Windows or use the Linux
    archive.
- **`tychoc.exe` could not compile from any other directory.** On Windows the
  compiler's own directory is where it looks for corelib and the runtime, and
  the lookup split the path on `/` only — so with tycho on `PATH`, every build
  outside the archive directory failed. Invisible because every gate happened to
  `cd` into the archive first (`b8cbabc3`).
- **The example server starved under load and locked out entirely.** A worker
  served one connection start to finish, so 64 silent peers delayed a legitimate
  request by 2080 ms and 64 *parked* peers meant only four requests — exactly the
  worker count — could be answered at all in five seconds. Rewritten around one
  `poll(2)` per worker over the listener and every connection it holds: 21–33 ms
  and all 64 answered, unchanged at 256 peers (`a6cab50f`). A peer that sends
  half a request head and stops now resumes instead of holding a worker for the
  whole head budget — 8 such peers cost a legitimate request 7754 ms before and
  1 ms after (`66120b82`).
- Keep-alive framing leftovers in the server, and round-trip symmetry in
  `core:bignum`.
- Building from source bootstraps stage 1 with the C compiler rather than with
  the binary it is trying to produce (`9a458e8c`).

### Verification

- The fixture corpus is 1092 `.ty` files, from 814; 636 of them are rejection
  fixtures, from 367.
- Most of the commits in this range are gate and test work no user observes.
  New lanes, each with its own negative controls: `make release-content` (both
  archives extracted and read — layout, imported DLLs, the version the packaged
  compiler reports, every `.exe` starting, and a real program emitted, linked and
  run from a foreign directory), `make handle-guard`, `make packed-check`,
  `make align-probe`, `make vector-check` scoring both compilers, and
  `make status`, which measures this tree rather than repeating what a document
  claims about it.

## [0.8.0] — 2026-09-02

**Not a breaking release.** Every one of the 300 files in the accept corpus that
v0.7.0 compiled, 0.8.0 compiles: measured by building the v0.7.0 compiler and
running both over `tests/*.ty` and `examples/*.ty` — 297 accepted by v0.7.0, 300
by 0.8.0, 0 regressions. The 12 fixtures added under `tests/reject/` document
rules that already existed; four were checked against the v0.7.0 binary and it
refused all four too.

### Compiler — the headline

- **The compiler that ships is now written in Tycho.** `tychoc1`, self-hosted,
  is what `make` builds and what the Linux archive contains (`8c156d38`). It is
  built in two stages — the C bootstrap in `src/tychoc.c` builds stage 1, and
  stage 1 builds the shipped binary, so the compiler carries its own
  optimisations. `src/tychoc.c` remains the bootstrap and is not going away.
- **On Windows the archive still ships the C bootstrap.** The mingw64 build
  cross-compiles `src/tychoc.c`; it does not cross-build the self-hosted
  compiler. Same language, same version, different implementation.
  - **Correction, 2026-09-06: the published Windows archive does not work at
    all.** All four executables in it are linked `-pthread` with no `-static`
    and import a `libwinpthread-1.dll` the archive does not carry, so none of
    them starts. Fixed on `main`, unfixed in the published 0.8.0 — see
    "Fixed" under `[0.8.5]`. Build from source on Windows until 0.8.5.
- **Compile speed is at or past parity.** On its own source the self-hosted
  compiler is faster than the bootstrap — 73 ms against 105 ms, min of 10 via
  `bench/transpile/run.sh` — and level on three other inputs. It began this
  cycle 5x slower.

### Language

- Three generic shapes that 0.7.0 refused now compile: a generic function
  returning an array under a `where` clause, a generic constructor taking a bare
  `[]`, and a lambda argument at a generic call site (`tests/generic_arr_ret_where.ty`,
  `tests/generic_ctor_bare_arr.ty`, `tests/generic_lambda_arg.ty`).
- **The surface is frozen** as of 2026-08-22 and gated by `make surface-check`:
  101 keywords and 41 builtins, no additions or removals; corelib may gain a
  function but may not lose one or change a signature.

### Core library

- Added: `io.make_dir_all` (the `mkdir -p` `tycho-ar` had hand-rolled),
  `io.append_text`, `io.copy`, `strings.format_g17`.
- `decimal.from_str` is **deprecated**, not removed. It fails open — it returned
  `0.15` for `"1.5x"` — and `decimal.parse_unchecked` is the explicit spelling
  for callers that want the old behaviour (FRICTION #56).

### Tooling

- Three new programs: `tycho-diff`, `tycho-fold`, `tycho-hash`.

### Performance

- Array-literal codegen was quadratic: 30,000 elements went from over 60 s to
  20 ms (`d9f03d99`).
- Emitted-program memory and speed are within ~3% of the bootstrap compiler's
  output across six benchmarks, measured with `bench/peakrss.c`.

### Verification

- The fixture corpus is 814 `.ty` files, from 776.
- New gates, each with its own negative controls: `make surface-check` (the
  freeze), `make version-check` (a doc may not announce a version the compiler
  does not ship), `make script-check` (every tracked `.py`/`.sh` parses, and no
  statement sits unreachable after a `return`), `make parse-check` (the second
  compiler's parser, against a committed AST census), `make docs-fences` (every
  fenced snippet in the tree is built AND run — 199 of them), plus contrast and
  code checks for the website.

## [0.7.0] — 2026-08-15

**Breaking.** Several shapes that used to compile no longer do — a copied
`handle`, a copied channel, `sink`/`inout` on an affine type, a `bounded[N]T`
generic field that had silently degraded — and two of those were live memory
errors, not style. `JsonErr` gains a variant, which breaks an exhaustive `match`
on it. Read "Language — breaking" and "Core library — breaking" below before
upgrading.

**Write new entries here, not under the release heading below** —
0.5.0 shipped without this block, so the only heading to write under was a
frozen one, and eight entries landed inside a tagged release that does not
contain them (see 0.6.0's opening note).

### Language — breaking

- **A handle can no longer be COPIED.** `g := f` on a `handle` value is a compile
  error: it gave one pointer two owners and two scope-exit destructor calls, and
  glibc reported `double free detected in tcache 2` from a four-line program.
  Reassignment (`g = f`) was already refused; only the declaration path was open.
  Migration: pass the handle as an argument, which borrows it, or bind the opener
  directly (`f := open(...)`). Nothing outside `tests/` declared a handle, so no
  known program is affected (FRICTION #43).
- **A bare handle may no longer be a struct field.** `f: File` in a struct is a
  compile error, matching every other aggregate — an array, map, tuple, `Option`
  and `Result` already refused one. `items: [R]` was refused before this only
  because the ARRAY intern helper checked its element; a handle that *is* the
  field type passed through no such helper (FRICTION #44).
- **`sink` and `inout` are refused on an affine type** — a `handle`, `Task(T)` or
  `Channel(T)` parameter may not carry either. Both were accepted and silently
  ignored: a `sink` handle callee borrowed exactly like a default parameter,
  leaving the value live and unclosed at return. Passing an affine value plainly
  is already a borrow, which is what a caller wants; consuming one would free it
  at the callee's scope exit while its destructor is emitted at the owner's
  (FRICTION #49).
- **A channel can no longer be COPIED.** `e := c` on a `Channel(T)` is a compile
  error: a channel-typed declaration is legal only from `channel(...)` itself,
  which is what the spec already required and nothing enforced. No double free
  came of the copy, but the alias hid every send and receive from the compiler's
  analysis — a program sending on every path still drew "nothing ever sends on
  channel 'c'". Passing a channel as an argument is unchanged, so a consumer
  still receives one normally (FRICTION #45).
- **A comment must now OPEN with `deprecated:` to mark a function.** The scan
  matched the marker anywhere in the line, so ordinary prose that merely
  mentioned it deprecated the next `fn` and every caller got a warning nobody
  wrote. Both real users in the tree already use the opening form, so no marker
  changed meaning (FRICTION #46).
- **Affine rules now hold through a GENERIC.** Every refusal that named a
  `handle`, `Channel(T)` or `Task(T)` was enforced on the template, where the
  field type is `$T` and not an affine type, and never re-checked on the
  instance. Four shapes compiled and now do not: an affine type as a generic
  struct field (`struct Box($T): c: $T` at a channel), as a generic enum
  payload, as a `sink $T` / `inout $T` parameter, and as the RETURN of a generic
  (`fn ident(x: $T) -> $T`). Two were real memory errors, not just rule gaps:
  the struct field aliased one channel to two owners, so a `send` through one
  was received through the other; the return double freed, with glibc reporting
  `double free detected in tcache 2` and exit 134. Migration: pass the affine
  value as a plain parameter, which is already a borrow. A **Task has no type
  syntax**, so a `$T` binding was the only way one could reach a field at all
  (FRICTION #53, #54, #55).
- **`bounded[N]T` in a generic struct stays bounded.** Instantiation rebuilt
  every array through the fixed-array constructor, dropping the capacity rule
  while keeping the size, so a field declared `bounded[4]$T` came out `[4]int`.
  A plain `[4]int` was accepted for it and pushes past the capacity grew instead
  of trapping. Migration: pass a real `bounded[N]T` — the fixed array that used
  to be accepted is now refused by name (FRICTION #52).

### Diagnostics

Four messages that an outside reader hit in the first ten minutes writing
`tools/tycho-diff` against `docs/` alone (`tools/tycho-diff/FRICTION-OUTSIDE.md`).

- **The builtin-collision warning no longer offers a remedy `package main`
  forbids.** It said "rename it, or call it qualified as `pkg.name(...)`" — but
  main has no package prefix to qualify with, and the next check makes the
  declaration a hard error, so the second remedy could not be taken. The library
  wording is unchanged, because there the qualified call really works.
- **A qualifier this file never imported says so.** `arrays.fill` without
  `import "core:arrays"` reported `package 'arrays' has no symbol 'fill'` — the
  symbol exists, and the message sent the reader to the package source, the one
  place that could not help.
- **`eprintln` names the stderr primitive.** Edit distance answered `println`,
  which is the wrong STREAM for an error and puts it on stdout, where it corrupts
  whatever the tool is piped into.
- **`main` returning a non-void type names `exit(code)`.** It named only
  `Result(void, string)`, which has two outcomes and prints one of them, so a
  0/1/2 contract like diff(1)'s looked inexpressible. It is not: `exit(code)` is
  a builtin and five programs under `tools/` already use it.

### Language — added

- **Explicit type arguments now parse on a package-qualified name.**
  `vp.ident$(int)(5)`, and with several parameters `vp.pair$(int, string)(1, "a")`.
  The `$` previously fell through to a field access and the spelling was a parse
  error in every form, while the unqualified `ident$(int)(5)` had always worked —
  so the two spellings of one call disagreed. §3/§4, Appendix A, §7.5 and §15.3
  move with it (FRICTION #39).
- **An empty call to a generic variadic may name its element type**:
  `count$(int)()` supplies the empty `[]int`. It was rejected with "pass at least
  one argument" even though the type was given, because the packing site never
  read the explicit type-argument list (FRICTION #40).

### Language — fixed

- **A `deprecated:` marker no longer leaks across files.** A note was matched to
  the `fn` below it by LINE NUMBER ALONE, and notes accumulate across every file
  in the import closure while line numbers restart in each — so a marker at line
  N in one package marked whatever `fn` sat at line N+1 in another. In corelib
  this made `crypto.aead_decrypt` report itself deprecated, quoting
  `sort.by_key`'s message: callers of a security function were told to "use
  sort_by(xs, cmp)". The note now carries its file and both must match.
  `tests/warn/pkg/deprec_crossfile/` constructs the collision in its own two
  packages rather than relying on the corelib coincidence, which would go quiet
  the moment either file gained a line.

- **A generic struct with a map-of-function field compiles.** `steps:
  [string: fn($T) -> $T]` emitted C naming an `FnC<id>` that was
  never defined, and cc rejected the program with `unknown type name 'FnC0'`.
  The array form of the same field was fixed earlier; the map body loop was the
  only one of five without the guard its siblings had (FRICTION #51).
- **A variadic called through a package qualifier now packs.** `vp.sum(1, 2, 3)`
  died with `'vp__sum' takes 1 argument(s), got 3`: the fold that gathers trailing
  arguments was skipped for a qualified callee, so the call reached the arity
  check unpacked. The same declaration called unqualified always worked, which is
  why three fixture files never caught it (FRICTION #38).
- **A deprecated function taken as a VALUE now warns.** `f := stale` warned
  nowhere, and the later `f(1)` names the binding rather than the function, so one
  line laundered the whole policy. The warning now also fires where the value is
  taken (FRICTION #47).

### Diagnostics

- **Imported types and callees are named as the source spells them.** Every
  diagnostic about a type from another package printed its mangled form —
  `declared type int but value is pool__Handle` for a user who wrote
  `pool.Handle`, and `argument 2 of 'money__add'`. This covered structs, enums,
  handles and newtypes alike, so it reached every corelib type in every message.
  Same-package types were never affected, which is why the tree never noticed
  (FRICTION #41).
- **`for k in m` over a map says what to do.** It reported `map key must be
  string, got int` — about an index the user never wrote, since the loop desugars
  to one — and named neither the loop nor the cure. It now points at `keys(m)`. A
  genuine wrong index keeps the precise old message (FRICTION #42).

### Tools

- **`tycho-make` refuses a damaged mtime in its stamp file.** Every other field
  of a stamp line was validated; the mtime went through the lax `parse_int`, so
  `17x` read as 17 and `abc` as 0. The mtime decides staleness, so a corrupted
  stamp file shipped a stale output as up to date (FRICTION #4).
- **`tycho-db` refuses a SQL integer literal outside int range.** The lexer's
  numeric span is all digits by construction, but `parse_int` returns 0 on
  overflow, so `WHERE id = 9223372036854775808` silently meant `WHERE id = 0`
  and matched the wrong rows with no error.

### Core library — added

- **`decimal.from_str_checked(s) -> Result(Decimal, DecErr)`.** The strict
  sibling of `from_str`, which fails open to a plausible WRONG NUMBER rather than
  to zero: it splits on the decimal point and lets the coefficient parse skip
  non-digits while the scale still counts them, so `1.5x` is `0.15`, `1.2.3` is
  `0.012` and the comma-decimal `1,5` is `1`. For the type this package exists to
  make exact, that is the worst failure it could have. The checked form accepts
  exactly `[-|+]digits[.digits]`; `from_str` is unchanged and still lax, so no
  caller moves (FRICTION #56).

### Core library — security

- **`core:crypto` no longer leaves the plaintext in freed memory.**
  `aead_encrypt` released the plaintext it decoded from hex, and `aead_decrypt`
  released the plaintext it had just recovered, both with a plain `free`. The
  discipline was already in the file and applied unevenly — `key_from_hex`
  cleanses its decode buffer. A third case: the recycled per-thread hex return
  buffer kept the last value it produced for the life of the thread, including
  the raw key from `key_export_hex`; it is now wiped before reuse.
- **`core:crypto` key import is constant-time.** The hex decode rejected at the
  first bad digit, timing the OFFSET of the error on the path `key_from_hex` and
  both `*_key_from_*` wrappers use. The digit decode is branch-free now. Measured
  under valgrind rather than with a stopwatch: the input is marked undefined and
  memcheck reports every branch derived from it — 7 before, 0 after. The input
  length and the single "was the hex well-formed" bit are declassified on
  purpose; nothing else about the key steers control flow.
- **`core:http` can be pointed at a private CA, and its certificate verification
  is now gated.** `SSL_CERT_FILE` and `SSL_CERT_DIR` are honoured — the same two
  `core:tls` already obeyed, so the tree's two HTTPS clients stop trusting
  different stores from one environment. They redirect trust and cannot disable
  it: no `CURLOPT_SSL_VERIFY*` is set anywhere in the shim, an unreadable path
  fails closed, and an empty value is treated as unset. Before this there was no
  way to trust a private CA at all, which blocked more than internal services: a
  gate needs a leg that must SUCCEED, and without one "the untrusted server was
  refused" reads exactly like "nothing connected", so a `CURLOPT_SSL_VERIFYPEER,
  0L` left in after debugging would have passed every lane in this tree.
  `make http-verify` closes that (FRICTION #57).
- **`math.sign` returns ±1 for the infinities.** It documented `-1 / 0 / 1` and
  derived its zero as `x - x` so one body could serve int and float. For an
  infinity that derived zero is NaN, every comparison against a NaN is false, and
  both tests fell through: `sign(inf)` and `sign(-inf)` both returned **0**. The
  derived zero stays — `zero$(T)` needs `defaultable(T)`, which does not see
  through a newtype, so it would have stopped `math.sign(Cents(-7))` compiling —
  and the NaN case is answered where it lands instead. `NaN` still returns 0,
  now by the NaN branch rather than by fall-through. Int is untouched by construction:
  integer arithmetic cannot produce the NaN that reaches the new branch
  (FRICTION #65).
- **`fmath.round` rounds.** It documented "round half away from zero" and was
  `floor(x + 0.5)`, an addition that rounds before `floor` ever runs.
  `round(0.49999999999999994)` returned **1.0** for a value strictly *below* a
  half, and `round(4503599627370497.0)` **moved an input that was already an
  exact integer** — every double at or above 2^52 is one, so the whole range was
  at risk. Decided by the fraction now (`x - trunc(x)`, which is exact for every
  finite x). `0.5`/`1.5`/`2.5` were always correct, which is why the golden never
  noticed (FRICTION #66).
- **`fmath.lerp` returns its endpoints exactly.** `lerp(1e308, 1.0, 1.0)` gave
  **0.0** instead of `b`: `1.0 - 1e308` *is* `-1e308`, so `b` is lost before `t`
  is applied, and `a + (b - a) * t` collapses. The same subtraction overflows to
  an infinity for a far-apart opposite-sign pair, making `t = 0` return NaN
  instead of `a`. Both endpoints are special-cased. No monotonicity guarantee is
  claimed for the interior (FRICTION #67).
- **`strings.pad_left`/`pad_right` no longer overshoot a multi-byte pad.** The
  deficit was counted in bytes and decremented by one per iteration while
  `len(pad)` bytes were appended, so `pad_left("x", 5, "ab")` returned **nine**
  bytes for a width of five, and a UTF-8 pad (`"é"`) did the same — the case a
  formatter reaches for, and the one that looks right in a terminal
  while being far too wide. A pad that does not divide the deficit now leaves the
  result **short** of width rather than over, which also keeps it from being split
  mid-codepoint. Every caller in the tree passes a one-byte pad and is
  bit-identical (FRICTION #68).
- **`sqlite.exec` reports the rows it actually changed.** It read
  `sqlite3_changes`, which describes the most recent statement alone, while
  `sqlite3_exec` runs every statement in the string — so
  `exec("INSERT 'a'; INSERT 'b'")` inserted two rows and answered `Ok(1)`. It
  reports a delta of `sqlite3_total_changes` now. Single-statement callers see
  the identical number. **Documented alongside it, not changed:**
  `exec_params`/`query_params` go through `sqlite3_prepare_v2` with a NULL
  `pzTail`, so everything after the first statement is discarded and the call
  still returns `Ok` — the opposite of `exec`. Pass one statement per call to the
  `_params` forms; the limitation is pinned by a fixture rather than left to
  drift (FRICTION #69).
- **`core:sqlite`'s usage example no longer shows `defer`**, which this language
  refused in 2026-08-10. A reader copying the package header got *"a statement
  must be a declaration, assignment, or call"* — a message that never mentions
  `defer`. One instance tree-wide (FRICTION #69).
- **`sqlite` bound parameters survive an interior NUL.** `sqlite3_bind_text` was
  given `-1`, which reads to the first NUL, while a Tycho string is
  length-carrying and may contain one: a 5-byte `"hi\0zz"` stored **two** bytes
  and the call returned `Ok`. The failure mode is collision rather than lost
  characters — two values the program treats as different become one row, which
  matters for a token, a key or a filename. Now bound with `len(params[i])`; the
  value round-trips whole. `sqlite3_prepare_v2` got the same treatment
  (`len(sql)`), though that side is program-authored and is **not covered by a
  test** (FRICTION #70).
- **`toml.parse` refuses a repeated `[table]` header.** The second `[t]`
  **replaced** the first rather than merging, so `[t] x=1 [t] y=2` silently lost
  `x` — the same "override nobody can see" as the duplicate key, and worse, since
  that one at least kept a value. `[[arr]]` still repeats (that is how an array
  of tables is spelled) and `[a]` followed by `[a.b]` still nests; both are
  gated. **The header's contract is narrowed to match reality:** no input is
  accepted that would lose data or invent a value the text does not carry.
  "Nothing is guessed" was untrue of `a = 01`, `a = 1__0`, `a = 1.` and
  `a b = 1`, which stay accepted and are now listed in the header with what each
  yields — a documented subset rather than a validator (FRICTION #62).
- **`crypto.pbkdf2_sha256` no longer truncates a password at its first NUL.** The
  shim called `strlen`, so `"secret\0A"` and `"secret\0B"` derived the **same
  key** — and the same one as `"secret"`. The failure is collision, not weakness:
  the output is a good PBKDF2 hash of the wrong input, so two distinct
  credentials authenticate against each other. The length is passed explicitly
  now and fails closed rather than guessing. **A signature change**: the extern
  `cx_pbkdf2_sha256` takes a `pwlen`; the Tycho-facing `pbkdf2_sha256` is
  unchanged, so no caller moves (FRICTION #75).
- **`crypto.ct_equal` no longer compares two hex strings by their prefixes.** An
  interior NUL shortened both inputs, so two different hex strings truncating to
  the same prefix compared **equal**. **Not an authentication bypass** — a
  computed MAC carries no NUL, so its length disagreed with a truncated attacker
  value and the answer was already false — but a caller comparing two *supplied*
  values had a collision. Lengths are passed and a mismatch refuses. A signature
  change to the extern only; `ct_equal` itself is unchanged (FRICTION #76).
- **`http.get`/`get_body`/`get_status`/`post` refuse a URL with an interior
  NUL**, and **`datetime.offset_at` refuses a TZ with one.** Both truncated at
  the NUL: a `file://…/real.txt\0/ignored` fetch returned real.txt's bytes, and
  `offset_at("EST5\0UTC0")` applied EST rather than the string given. A truncated
  URL is not a shorter one, it is a different one. This closes the sweep — all 46
  corelib externs taking a `string` are now length-carrying or NUL-guarded
  (FRICTION #77, #78).
- **`json.parse_checked` refuses a document nested past 2000 instead of aborting
  the process.** The parser is recursive descent, so depth is C stack: at 50000
  the runtime killed the process with `tycho: stack overflow` and exit 1. That
  guard is safe, but a process that dies cannot return `Err` — and
  `parse_checked` is documented as the entry point that tells you whether the
  document survived. A server that chose it for exactly that reason still died on
  100 KB of `[`. **New `JsonErr.TooDeep` variant** — a breaking change for an
  exhaustive `match` on `JsonErr`; nothing in this tree matched on it outside the
  package (FRICTION #79).

### Core library — breaking

- **`io.write_bytes`, `io.write_at`, `io.set_mtime` and `io.sync` return
  `Result(void, IoErr)`**, not `Result(bool, IoErr)`. Every one of them returned
  `Ok(true)` or an `Err` — `Ok(false)` was unreachable — so the bool made
  `or_return` produce a value the caller had to bind and then guard against a
  case that could not happen (FRICTION #15). `io.write_bytes(p, b) or_return` is
  now a statement. Migration: drop the binding, and rewrite a `match` arm
  `Ok(x)` as `Ok()`. **`io.is_dir`, `io.make_dir` and `io.remove` are
  unchanged** — their `Ok(false)` is a real second answer ("it was already how
  you asked"), not a placeholder.

## [0.6.0] — 2026-08-11

Seventy commits since `v0.5.0`. **Eight of the entries below were written into
the `[0.5.0]` section after `v0.5.0` was tagged** and are moved here, where they
belong: `git merge-base --is-ancestor` puts every one of them outside the tag.
The cause was structural, and the `[Unreleased]` block above is the fix. Read
`compress.decompress`, `io.read_text`, qualified consts, the newtype-collection
fix, `or_return` in `main()`, the live-copy remedy, the `[string]`-as-a-value
diagnostic and the `tycho-ar` parser share as 0.6.0 changes; a `v0.5.0` tarball
does not contain them.

### BREAKING CHANGES

Six changes refuse or re-shape code that compiled under 0.5.0. All six migrations
are mechanical, and each is stated as "what you wrote" → "what you write".

- **A binding may not start with an uppercase letter.** Locals, parameters,
  destructuring targets, loop variables, and `match`/`select` bindings must now
  begin with a lowercase letter or `_`. The uppercase namespace belongs to
  compile-time entities — types, enum variants, consts — so it is no longer
  *expressible* for a binding to shadow a constructor. This is the
  OCaml/Haskell/Elm/SML approach: the bug class is removed by the grammar rather
  than diagnosed by a special case. The guard sits at one point, `vars_push` in
  `src/tychoc.c`, which all ten runtime binding forms reach, and it sits in the
  resolver rather than the parser — a bare `Name` in a payload slot
  is indistinguishable from a binding at parse time, so checking after the
  pattern is promoted keeps `Err(NotFound)` legal while still refusing a genuine
  binding.

  ```
  fn hash(K: int, S: int) -> int:   →   fn hash(k: int, s: int) -> int:
      A := K + S                            a := k + s
  ```

  **`const` is exempt and unchanged**: 264 of this tree's 266 const declarations
  start uppercase and none starts lowercase. Migrating this tree took 16 sites in
  6 files (md5, sha256, dijkstra, weblog, decimal); md5 and sha256 each keep a
  one-line note mapping the new names back to RFC 1321 and FIPS 180-4, because
  that mapping is how a reader checks the algorithm. Specified in
  `docs/spec/01-lexical.md` §3.5.1.

- **`is` is a reserved keyword.** It is the variant test (below), and it is
  reserved rather than contextual — an operator keyword shares its position with
  an ordinary name in `x is y`, which is exactly where a contextual spelling
  becomes ambiguous. **A variable, parameter or function named `is` no longer
  compiles.** No `.ty` file in this tree used it, so the migration cost here was
  zero, but the name is gone from the identifier space. Listed in
  `docs/spec/appendix-b-keywords.md`.

- **A function may not be named for a constructor, an enum variant or a const.**
  `fn Ok(...)`, `fn Err(...)`, `fn Some(...)`, `fn None(...)` and any `fn` whose
  name collides with an enum variant or a const are refused with `'<name>' is
  already defined`. Previously such a definition compiled and then shadowed the
  constructor at every use site, which is the same bug class the binding rule
  above closes, approached from the definition side. Rename the function.

- **`core:iter` predicates return `bool`, not `int`.** Four functions:
  `filter`, `try_filter`, `count` and `any` took `fn($T) -> int` and treated
  nonzero as true. A predicate is a question, and `int` let an *arithmetic*
  result stand in for an answer — `count(xs, fn(x) -> int: x - lim)` reads as a
  subtraction and counts everything unequal to `lim`, which is almost never what
  the author meant.

  ```
  iter.filter(xs, fn(x: int) -> int: x % 2)        →   iter.filter(xs, fn(x: int) -> bool: x % 2 != 0)
  iter.count(xs, fn(x: int) -> int: x - lim)       →   iter.count(xs, fn(x: int) -> bool: x != lim)
  ```

  A named predicate changes the same way: `fn even(x: int) -> int` returning
  `1`/`0` becomes `fn even(x: int) -> bool: return x % 2 == 0`. `map`, `try_map`
  and `reduce` are unchanged.

- **`image.decode` and `image.encode` return a `Result`.** `decode` answered a
  0×0 `Image` sentinel for every failure and `encode` answered empty `bytes`, so
  a truncated file, a JPEG handed to a PNG decoder and a genuinely zero-dimension
  image were the same value. Now `decode(data: bytes) -> Result(Image, ImgErr)`
  and `encode(img: Image) -> Result(bytes, ImgErr)`, with `ImgErr` naming the
  cause: `Empty`, `NotPng`, `Corrupt`, `BadDims`, `ShortPixels`, `Failed`.

  ```
  img := image.decode(b)          →   img := image.decode(b) or_return
  if img.width > 0:                   # or match Ok(img): / Err(e):
  ```

- **`compress.decompress` returns `Result(bytes, ZErr)`.** It returned bare
  `bytes`, empty on any failure — so a corrupt stream, a truncated one and a
  legitimately empty payload were the *same answer*. An archive can hold a
  zero-byte member, so for any container format a corrupt member read as an empty
  one: data loss that looks like data. Now `Corrupt` / `Truncated` / `Failed`,
  and `raw_decompress` likewise. `compress` itself still returns bare `bytes`.

  ```
  out := compress.decompress(b)   →   out := compress.decompress(b) or_return
  if len(out) == 0: ...
  ```

  The shim always knew which branch it took and discarded it; it now reports a
  status through an `inout int`, the FFI shape `core:io` already used. Callers
  updated: `core:zip` (its CRC check already covered the collapse),
  `tools/tycho-ar` (reports the cause instead of inferring damage from a length
  mismatch — the length check stays, since a payload that inflates cleanly to
  the wrong thing is forgery, not damage), and the worked example.

- **`tycho-ar x` exits non-zero when an mtime could not be restored.** Extraction
  used to `die` on the first failed `set_mtime`, leaving the archive half
  unpacked. It now warns per member on stderr, finishes the extraction, and exits
  1 — so the files are all there and the exit status still says the restore was
  incomplete. A script that ran `tycho-ar x` and checked `$?` will now see a
  failure where it previously saw success on a filesystem that refuses
  timestamps. Exit 0 continues to mean "fully restored".

### Language

- **`is`, the variant test.** `x is VariantName` yields `bool`, for a plain enum
  and for `Option` and `Result`. It is the tag test `match` already emitted, so
  it is true for a payload-carrying variant without naming the payload — the
  point is to *ask* without destructuring, where a `match` with a `_:` arm was
  the only way to phrase the question. The right-hand side is a variant name and
  is never resolved as an expression; it binds nothing. `is` does not chain:
  write `a is X and a is Y`. Precedence sits between additive and comparison.

  ```
  if v is VNull: ...        o is Some     r is Ok     r is Err
  ```

  `core:result` now asks with it internally — `is_ok` is `return r is Ok` — and
  `is_some` was rewritten the same way. Both keep their signatures and their
  meaning; this is an implementation change, not an API one.

- **A `[string]` may cross the FFI as an extern parameter.** It arrives in C as
  `(const char *const *, long)` — a pointer and a length, **not** a
  NULL-terminated `argv` — borrowed for the duration of the call, with nothing
  copied and nothing to free. An empty array may pass a null pointer with length
  0. Parameter position only: a `[string]` return is refused, and `inout string`
  stays refused. Documented in `docs/spec/14-ffi.md`.

  This retires a whole builder protocol. `core:os` used to reach C through
  `osx_argv_new` / `osx_argv_push` / `osx_argv_free` and a `ptr` handle; it now
  declares `extern fn osx_exec(argv: [string]) -> int` and passes the array.
  `os.exec` and `os.exec_out` keep byte-identical signatures — no caller outside
  corelib changes.

- **f-string interpolations evaluate left to right.** `f"{a()}{b()}{c()}"`
  printed A, B, C but *called* c, b, a: the holes lowered to arguments of a
  single `tycho_str_concatN`, and C leaves argument evaluation order
  unspecified, so gcc chose right to left. Each piece but the last is now bound
  to a temporary in a braced group, making evaluation follow source order.

  Observable only when a hole carries a call with side effects, which is exactly
  how it was found: an `io.remove` in one hole ran before the `io.mtime` printed
  to its left. Bare `+` concatenation and ordinary call arguments remain
  unspecified — this fixes the construct that *looks* sequential.

- **Top-level `const`s cross package boundaries.** `pkg.NAME` reads an exported
  constant, **folded to its literal at the use site** exactly as an unqualified
  const is — no runtime global is emitted, so the reference costs nothing. A
  package that wanted to publish a level had to ship it as a function before
  this, because `levels.DEBUG` resolved as an enum variant and died "has no
  variant".

  No export keyword and no new syntax: the leading-underscore rule the language
  already had does the privacy, so `pkg._NAME` is refused unchanged. One
  position is left out and marked `gap:` in the source — a
  qualified const as a fixed-array length (`[pkg.CAP]int`), because an array
  length is resolved while parsing, when the imported package may not be parsed
  yet; accepting it there would fail or not depending on file order.

- **A newtype over a collection now supports that collection's operations.**
  `type Ids = [int]` was effectively write-only: `len`, indexing, slicing,
  `push`, `pop`, `for … in`, index-assign and every map builtin all refused it,
  and `to_under` was the only door. ROADMAP.md recorded the symptom as "blocks
  `push`"; a probe found all of them refusing.

  A newtype's purpose is **distinctness** at assignment, parameter passing and
  comparison — not hiding what the underlying value can do. Those boundaries are
  unchanged: `[int]` still cannot be passed where `Ids` is wanted, in either
  direction, and two newtypes over the same underlying type stay incompatible.
  A slice or a `map_set` yields the *underlying* type, since it is a fresh value
  rather than the named one.

- **`or_return` works in `main()`.** `fn main() -> Result(void, string):` is a
  second legal shape for the entry point, so error propagation no longer stops
  one frame short of where a program starts. On `Ok` the process exits 0; on
  `Err(msg)` the message is written to stderr — **bare, with no prefix**, the
  same way `die(msg)` prints a user message — and the process exits 1.

  The error type is `string` and the ok payload is `void`:
  an `Err` reaching the entry point is *printed* and `has_str` admits no enum,
  so the error is the message; and a value in the ok slot would make the exit
  status ambiguous. Both wrong shapes are refused with a diagnostic that names
  which half is wrong.

### Core library

- **`io.mtime` and `io.set_mtime`.** `mtime(p) -> Result(int, IoErr)` reads a
  modification time in seconds; `set_mtime(p, secs) -> Result(bool, IoErr)`
  writes one. `set_mtime` does not create a missing file — that is
  `Err(NotFound)` — and a directory is fine. The access time is
  left alone (`utimensat` with `UTIME_OMIT`), so preserving a timestamp is
  `io.set_mtime(dst, io.mtime(src) or_return) or_return` and nothing else moves.
  Closes FRICTION #8; it is what let `tycho-ar` restore mtimes at all.

- **`strings.slice_bytes` and `strings.slice_str`**, returning
  `Result(_, SliceErr)` with `OutOfBounds` and `Inverted`. A `bytes` or `string`
  slice **clamps** silently — `b[2:10]` on a 5-byte value yields 3 bytes rather
  than failing — while an *array* slice aborts. That asymmetry is now written
  down as the normative rule, and these two are the fail-closed door for code
  that would rather hear about it: a parser gets a real error instead of a
  quietly shortened slice that looks like a short record.

- **`iter.try_map` and `iter.try_filter`**, the fallible pipeline stages.
  `try_map(xs, f: fn($T) -> Result($U, $E)) -> Result([$U], $E)` and
  `try_filter(xs, keep: fn($T) -> Result(bool, $E)) -> Result([$T], $E)`. Both
  stop at the first `Err` and return it unchanged, with no partial array — so a
  parse over a list is `nums := iter.try_map(lines, parse_int) or_return`
  instead of a hand-rolled loop with an early return. Closes FRICTION q#7.

- **`result.map_err_with(r, f)`**, `fn($E) -> $F`, so the cause survives the
  translation. `map_err` replaces an error with a *constant* of the new type,
  which discards whatever the original said; `map_err_with` runs a mapper, so a
  `ParseErr` can become an `AppErr` that still carries which field failed.

- **`decimal.div(a, b, scale, mode) -> Result(Decimal, DivErr)`**, with
  `HALF_UP` and `TOWARD_ZERO` (also spelled `decimal.half_up()` /
  `decimal.toward_zero()`). Three failure causes, not one: `DivByZero`,
  `BadScale(int)` for a negative scale, and `BadMode(int)` for an unknown mode.
  Closes FRICTION q#2. A worked example that claimed `DivByZero` was the only
  cause was corrected with it.

- **The `core:result` combinators work at `Result(void, E)`.** `is_ok`, `is_err`
  and `err_or` could not be instantiated at a void payload: each bound `v`, and
  `Ok(v)` is refused where the payload is void while a bare `Ok:` arm is refused
  everywhere else. Rewritten with `r is Ok` / `r is Err` and an `Err(e):` plus
  `_:` pair — **no compiler change was needed**, which is the useful part of the
  finding. `unwrap_or`, `map_err` and `map_err_with` are still out of reach at a
  void payload, because each must produce or accept the payload itself.

- **`core:io` gains `read_text(p) -> Result(string, IoErr)`.** `io.read` is
  fail-open: it answers `""` for a missing file, an unreadable one and a
  genuinely empty one alike — three different facts flattened into one value.
  `read_text` keeps them apart, reporting the same `NotFound` / `IsDir` /
  `Failed` causes `read_bytes` already did. `io.read` is unchanged and not
  deprecated.

- **`tools/tycho-ar` stops carrying its own integer parser.** `parse_uint` now
  shares `strings.parse_int_checked`'s lexical rule and keeps only the domain
  rule that belongs to the archive format — non-negative, and capped far below
  int64 so a forged length cannot drive a huge allocation. Not a straight swap:
  `parse_int_checked` accepts a leading `-` and the full int64 range, so
  replacing the call outright would have widened what a header field accepts.

### Diagnostics

Four messages stopped pointing at the wrong place or withholding the answer.

- **A refusal inside a generic names the call site.** An error raised while
  checking an instantiated template printed only the template's own location, so
  a user saw `corelib/result/result.ty:125: error: ...` with nothing connecting
  it to their code. A second line now follows: `./main.ty:9: note: required from
  here -- this call instantiated the generic`. That call is what the reader
  needs.

- **A non-exhaustive `Result` or `Option` match names the uncovered variant.**
  A match that wrote both sides but covered only some `Err` variants got
  `match on a Result must cover both Ok and Err`, which is wrong twice over —
  both sides *were* written. It now reads `non-exhaustive match: missing variant
  <V> of <E>`, the same wording the plain-enum path already used. The original
  message survives for a genuinely absent side. `Option`'s partial `Some` side
  is fixed with it.

- **The binding rule outranks the use-site parse error.** `fn f(Ok: int)` died
  with `expected '(' after Ok` — a parser complaint about a construct the user
  never intended — and never named the rule being broken. It now says `'Ok'
  cannot name a binding -- a local, parameter or pattern binding must start with
  a lowercase letter or '_'; as a value it is a constructor: Ok(x)`, which names
  both the rule and the other reading.

- **A corelib diagnostic names the corelib file.** An error inside a generic's
  substituted body was reported against whatever file was current — typically
  the user's `./main.ty` — paired with the *template's* line number, so it quoted
  an innocent line of the wrong file. It now names the template's own file.

- **The live-copy warning no longer suggests a no-op.** It told you to "pass a
  copy you keep (`y := s`)". Measured on the emitted C: that spelling silences
  the warning and emits the *same* two copies, while making the value's last use
  the aggregate emits one. The warning itself is right — there is an avoidable
  copy — so it stays, with only the remedy that removes one.

- **`[string]` written where a value goes** now names both working forms
  (`[]string`, or `xs : [string] = []`) instead of "expected an expression".

### Fixed

- **`sig_find`'s pointer dangled across argument resolution — a use-after-free
  in the compiler.** `sig_find` returns a `Sig *` into the growable `g_sigs`
  table. Resolving an argument can instantiate a generic, which appends to
  `g_sigs` and reallocates it, leaving the callee's pointer pointing at freed
  memory for every check that followed — inout, arity, types. The symptom was a
  *bogus refusal read out of the freed slot*: a valid program rejected with
  something like `argument 1 of 'print' is inout`, and the rejection depended on
  allocator behaviour, so it came and went with unrelated edits.

  The fix re-derives the index rather than holding the pointer: `int si = (int)(s
  - g_sigs);` once, then `s = &g_sigs[si];` after each `resolve_exp`. Locked by a
  deterministic fixture, `tests/generic_sig_realloc.ty`, which forces 200
  distinct instantiations inside `print`/`str` arguments with large
  bodies so it crosses several reallocation boundaries every run — not left to
  ASan to notice, because a use-after-free that reads plausible garbage is
  the one a sanitizer-only gate can miss on some allocations.

- **A `bytes` slice's clamping is now specified**, along with the array slice's
  abort, in the spec and in a worked example — behaviour is unchanged, but the
  difference between the two was previously implied rather than stated.

### Gates and tooling

Not user-facing API, but the record matters: **eight checks that could not fail
now can**, and two of them were sitting over live holes.

- **`corelib/run.sh` and `examples/corelib/run.sh` printed all-green while
  silently skipping.** A package or example whose dependency was missing was
  counted as passing. Both now count skips and name them —
  `N ok, M SKIPPED -- image(missing: libpng)`. This is the failure mode where a
  gate reports success *because* it did not run.
- **`tycho-ar`'s round-trip gate was green over the mtime hole.** Extraction
  restored no mtimes and the gate compared with `diff -r`, which ignores them.
  Leg 3b now compares the timestamps themselves — verified to fail, naming all
  eight members, with `set_mtime` stubbed out.
- **Reject fixtures may pin their diagnostic.** A reject fixture scored only on
  non-zero exit plus non-empty stderr, so a diagnostic that regressed to *wrong
  but still refusing* passed. An opt-in `# expect: <text>` line now pins the
  message.
- **`prunner` scores abort fixtures by their `.err` golden.** `judge_abort`
  never learned that branch, so `make test-fast` disagreed with `make test` and
  the `# expect:` fixtures were scored by nothing in the parallel runner.
- **`bench/` is compiled by a gate.** 51 `.ty` files under `bench/` were
  compiled by nothing; `scripts/entrypoints.sh` now sweeps them (24 → 75 entry
  points), so a language-rule change that breaks a bench file reddens instead of
  waiting to be found by hand.
- **Raw control bytes are rejected in tracked Markdown.** A literal NUL had
  landed in two tracked docs and passed every check; `check_links.sh` now
  rejects 00-08, 0B, 0C and 0E-1F.
- **Citations are gated in CI and pre-push.** `make check-links` — links plus
  `path:line` citations — was manual-only and run by neither, and a red citation
  gate had already ridden a commit onto `main`. Both now call it.
- **The pre-push hook dropped `make ci N=0`.** ~274s on every push taught
  bypassing. It keeps the sub-second link and
  citation gate plus a fuzz smoke test.
- A hard-coded fixture count in `tests/run.sh`'s prose was **dropped rather than
  refreshed** — it was stale by 34 and load-bearing for nothing, and a snapshot
  count in prose is stale again by the next commit.

## [0.5.0] — 2026-08-10

**The first release ever cut.** Everything below ships in it. The section was
split in two until 2026-08-10 — an `[Unreleased]` block on top of a `[0.5.0]`
block dated 2026-08-05 that was never tagged — which would have shipped a
changelog that did not describe its own artifact. The 08-05 block is a draft,
not history, so the two are merged here under the date this shipped.

Eight entries that were appended here *after* the tag were moved to `[0.6.0]`
on 2026-08-11; a `v0.5.0` tarball does not contain them.

- **DEPRECATED: `sort.by_key(xs, key)`** — use `sort.sort_by(xs, cmp)`. The
  rewrite is mechanical: `by_key(xs, k)` is
  `sort_by(xs, fn(a, b) -> int: k(a) - k(b))`. It keeps working for all of 0.x
  and is **removed in 1.0**. Calling it warns.

  This is the deprecation policy in `docs/reference/corelib.md` run end to end for
  the first time. Step (3), the compiler warning, is now a general mechanism rather than
  a one-off: a `# deprecated: <text>` comment line **directly above** a `fn`
  marks it, and every call site warns with that text. Locked by
  `tests/warn/deprecated.ty`.

- **`core:net`'s concurrency ceiling is now stated rather than implied.** There
  is no `poll`/`select`/`epoll`/`O_NONBLOCK` in any of its 12 exports, so a
  server's worker count is a hard limit on concurrent connections and one slow
  client occupies one worker. Written into the corelib guide instead of being
  fixed: readiness polling was costed at ~283 lines across 4 files plus a
  redesign of `core:httpd`'s read surface, and adding it later is additive.

- **The Windows parked-`recv` difference is a documented platform limit.** A
  thread parked in `recv` is not released by the shutdown handler as it is on
  Linux, so a Windows server winds down within its idle timeout rather than a
  millisecond. Nothing is lost or corrupted. Recorded in the README's platform
  notes; the measurement stays in `SECURITY.md`.

- **`pass`, the no-op statement.** A block cannot be empty, so a `match` arm or
  an `if` branch with nothing to do had to bind something nobody reads — `ok :=
  sz`, `_ := 0` — because a declaration is a legal statement where a bare
  expression is not. `pass` says it instead.

  It is **contextual, not reserved**: significant only when it is the whole
  statement. `pass` was already a variable name in `corelib/test/testing` and
  `tools/prunner`, and reserving it would have broken the runner that scores
  `make test-fast`, so `pass := 0` and `pass = pass + 1` still mean what they
  did. Used where a value is required — a value `if`/`match` branch tail — it is
  refused with a message naming the fix, unless a variable of that name is in
  scope, in which case it is that variable.

- **`core:sort` gains `sort_by(xs, cmp)`.** A three-way compare
  `fn($T, $T) -> int`, so an order can use several keys, mixed directions, or a
  type with no `comparable` instance — none of which fit `asc`/`desc`/`argsort`
  (values, one direction) or `by_key` (an `int` key, and no order-preserving
  injection of a string into one). Bottom-up merge, stable: equal elements keep
  their input order under a descending compare, which a total-by-index
  comparator would silently reverse.

- **`tychofmt` no longer writes `-1` as `- 1`.** Prefix negation is told from
  binary subtraction by what precedes it. Its two existing gates could not catch
  this: `- 1` is idempotent and emits identical C, so the formatter now also
  asserts canonical spellings.

- **`Result(void, E)`, for an operation that either fails with a reason or
  succeeds with nothing to report.** `void` is now spellable as a `Result`'s ok
  payload — and in no other position — as a type with no values: constructed by
  a zero-argument `Ok()`, matched by a bare `Ok:` arm, never bindable. The
  permission does not nest, so `Result(Option(void), E)` is still refused, and a
  `Result`'s error type still may not be void. Retires the one-field `Unit`
  struct that fallible-but-valueless helpers carried, and with it the
  meaningless `Ok(0)` at the end of every validator.

  Shipped with it, because the feature is unusable without it: **`f() or_return`
  is now a statement** when the ok payload is void. It was previously rejected
  as a bare expression with no effect, which left a void-payload `Result` no
  propagation form at all — `x := f() or_return` has nothing to bind. Over any
  other payload type it is still an error, now naming the type it would have
  dropped. `docs/internals/FRICTION.md` §4 recorded these as one defect with
  three independent sightings; they are closed together.

  Internally, the generic bind vector's "not yet bound" sentinel moved off
  `T_VOID` to its own `T_UNBOUND`. That collision was latent rather than live —
  `void` was not in the type grammar at all — but it is what would have made
  binding a type parameter to `void` read as unbound.

- **Demoted from 1.0.0 to 0.5.0.** `TYCHO_VERSION` in `src/tychoc.c` now reads
  `0.5.0`, and every document that promised a stable surface says pre-1.0
  instead. Nothing about the implementation changed and nothing regressed: the
  demotion is about the promise, not the code. 1.0 says "I will not break you",
  and that had never been tested — `git tag` showed only `v0.1.0`, the
  `[1.0.0]` entry below describes a release that was never cut, and no one
  outside this repo has written a line of Tycho. Withdrawn with the number: the
  corelib API freeze (`docs/reference/corelib.md`) and the spec's "first frozen
  version" (`docs/spec/00-conventions.md` §1.5). The spec stays normative and
  the implementation stays gated against it. `ROADMAP.md` gains "What 1.0
  requires" — the conditions, so the number is earned next time rather than
  declared.

### Earlier in the 0.5.0 cycle (recorded 2026-08-05, at the time as 1.0.0)

The promotion that was later withdrawn. The language surface and the spec are
**not** a stability contract at 0.5 — the README's status banner says so, and
`ROADMAP.md` lists what 1.0 would require. What follows is the chain of work
that landed before the version number was corrected.

- **Batteries-included corelib (phase 1).** Six new packages, each with a test
  + golden + guide entry: `core:testing` (assert/eq/report unit-test state),
  `core:toml` (recursive table/array parser, fail-closed), `core:log`
  (leveled logging, threaded state), `core:sqlite` (direct FFI over
  libsqlite3 — the dbquery pattern), `core:utf8` (validation and codepoint
  iteration, strict rejection of overlong/surrogate/truncated sequences), and
  `core:zip` (reader/writer over a new raw-deflate path in `core:compress`,
  CRC-32 in-package). `core:compress` gained raw deflate/inflate.
- **Vendored dependencies (phase 2).** The importer-relative import story
  documented as the vendoring convention (`vendor/` layout, flat-vendor
  shape); `tycho-fetch <url> <name>` downloads and unpacks a package tarball
  into `vendor/` with sha256 verification (dogfooding core:http + core:sha256
  + core:io); `core:http` gained `body_bytes(r)` — the string `body()`
  truncated at NULs, so binary downloads were impossible.
- **Debugger (phase 3).** `tycho debug <file.ty>` / `tools/tycho-debug/` — a
  gdb adapter: compiles with `-g`, drives a scripted gdb session over pipes
  (breakpoints by source line, step/next, locals under their Tycho names,
  Ctrl-C interrupts a running inferior). Gated by `make debug-check`.
- **1.0 promotion (this phase).** `tychoc --version` and the version constant;
  the README's status banner flips from research prototype to the 1.0
  contract (stable: the language surface and the spec; not stable:
  performance tuning and benches); a security review of the FFI shims
  (string/bytes ownership, the core:os shell-out paths, the TLS wrapper)
  with findings recorded in `SECURITY.md`; and the corelib 1.0 API-freeze
  decision with its deprecation path, recorded in `docs/reference/corelib.md`.

## [0.1.0] — earlier

The pre-1.0 research-prototype release. See `RELEASE_NOTES.md` at the
`v0.1.0` tag for what it contained; it carried no stability guarantees.
