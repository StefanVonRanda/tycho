# Changelog

User-visible changes, per release. The version constant is `VERSION` in
`compiler/main.ty` (printed by `tychoc --version`); per-release publishing notes
are in `RELEASE_NOTES.md`. `surface.lock` is the frozen record of keywords,
builtins and corelib signatures.

## [Unreleased]

### Language

- **An int or float `const` adapts like its literal**: `ev.kind == EV_QUIT` at an `i32`
  field, `x: u8 = FLAG`, `h: f32 = HALF` compile as they would with the literal written in
  place (range checks included).
- **`comparable(T)` admits the sized numerics** (`u8`…`u64`, `i8`…`i64`, `f32`), so
  `sort.asc` over `[f32]` and `math.max` on two `f32`s compile.
- **Spec: `parallel for` captures are shared, not deep-copied** (13 §22). This is what the
  implementation always did; capturing a large array costs nothing per chunk.
- **`f32` widens to `float` in an unannotated array literal and at a generic `$T`**:
  `[x, v.y]` is `[float]` and `math.min(x, v.y)` instantiates at `float`, as `x + v.y`
  already did. By value only; an array of `f32` still never becomes an array of `float`.
- **`push(xs, b)` with `xs: [u8]` appends bytes as bytes**, one copy, so a
  byte builder no longer needs an `[int]`.
- **The bytes bridge takes vectors and number arrays.** A packed struct may hold a
  `vector[N]T` field (laid out as its lanes), and `to_bytes(xs)` /
  `from_bytes$([T])(b)` take an array of a sized number (`[f32]`, `[u32]`,
  `[i64]`, `[float]`, ...), each element little-endian. `[int]` keeps its
  one-byte-per-element meaning.
- **`to_float(b)` and `to_int(b)` take a `bool`** (`1.0`/`0.0`, `1`/`0`), so a
  flag becomes a number in one expression instead of a three-line `if`.
- **`for i in a..<b:` counts sequentially.** It is `for i := a; i < b; i += 1:`
  (so `b` is re-read every pass); `range()` stays removed.
- **Vector maths reads like shader code.** Unary `-` on vectors, `f32` and the
  signed sized ints; a named swizzle `v.yzxw` (2 or 4 letters) reads lanes as a
  vector; a `float`/`f32` scalar variable scales a `float`/`f32` vector or array
  (`v * k`), and adapts inside a declared literal (`[x, y, z, 0.0]`); a scalar
  fills a declared vector (`z: vector[4]f32 = 0.0`); generics take
  `vector[$N]$T`, instantiated per width.
- **`f32` widens to `float`** in arithmetic, comparisons, a declared `float` and a
  by-value `float` parameter. It is lossless; `float` -> `f32` stays explicit, and
  `inout` and arrays never widen.
- **An `extern fn` parameter may be an array of a sized scalar.** `[f32]`
  crosses as `const float *`, `[u32]` as `const uint32_t *` (likewise the other
  `u`/`i` widths), with the length after it and no conversion (spec §24.1).
- **`[packed T]` crosses the bytes bridge as one array.** `to_bytes(xs)` writes
  every record in order, little-endian; `from_bytes$([T])(b)` reads the array
  back and refuses a length that is not a whole number of records.
  `push(dst: [int], src: bytes)` appends a byte source in one growth step
  (spec §17.1b).

### Corelib

- **`os.exe_path()`**: the running program's own file, absolute, so data kept beside a
  binary is found when it is started from `PATH` (where `args()[0]` has no directory).
- **`cli.get_all(c, key)`**: every value of a repeated option, in order.
- **`hash.crc32` runs at memory speed** (64 MB: 398 ms -> 5 ms; arm64 CRC32
  instructions, slicing-by-8 elsewhere), and takes `bytes` too: `crc32_bytes(b)`, and
  `crc32_update(crc, b)` to continue a CRC over chunks. `zip.crc32` is now the same code.
- **`arrays.extend(&xs, ys)`** appends every element of `ys` to `xs` in place,
  where `concat` builds a new array (and so copies `xs` on every call in a loop).
  Pushes onto an `inout` array in a loop now take the same fast path as onto a
  local (unless the loop can return early), which made `extend` 1.8x faster.
- `arrays.push_index(&xs, v)` appends and returns the new element's index;
  `arrays.OptIndex` is a one-int "maybe index" (`no_index`, `some_index`,
  `is_none`, `get_or`).
- `core:pool` gains typed ids: `add_typed` returns an `Id($T)`, and
  `get_typed`/`set_typed`/`remove_typed` refuse an id from a pool of another
  element type at compile time. The `Handle` API is unchanged.

- `core:bits` gains `f32_bits` / `f32_from_bits` and `f64_bits` /
  `f64_from_bits`: exact IEEE-754 bit patterns for binary formats.
  `core:cbor` now uses them instead of a private shim.
- `core:gfx` events tell apart what they used to fold together: kind 4 is a
  mouse button press and the new kind 7 its release; kind 5 is a logical
  resize and the new kind 8 a drawable (pixel) size change; and the new
  `Event.sym` field holds the raw SDL keycode, so W and Up, or Q and Esc, can be
  told apart while `key` keeps folding them.
- `core:linalg` works on the built-in vectors: `vec2f`/`vec3f`/`vec4f`, and
  `dot`, `length`, `cross`, `normalize`, `lerp` generic over `vector[$N]$T`, plus
  `transform(m, v)`. The `Vec2`/`Vec3`/`Vec4` structs and their per-width
  functions are deprecated (removed in 1.0); calls now warn and name the
  replacement.
- `to_float` accepts a `float` inside a generic body, so a numeric generic can
  convert `$T` whether it is an int, f32 or float. Outside a generic the no-op
  conversion is still refused.
- `core:gfx` windows are resizable, and the new event kind 9 reports the mouse
  wheel in whole steps (`y > 0` is scrolled away from the user, whatever the
  system's natural-scrolling setting).

### Breaking

- **`core:gfx.Event` has a sixth field, `sym`.** A positional
  `gfx.Event(kind, key, x, y, button)` needs a sixth argument.
- **`core:gfx` mouse and resize kinds split.** Kind 4 no longer fires on a
  button release (that is kind 7), and kind 5 no longer fires on a pixel-size
  change (that is kind 8). A program that re-reads the window size on resize
  should check `ev.kind == 5 or ev.kind == 8`, as the gfx examples now do.

### Compiler

- Clearer diagnostics for four habits from other languages: calling a keyword
  (`select(x)`), `return` in a lambda body, a lambda body on its own line, and
  Python's inline `a if c else b` (which names the value `if` instead).

### Performance

- **A loop that calls a closure resets its scratch arena with one load** instead of four
  (`sort_by` ~6% faster).
- **`TYCHO_ARENA_STATS` no longer serialises threads**: counting is per thread, published at
  task end, so a `parallel for` with stats on is no longer up to 80x slower (2029 ms -> 65 ms on
  a small-allocation loop over 15 threads). Totals are unchanged; a multi-threaded peak can be
  off by under 4 KiB per thread.
- **`sort.asc` / `sort.desc` are 2x faster** (21.7M ints: 2.9 s -> 1.46 s, now faster than
  `sort_by` and C `qsort`): they merge the values directly instead of an index permutation.
  Every sort's merge passes alternate buffers instead of copying back (`sort_by` 9% faster).
  Results are unchanged.
- A fresh `to_bytes` result is no longer copied on return for a `[int]` or
  packed argument, and `core:buffer.write` / `write_bytes` append a whole
  `bytes` at once (flat-state save/load roughly 2x faster).
- Every `core:sort` sort and `arrays.sort` is now a stable O(n log n) merge;
  all but `sort_by` were insertion sorts. The order of the results is
  unchanged. Sorting 300k keys went from 21 s to well under a second.
- `arrays.fill(n, v)` allocates once: a 40M-element fill peaked at 1.3 GiB and
  now peaks at the 305 MiB it returns.

### Fixed

- **`push(xs, b)` (`xs: [int]`, `b: bytes`) inside a loop kept nothing.** Loops
  cache a pushed-to array's length and storage in locals, and the bulk append
  wrote around that cache, so the loop's write-back erased every appended
  element, silently. The append now flushes and reloads the cache.
- **`core:buffer` stored one byte per `int`** (8x the memory, and a
  byte-at-a-time conversion on read). It now holds `[u8]`: assembling 280 MB
  takes 0.18 s instead of 1.8 s.

## [0.9.2] — 2026-11-12

### Compiler

- `read_file` warns that it is fail-open and points at `core:io.read_text`.
- An unused match payload binding (`Err(e)` / `Ok(v)`) warns; write `_` to
  discard it.
- Named struct construction supplies the field type to each argument, so an
  empty literal infers from the field (`Graph(edges: [], ...)`).

### Corelib

- New packages: `core:container` (`Deque`, `Queue`, `PriorityQueue`,
  `BitArray`, LRU cache, `Treap`, `topo_sort`, AVL and red-black ordered maps,
  `HandleMap`, `SmallArray`, `Xar`), `core:sync` (atomics, `Mutex`, `RwLock`,
  `Cond`, `Semaphore`, `WaitGroup`, `Once`, `Barrier`, recursive mutex),
  `core:textmatch` (Lua patterns), `core:hxa`, and `core:base32`,
  `core:varint`, `core:endian`, `core:pem`, `core:ini`, `core:utf16`,
  `core:edit`, `core:xml`, `core:cbor`, `core:asn1`, `core:diff`,
  `core:terminal`, `core:lz4`, `core:bufio`, `core:bits`, `core:ease`,
  `core:linalg`, `core:fixed`, `core:complex`, `core:noise`, `core:glob`,
  `core:table`, `core:scanner`, `core:unicode`, `core:x509`, `core:dynlib`,
  `core:nbio`, `core:buffer`, `core:debug`, `core:filepath`, `core:prof`,
  `core:thread`, `core:fmt`, `core:entity`.
- `core:os`: environment, cwd/chdir, pid/ppid, hostname, home/temp and
  cache/config/data dirs, `chmod`/`umask`, uid/gid. `io.walk` (sorted,
  fail-closed); `path.relative`; `strings.fields`.
- `core:net`: DNS resolution, response parsing and UDP/TCP querying
  (`parse_dns`, `dns_query`, `query_mx`/`txt`/`srv`, TCP fallback), socket
  options.
- `core:crypto`: SHA-3/SHAKE, BLAKE2, HMAC-SHA512, HKDF-SHA256, AES-GCM,
  AES-CTR/CBC, RSA PKCS#1 v1.5, ECDSA P-256, ML-DSA, ML-KEM, Argon2id, X.509
  parsing and verification.
- `core:compress`: runtime-loaded bzip2, delta/BCJ/BCJ2/swap filters, PPMd7/8,
  PKZIP Shrink/Reduce/Implode, Deflate64, shoco.
- `core:image`: baseline JPEG and BNG; `core:raster`: TGA and Netpbm.
- `core:tz` parses the POSIX TZ footer (DST after the last transition) and
  exposes leap seconds (`Zone.leaps`, `leap_correction_at`, …).
- `core:nbio.CompletionLoop` (epoll/kqueue/IOCP); `core:prof` sampling and
  Spall traces; `core:terminal` cursor queries; `core:text` Qt `.qm` catalogs
  and locale data; `core:thread` thread-local slots and `run(f)`;
  `core:dynlib` typed function-pointer calls and C-to-Tycho callbacks
  (scalars, `ptr` and `void` only); `core:fmath` trig/exp/log.
- The dependency-free corelib builds and runs on Android (NDK, x86_64 and
  aarch64) and the iOS Simulator.

## [0.9.1] — 2026-11-10

### Compiler

- Optimized user-program builds use LTO: `hello.ty` builds in ~0.5 s instead
  of ~1.6 s. `-O0 -g` builds are unchanged.
- `tycho-debug bt` shows `pkg.name` symbols and `.ty` file:line, and tags
  runtime and native frames.
- A program with no lambda and no extern uses 8-byte function values instead of
  the 24-byte closure descriptor.
- The archived C compiler is removed from the tree (it remains in git history
  at `c7d46528`).

### Performance

- String byte indexing and short string equality are inlined.
- Recursive two-variant enums (`Leaf | Node(Tree, Tree)`) use NULL for the empty
  variant: 16-byte nodes, no tag.
- Maps compact at 1/8 dead entries instead of 1/2, so churn-heavy maps stop
  growing.

### Runtime

- String-keyed map insertion is race-free across tasks.

### Corelib

- Checked siblings for the remaining sentinel APIs: `io.write_text`,
  `io.write_lines_checked`, `io.flush_stdout_checked`,
  `net.udp_{bind,send,read}_checked`, `path.safe_join_checked`,
  `path.resolve_under_checked`.
- `io.flush_stdout()`; `io.read_link`; `io.read_stdin_bytes` (EOF distinct from
  failure); `net.set_write_timeout_ms`; `os.exec_capture(argv)`;
  `strings.parse_u64`.
- Fixed: `io.open_lines` on a directory returns `Err(IsDir)`; `core:json`
  rejects `[]` followed by trailing text; `io.read_stdin_bytes` and
  `io.write_at` retry `EINTR`; `http.get` returns a null handle on allocation
  failure instead of dereferencing NULL.

## [0.9.0] — 2026-10-26

### Breaking

- **`core:json` object representation changed.** `JObj([string], [Json])` is
  now `JObj([JsonField])`, with `JsonField` holding `k: string` and `v: Json`.
  Code that constructs or matches `JObj` directly must be updated.

### Compiler and performance

- Scalar-payload enums are emitted inline (tag plus union) instead of as a
  pointer to an arena cell. `TYCHOC_NO_INLINE_ENUM=1` restores the old layout.
- Small closure environments are inlined; counted builders (`for i := 0; i <
  n`) and canonical counted loops are presized and bounds-proved.
- Faster map churn (borrowed read-only `m.get`, inline integer `push`,
  permutation hash for integer keys; public `hash()` is still SplitMix64) and
  flatter JSON and map layouts.
- `sha256` hashing uses SHA-NI when the CPU has it; `sha256.hex_bytes` added.

## [0.8.9] — 2026-10-21

### 3D and GPU

- **`core:gfx3d`**: a software camera and flat-shaded triangle rasterizer
  (`Camera`, `Mesh`, `look_at`, `project`, `draw_tri`, `draw_mesh`, …) with
  near-plane clipping.
- **`core:gfxgpu`** (experimental): an SDL_GPU backend for Vulkan and Metal,
  with the software renderer as fallback. Building GPU code needs SDL_shadercross
  (`scripts/build-shadercross.sh`).

### Fixed

- Software-3D depth interpolation and near-plane clipping.
- `core:image` no longer leaks a per-decode header.

## [0.8.8] — 2026-10-21

### Breaking

- **`strings.parse_int` is the strict parser.** It returns
  `Result(int, IntErr)` and refuses empty input, trailing garbage and
  out-of-range values. The fail-open prefix scanner is now
  `strings.parse_int_prefix`; `parse_int_checked` is gone.
- **`eprintln(s)` is a builtin** (stderr plus a newline).
- **`path.real` is implemented through `real_checked`**; a failure is available
  as a named `PathErr`.

### Core library

- `core:io`: `FileKind`, `Stat`, `stat`, `lstat`, `same_file`.
- `core:path`: `real_checked` (`NotFound` / `Denied` / `TooLong` / `BadPath` /
  `Failed`) and `quote` (POSIX shell-safe quoting).
- `core:gfx`: windowing, events, textures and asset manifests, text, fixed-step
  `Clock`, sprites/animation, a PCM `Mixer` and looping audio over SDL, with an
  auto-linked shim. `examples/gfx` (Snake, Pong) uses it.

### FFI

- A non-packed struct of numeric scalar fields may cross by value, as a return,
  as an `inout` out-parameter, and as `[POD]` (borrowed as `(const S *, len)`).
  Non-POD and packed structs stay refused; use `--shim`.

### Fixed

- Scalar maps are no longer marked shared on a read inside an `inout` helper
  (the next write copied the whole map; `bench/lru` exhausted memory).
- Escape analysis sees a newtype wrap (`return Env(m)`), fixing a
  use-after-free when a local map or array was returned through a newtype.

### Compiler optimizations

- Immortal string literals: copying a literal costs nothing.
- Last-use moves into `push`, borrowed string slices as call arguments,
  destination passing for `t = f(t, ...)`, copy-on-write sharing for composite
  maps, and presizing of loop-grown arrays and map literals. Persistent
  tree/rope/trie updates went from gigabytes to megabytes on the
  `bench/opt` workloads.

## [0.8.7] — 2026-10-18

### Breaking

Each item makes a program 0.8.6 accepted stop compiling, with a diagnostic
naming the rule.

- **An out-of-range literal is refused wherever it adapts** — `x + 300` at a
  `u8`, a variadic or UFCS argument, a lambda body, an `m.get` default, a
  subscript assignment, a `match` arm, a `send`, a sized-int array broadcast:
  `300 does not fit u8 (its range is 0..255)`.
- **An empty literal `[]` is refused for a `[N]T` or `vector[N]T`.**
- **A `[N]T` with a const `N` keeps its length**: a wrong-arity literal, a
  `push` onto it, a zero or negative `N` and a mismatched fixed-array argument
  are refused.
- **An f-string hole is one expression**: `f"{price:.2f}"` and `f"{n, 99}"` are
  refused. A space after `{` is accepted.
- **The identifier prefix `Ztycho_` is reserved** for compiler temporaries.
- **A handle `free:` destructor that returns an aggregate is refused.**
- **The CLI refuses `-o ""` and a trailing bare `-L` or `-I`.**

### Language

- Negative literals adapt to `f32`, sized ints and vector elements like
  positive ones.
- `to_u8`..`to_i64` of a float are total (NaN, infinity and out-of-range wrap
  as specified); a `u64` shift count of 2^63 or more gives zero.
- A generic enum variant can be constructed through a package qualifier
  (`box.Nothing$(int)`).

### Compiler

- Division by zero on sized integers and element-wise array arithmetic traps as
  `tycho: division by zero` instead of SIGFPE.
- Long `if`/`elif` chains, `+` chains and many-hole f-strings emit at constant
  depth; nested generic calls and constructors no longer compile in exponential
  time; constant array literals are static tables.
- Enum variants named like C keywords (`EOF`, `NULL`, `default`) build.
- A `packed` field passed `inout` goes through an aligned local.
- Float-leaf composite map keys hash `+0.0` and `-0.0` alike.
- `pow(x, y)` and `for x in s` over an `soa` compile.
- `-g` escapes special characters in `#line` paths.
- An in-place string append (`acc = acc + e`) stays linear through an
  `inout string` helper and in `bytes` and match-arm accumulators.
- `recv(ch)` used as an expression no longer returns a payload from a freed
  scope.
- The builtin-collision warning describes the real dispatch: inside a library
  package an unqualified call selects the package's own function; in
  `package main` the builtin wins.
- Clearer diagnostics for a one-line value `if` and for `\"` inside an
  f-string hole.
- The shipped `tychoc` names itself `tychoc`, reports `cannot open <path>` for a
  missing source, and `--emit-c` with no `-o` writes C to stdout.

### Runtime

- Overwriting a heap value in a long-lived map reclaims the old value.
- `reserve(xs, n)` and `channel(T, cap)` check size arithmetic before
  allocating and abort by name on overflow.

### Core library

- New: `io.rename`, `io.lock_exclusive`, `httpd.has_header`,
  `tls.connect_timeout`, `tls.set_timeout_ms` (previously documented but
  private), `zip.create_checked`, `zip.extract_entry`.
- Interior NULs fail closed at every remaining C-string boundary: the whole
  `core:crypto` hex surface (verify, HMAC and AEAD decrypt accepted unsigned
  input), `sqlite.open`/`exec`, stream lines and child-process output.
- `core:httpd` refuses any `Transfer-Encoding`, an empty `Content-Length`,
  whitespace before a header colon, obs-fold and extra request-line tokens;
  `BodyTooLarge` (413) is separate from an oversized header (431).
- `core:http` refuses CR/LF in `content_type`.
- `core:toml` keeps parent tables declared after subtables and quoted dotted
  keys, refuses malformed literals, and bounds input size, entries and depth.
- `core:regex` caps a subject at 4,096 bytes and checks group indexes.
- `core:compress` refuses lengths of 4 GiB or more, multi-member gzip and
  trailing garbage.
- `io.copy` onto the same file is refused; `path.resolve_under` refuses a
  dangling symlink inside the root; `net.close_fd` validates the descriptor.
- `json.parse` and `markdown.inline` are no longer quadratic.
- `datetime.offset_at` answers 0 for an out-of-range TZ string on every
  platform; `core:tls` builds against OpenSSL 4.0.

### Tools

- `tycho-db` is crash-safe (atomic checkpoint, WAL-first mutation, exclusive
  `.lock`), validates stored rows, bounds idle connections, and uses a hash
  index.
- `tycho-docs` renders `docs/` as a browsable site.
- `tycho-httpd`'s `Range` limit counts significant digits.

## [0.8.6] — 2026-09-25

### Breaking

Each item makes a program 0.8.5 accepted stop compiling.

- **`or_else` is a keyword.** Rename an identifier spelled `or_else`.
- **136 internal corelib helpers became package-private** (`_`-prefixed), e.g.
  `json.parse_value`, `bignum.mag_cmp`, `csv.quote_field`. A call from outside
  says `did you mean '_mag_cmp'?`. Everything programs were found calling stayed
  public.
- **`io.open_lines` returns `Result(LineReader, IoErr)`**, and `io.read_line` /
  `io.close_lines` take a `LineReader`. `LineReader` is a plain struct, not a
  handle: close exactly one copy, exactly once. Migration — the null check
  becomes the `Err` arm:

  ```text
  # before (no longer compiles)
  r := io.open_lines(p)
  if is_null(r):
      return

  # after
  match io.open_lines(p):
      Err(e):
          return
      Ok(r):
          # ... io.read_line(r) ...
          io.close_lines(r)
  ```

- **`reserve` and `pop` on a `[N]T` or `vector[N]T` are refused.**
- **Four handle misuses are compile errors**: a use after a `close(h)` that runs
  on every path; `close(h)` on a handle parameter; an opener whose result is not
  bound directly to a variable; a `free:` that does not name an `extern fn`
  taking that handle type.
- **A dead import is reported per file**: an import only a sibling file uses is
  unused.

### Language

- **`or_else`**: `x := f() or_else e: h` unwraps `Ok`/`Some`; on `Err(e)` it
  returns `h` from the enclosing function (spec §14.6.1).
- `size_of$(T)` accepts an `align(N)` struct; the result is a multiple of `N`.
- `math.min`, `math.max` and `math.clamp` work lane-wise on `vector[N]T`.
- Floating point is no longer contracted into FMA (`-ffp-contract=off`), so
  aarch64 and x86-64 results agree.

### Compiler

- A generic instantiation error names the call chain from the outermost call.
- More diagnostics state their rule; a lex error in an f-string hole names its
  file and line.
- The over-wide vector lint no longer gives x86-64 advice on ARM.
- Fixed: `x[a:b]` on a struct or enum crashed in emit; an `or_return`/`or_else`
  payload returned to the caller was read from freed memory; `to_bytes(local)`
  inside a returned value dangled; a local `bounded[N]T` grew past capacity;
  indentation depth is capped at 256; pkg-config flags with spaces survive.

### Core library

- `net.listen` uses `SOMAXCONN` (macOS servers refused bursts at backlog 16).
- A server worker no longer blocks in `accept()` past SIGTERM.
- Subnormal float literals are correct on aarch64 Windows.
- `core:os` refuses to spawn a `.bat` or `.cmd` on Windows.

### Security

- `core:tls` refuses an interior NUL in `host`.

### Fixed

- `tycho-sim` builds the same world from a seed under gcc and clang.
- `tycho-make` reads a value containing `:` and refuses unimplemented makefile
  features by name.
- Building on Fedora with gcc 16 links (`-static-pie` needs `-fPIE`).
- `tycho-debug` treats `C:` and `c:` alike.

### Release and platforms

- A `darwin-arm64` release archive.
- `UNTESTED-PLATFORM.txt` in cross-built archives reflects measured verdicts.

### Tools

- `tycho-du` (disk usage over a typed `handle`) and `tycho-grade` (TGA/BMP/QOI
  colour grader).

## [0.8.5] — 2026-09-06

### Language

- **`vector[N]T`**: a fixed array whose arithmetic is one machine instruction;
  `N` a power of two from 2 to 64, elements `int`, `float` or `f32`. At a width
  the target lacks it is slower than scalar code, and the compiler warns at the
  declaration with the `--target` level that fixes it.
- **`packed struct`**: byte-exact C layout. `to_bytes(v)` / `from_bytes$(T)(b)`
  convert to and from exactly `size_of$(T)` little-endian bytes.
- **`align(N)`**: `N` a power of two from 1 to 8; larger is refused.
- **Simultaneous assignment** `(x, y) = (y, x)`: every right-hand side and every
  target index is evaluated before any target is written.
- **Swizzling** `v.(x, y)` reads, binds and assigns several components as a
  tuple.
- **Lane names** `.x .y .z .w` / `.r .g .b .a` on `vector[N]T`, `[N]T` and
  `bounded[N]T`; `v.x` is `v[0]`. A struct field of the same name wins.

### Compiler

- `--target <level>` (`x86-64-v2`, `-v3`, `-v4`, `baseline`) raises the ISA
  baseline; without it the build is unchanged.
- `vector[N]T` lowers to machine vectors in the shipped compiler (it had been
  an arena-backed array).
- The shipped compiler refuses about 270 shapes the language does not allow
  that it had accepted.
- Map keys follow the spec: `vector[N]T`, `[N]int`, `bounded[N]int`, `[float]`
  and tuple keys are accepted; a struct with a fieldless-enum field is refused.
- Float-to-integer conversions are total; an out-of-range sized-integer literal
  is refused instead of truncated.

### Core library

- A double free or use after free of a `core:crypto`, `core:tls`, `core:http`
  or `core:image` handle exits 1 by name instead of segfaulting.
- New: `net.wait_readable(fds, ms)`, `path.real`, `path.under`,
  `path.resolve_under`, `io.list_checked`, `http.post_bytes`,
  `tls.connect_timeout`, `tls.set_timeout_ms`, `sqlite.query_null`,
  `sqlite.query_null_params`, `httpd.read_request_resume`.
- `core:csv`, `core:json`, `core:toml`, `core:cli` and `core:markdown` fail
  closed; TOML validates dates and times. `core:bignum`, `core:decimal`,
  `core:pool` and `core:raster` refuse their numeric edge cases.
- **Removed**: undocumented byte helpers in `core:zip` and `core:raster`
  (`zip.le16`, `raster.rd_u32_le` and siblings), replaced by packed structs.

### Security

- `core:http` refuses `file://` URLs; `core:zip` has a decompression-bomb
  ceiling; `core:path` closes backslash traversal. `tycho-fetch --local`
  covers local files.
- `tycho-httpd` refuses ambiguous request framing (both `Content-Length` and
  `Transfer-Encoding`, or two lengths).
- The example server refuses symlink escapes and bounds idle connections.
- Paths passed to `system()` (the `cc` link line, the `tycho` driver, the LSP
  server) are shell-quoted.
- **Removed**: `tycho-rsa`, which cannot be constant-time. Use `core:crypto`.

### Fixed

- The Windows archive ships the self-hosted compiler, and its executables are
  linked statically so they start (the published v0.8.0 Windows archive does
  not run).
- `tychoc.exe` finds corelib when run from another directory.
- The example server no longer starves under slow or idle peers.
- Building from source bootstraps stage 1 with the C compiler.

## [0.8.0] — 2026-09-02

Not a breaking release: every program v0.7.0 compiled, 0.8.0 compiles.

### Compiler

- **The shipped compiler is written in Tycho** (`tychoc1`), built in two stages
  from the C bootstrap. It compiles its own source faster than the bootstrap.
- The v0.8.0 Windows archive does not run (missing `libwinpthread-1.dll`);
  fixed in 0.8.5.
- Array-literal codegen is linear (30,000 elements: 60 s to 20 ms).

### Language

- Now compile: a generic function returning an array under `where`, a generic
  constructor taking a bare `[]`, a lambda argument at a generic call site.
- The surface is frozen and gated by `surface.lock`: corelib may gain functions
  but not lose one or change a signature.

### Core library

- `io.make_dir_all`, `io.append_text`, `io.copy`, `strings.format_g17`.
- `decimal.from_str` is deprecated (it fails open: `"1.5x"` is `0.15`); use
  `from_str_checked`.

### Tools

- `tycho-diff`, `tycho-fold`, `tycho-hash`.

## [0.7.0] — 2026-08-15

### Language — breaking

- **A handle cannot be copied** (`g := f`). Pass it as an argument (a borrow)
  or bind the opener directly.
- **A bare handle cannot be a struct field.**
- **`sink` and `inout` are refused on a `handle`, `Task(T)` or `Channel(T)`
  parameter.** Pass it plainly.
- **A channel cannot be copied** (`e := c`); a channel-typed declaration must
  come from `channel(...)`.
- **A `deprecated:` marker must open the comment.**
- **Affine rules hold through a generic**: an affine type as a generic struct
  field, enum payload, `sink`/`inout $T` parameter or generic return is refused.
- **`bounded[N]T` in a generic struct stays bounded**; a plain `[N]T` is
  refused for it.

### Core library — breaking

- **`io.write_bytes`, `io.write_at`, `io.set_mtime` and `io.sync` return
  `Result(void, IoErr)`** instead of `Result(bool, IoErr)`. Drop the binding and
  write `Ok()` for `Ok(x)`. `io.is_dir`, `io.make_dir` and `io.remove` are
  unchanged.
- **New `JsonErr.TooDeep` variant**: `json.parse_checked` refuses nesting past
  2000 instead of overflowing the stack. An exhaustive `match` on `JsonErr`
  needs the new arm.

### Language — added and fixed

- Explicit type arguments on a qualified name: `vp.ident$(int)(5)`.
- An empty generic variadic call may name its type: `count$(int)()`.
- A variadic called through a package qualifier packs its arguments.
- A generic struct with a map-of-function field compiles.
- A `deprecated:` marker no longer leaks to a function at the same line in
  another file; taking a deprecated function as a value warns.

### Diagnostics

- Imported types and callees are named as written (`pool.Handle`, not
  `pool__Handle`).
- `for k in m` over a map points at `keys(m)`.
- An unimported qualifier says to import it; `eprintln` is suggested for
  stderr; `main` returning a value names `exit(code)`.

### Core library

- `decimal.from_str_checked(s) -> Result(Decimal, DecErr)`.
- `core:crypto` wipes plaintext and key buffers before freeing them, and key
  import is constant-time.
- `core:http` honours `SSL_CERT_FILE` / `SSL_CERT_DIR` (redirects trust, cannot
  disable verification).
- Fixed: `math.sign(±inf)` returns ±1; `fmath.round` rounds half away from zero
  correctly; `fmath.lerp` returns its endpoints exactly;
  `strings.pad_left`/`pad_right` no longer overshoot with a multi-byte pad;
  `sqlite.exec` reports the rows every statement changed; `toml.parse` refuses
  a repeated `[table]` header.
- Interior NULs no longer truncate `sqlite` bound parameters,
  `crypto.pbkdf2_sha256` passwords, `crypto.ct_equal`, `http` URLs or
  `datetime.offset_at` TZ strings. `exec_params`/`query_params` run only the
  first statement.

### Tools

- `tycho-make` refuses a damaged stamp mtime; `tycho-db` refuses an integer
  literal out of range.

## [0.6.0] — 2026-08-11

### Breaking

- **A binding may not start with an uppercase letter** (locals, parameters,
  destructuring, loop and `match`/`select` bindings). `const` is exempt.
- **`is` is a reserved keyword.**
- **A function may not be named for a constructor, enum variant or const**
  (`fn Ok(...)`).
- **`core:iter` predicates return `bool`**: `filter`, `try_filter`, `count`,
  `any` take `fn($T) -> bool`.
- **`image.decode` / `image.encode` return `Result(_, ImgErr)`** (`Empty`,
  `NotPng`, `Corrupt`, `BadDims`, `ShortPixels`, `Failed`).
- **`compress.decompress` and `raw_decompress` return `Result(bytes, ZErr)`**
  (`Corrupt`, `Truncated`, `Failed`).
- **`tycho-ar x` exits 1 when an mtime could not be restored**, after finishing
  the extraction.

### Language

- **`x is Variant`** tests a variant without destructuring.
- A `[string]` may be an extern parameter, arriving as
  `(const char *const *, long)`.
- f-string holes evaluate left to right.
- `pkg.NAME` reads another package's `const`, folded at the use site.
- A newtype over a collection supports that collection's operations; it stays
  distinct at assignment, passing and comparison.
- `fn main() -> Result(void, string)`: `or_return` works in `main`; an `Err`
  prints its message to stderr and exits 1.

### Core library

- `io.mtime`, `io.set_mtime`, `io.read_text`.
- `strings.slice_bytes` / `strings.slice_str` (fail-closed slicing).
- `iter.try_map`, `iter.try_filter`; `result.map_err_with`.
- `decimal.div(a, b, scale, mode) -> Result(Decimal, DivErr)`.
- `core:result` combinators work at `Result(void, E)`.

### Diagnostics

- An error inside a generic adds `note: required from here` at the call site
  and names the template's own file.
- A non-exhaustive `Result`/`Option` match names the missing variant.
- An uppercase binding names the rule; `[string]` written as a value names
  `[]string`; the live-copy warning offers only the remedy that works.

### Fixed

- A compiler use-after-free that rejected valid programs with a bogus
  diagnostic during generic instantiation.

## [0.5.0] — 2026-08-10

The first tagged release.

- **Deprecated: `sort.by_key(xs, key)`** — use `sort.sort_by(xs, cmp)`. Calling
  it warns; it is removed in 1.0. A `# deprecated: <text>` line directly above a
  `fn` marks any function this way.
- `sort.sort_by(xs, cmp)`: stable, three-way compare.
- `pass`, the no-op statement (contextual, not reserved).
- `Result(void, E)` with `Ok()`, a bare `Ok:` arm, and `f() or_return` as a
  statement.
- `tychofmt` keeps `-1` as `-1`.
- New packages: `core:testing`, `core:toml`, `core:log`, `core:sqlite`,
  `core:utf8`, `core:zip`; raw deflate in `core:compress`; `http.body_bytes`.
- `tycho-fetch <url> <name>` vendors a package tarball with sha256
  verification.
- `tycho debug <file.ty>`: a gdb adapter with source-line breakpoints and
  Tycho-named locals.
- `tychoc --version`.
- `core:net` has no readiness polling, so a server's worker count bounds its
  concurrent connections; a Windows server parked in `recv` winds down within
  its idle timeout.
- Pre-1.0: the language surface is not a stability contract.

## [0.1.0] — earlier

The research-prototype release. See `RELEASE_NOTES.md` at the `v0.1.0` tag.
