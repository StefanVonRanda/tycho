# corelib — Tycho's core library

> **Memory:** Every corelib function puts its results in the caller's memory and
> returns independent values, never aliasing the inputs. The one exception is a
> C-owned opaque handle (`ptr`) from the C-backed packages below, which is not
> caller-managed: release it with that package's own free function.

corelib is Tycho's core library: a set of packages under `corelib/`, imported with the
`core:` collection root. The modules below are usable today.

```tycho
package main

import "core:math"

fn main():
    println(f"gcd={math.gcd(12, 18)}")
```

## Quick start

1. `import "core:<pkg>"` in your source. The name `<pkg>` is bound to the package.
   The transpiler finds `corelib/` next to its own binary by default, so no setup is
   needed in this repo. Set `TYCHO_CORELIB` to a corelib directory to override.
2. Call its functions either free-standing (`math.gcd(12, 18)`) or method-style
   (`x.abs()` — any function can be called with its first argument as the
   receiver, see [Methods](functions.md#methods-ufcs)).

## Resolution

`import "core:<pkg>"` resolves to `<corelib>/<pkg>` and binds the name `<pkg>`, where
`<corelib>` is `TYCHO_CORELIB` if set, otherwise `corelib/` next to the transpiler binary.
Non-`core:` imports stay relative to the importing package, unchanged. `tychoc` resolves `core:` natively, with no
bundling step required.

## How corelib is shaped

corelib is **generic free functions over a type parameter `$T`**, with `where`
constraints (`comparable(T)`, `numeric(T)`, `has_str(T)`) picking which element
types each function accepts. Array and iterator utilities are single generic
packages over any element type `[$T]` (`core:arrays`, `core:iter`) rather than
one package per element type. The functions are also callable method-style
through method-style call, so `index_of(xs, v)` and `xs.index_of(v)` are the same call.

Higher-order helpers (map / filter / reduce that take a function) live in `core:iter` and
its siblings, and take a first-class `fn`/closure argument. Lambdas in Tycho are
expression-bodied; pass a named function where you need a multi-line body.

## Packages

- **`math`** — `min`/`max`/`clamp(x, lo, hi)` are generic over any comparable type
  (int/float/string/char) and lane-wise at a `vector[N]T`; `sign(x)` over any numeric
  type (returns an int −1/0/1); `abs`, `gcd`, `ipow(base, exp)` (exp ≥ 0) are integer-
  specific. (`sqrt`/`pow`/`floor`/`fabs` are float builtins.)
- **`fmath`** — float-only helpers (scalar `min`/`max`/`clamp`/`sign` live in `math`):
  `pi`, `e`, `round` (half away from zero), `trunc`, `lerp(a, b, t)`,
  `approx_eq(a, b, eps)`. (No trig — the language has no libm sin/cos builtin.)
- **`gfx`** — an SDL3-backed window, 2D renderer and stream-audio shim
  (`deps: sdl3`). Handles `Window`, `Audio` and `TexSet`; types `Event`, `FRect`,
  `IRect`, `FVec`, `View`, `AssetSpec`, `Controls`, `Clock`, `Sprite`, `Anim`,
  `Mixer`. `begin`, `draw_fb`, `draw_lines`, `present`, `poll_event`,
  `set_title`, `window_width`, `window_height`, `fit_view`, `view_rect`,
  `view_fvec`, `default_controls`, `action`, `clock_start`, `clock_advance`,
  `clock_tick`, `tex_create`, `tex_draw`, `draw_sprite`, `anim_frame`,
  `draw_anim`, `audio_queue`, `mixer`, `add`, `finish`, `play_mix`, `repeat`,
  `play_loop`, `load_manifest`, `load_texture`, `draw_atlas_text`,
  `draw_text_view`, `draw_text_centered`, `draw_text_centered_view`,
  `text_width`, `fill_rect`, `rgb`, `make_beep`, `play_beep`, `save_bmp`.
  `draw_fb` accepts a framebuffer whose width/height may be smaller than the
  window; the shim uploads it at source resolution and scales it into `dst`
  (the software-3D example renders at 480×270 and scales to the window). Event
  `kind` values are: 0 none, 1 quit, 2 key down, 3 mouse move, 4 mouse button,
  5 resize, 6 key up.
  A `TexSet` owns its textures and is
  scope-freed; SDL3 must be
  installed and selected with `--pkg sdl3`. The boundary rule is recorded in
  [design-gfx](../internals/design-gfx.md): values and affine handles only, no
  public `ptr`, callbacks or unions.
- **`gfx3d`** — a pure software 3D camera, mesh and flat-shaded triangle
  rasterizer. Types `Vec3`, `Camera`, `Target`, `SVert`, `Tri`, `Mesh`,
  `Place`; functions `look_at`, `project`, `draw_tri`, `draw_quad`, `draw_box`,
  `clear`, `mesh`, `add_tri`, `add_quad`, `add_box`, `draw_mesh`. There is no
  SDL, handle or pointer in this package: the caller owns the framebuffer,
  depth buffer, target dimensions and ARGB colours. `draw_tri` clips against the
  near plane and interpolates inverse depth with the corrected barycentric
  weights. `Mesh` holds local-space triangles; `Place` applies a caller-built
  right/up/forward basis, origin and uniform scale without copying the mesh.
  `examples/gfx/3d`, `examples/gfx/orbit` and `examples/gfx/rotor` are its
  callers.
- **`gfxgpu`** — the first SDL_GPU backend. It owns a GPU device, swapchain,
  depth texture and graphics pipeline for a `gfx.Window`; the Tycho side is the
  affine `Gpu` handle plus an external frame call. Shaders are committed SPIR-V
  and are translated to the native backend by SDL_shadercross. Build the pinned
  runtime with `scripts/build-shadercross.sh`; this package is not available on
  a host without SDL_shadercross. The public function is `gfxgpu_close`; the
  handle-returning open and frame entry points are declared by the caller, the
  same affine-opener pattern as `core:gfx`. The probe draws a depth-tested
  triangle; the offscreen mesh path renders Rotor Run's scene into a GPU color
  target and hands the pixels back to `core:gfx` for its HUD/input/audio.
- **`char`** — byte/char classification & conversion over int byte values (what
  `s[i]`/`chr` use): `is_digit`, `is_alpha`, `is_alnum`, `is_upper`, `is_lower`,
  `is_space`, `is_hex`, `to_upper`/`to_lower` (one byte), `digit_val` (−1 if not a
  digit), `hex_val` (0..15 or −1).
- **`strings`** — `to_upper`, `to_lower`, `starts_with`, `ends_with`, `contains`,
  `repeat(s, n)`, `trim` (ASCII whitespace), `parse_int`, `parse_int_prefix`,
  `is_space`, `lines` (splits on `\n`, drops one trailing `\r` per line, trailing
  newline adds no empty line), `replace`
  (non-overlapping, left to right; empty `old` returns the input unchanged), `count(s, sub)`,
  `strip_prefix`/`strip_suffix`, `pad_left`/`pad_right(s, width, pad)` (pad is one byte),
  `reverse`, `capitalize`, `split_once(s, sep) -> (before, after)` (`(s, "")` if absent).
  (`split`/`find`/`substr`/`len`/`chr` are builtins.)
  **The integer parser is fail-closed by default:**
  `parse_int(s) -> Result(int, IntErr)` and
  `parse_float(s) -> Result(float, FloatErr)` refuse an empty string, trailing
  garbage and out-of-range input. `parse_int_prefix(s) -> int` is the explicit
  fail-open scanner (`"3x"` is `3`), for grammars that consume a numeric prefix;
  do not use it on a file, a socket or a user. `slice_str(s, start, stop) -> Result(string, SliceErr)`
  and `slice_bytes(b, start, stop)` — a plain slice **clamps** rather than
  bounds-checking (`b[1:99]` on three bytes gives two and no trap), so these are
  the forms that refuse an out-of-range window instead of narrowing it.
  `format_g17(v)` renders a float at the 17 significant digits that round-trip.
- **`path`** — POSIX path utilities (separator `/`). The pure functions below are
  string math only (no filesystem access, every function returns a fresh value):
  `base` (final element, trailing slashes ignored; `""`→`.`, `"/"`→`/`), `dir`
  (all but the final element), `ext` (extension incl. the dot, `""` if none; a
  leading dot is a dotfile, not an ext), `stem` (base minus ext), `join(a, b)`
  (exactly one separator, empty operands drop out), `is_abs`,
  `split_path(p) -> (dir, base)` (the inverse of `join`), and `clean` (lexical
  normalize: collapse `//`, drop `.`, resolve `..` but never above the root or
  past a leading `..`). `last_slash(s)` is the shared scan helper (index of
  the last `/`, or -1). `quote(p)` is POSIX shell-safe single quoting for
  display (`'a b'`, and an embedded quote as `'\''`), so a text report can print
  an arbitrary path as one unambiguous word. `safe_join(base, rel)` is the lexical containment
  prefilter: the cleaned join when an untrusted `rel` stays under `base`, `""`
  when it is absolute, escapes via `..`, or carries a backslash or an `X:` drive
  prefix.

  **The filesystem fence.** A lexical check cannot see a symlink, so four
  functions make a real syscall, and all fail closed — `""` is a refusal and
  callers must treat it as one before using the path.

  - `real_checked(p)` — the checked sibling: `Ok(resolved)` or
    `Err(NotFound / Denied / TooLong / BadPath / Failed)`. Use this when the
    caller has to report *why* a path could not be resolved.
  - `real(p)` — the absolute path with every symlink and `..` resolved, or `""`
    if the path does not exist, cannot be read, contains a NUL, or does not fit.
  - `under(root, p)` — true when `p` **is** `root` or lies underneath it. The
    comparison only, not the resolution: both arguments must already be resolved.
  - `resolve_under(base, rel)` — **the one to reach for first** when a path is
    built from untrusted input; `safe_join` is the cheap prefilter inside it. It
    returns the path to use, or `""` when `rel` is refused — refused lexically,
    or because the resolved answer lands outside `base`, or because `base` itself
    does not resolve. A path that **does not exist is not a refusal**: the longest
    existing ancestor is resolved and checked instead, so the caller reports its
    own "not found" rather than a spurious "forbidden". A dangling symlink is
    refused.

  One caution: a check followed by a separate open can race if an attacker can
  write inside the root. `resolve_under` narrows that window to an attacker with
  write access inside the root; a fully race-free form would need the open done
  inside the check.
- **`arrays`** — generic utilities over any element type `[T]`: `contains`, `index_of`
  (−1 if absent), `count`, `reverse`, `take(xs, n)`, `drop(xs, n)`, `concat(a, b)`,
  `fill(n, v)`, `dedup` (consecutive — sort first for a full dedup); `sort` (ascending),
  `is_sorted`, `min`, `max` (`where comparable` — int/char/float/string); `sum`, `product`
  (`where numeric` — int/float); `join(xs, sep)` (`where` the element converts to a string).
  All return a new array — value semantics, the input is never mutated. `min`/`max`/`sum`/
  `product` seed from `xs[0]`, so they need a non-empty array. (`push`/`pop`/`len`/`range`
  are builtins; higher-order `map`/`filter`/`reduce` live in `iter`.)
- **`iter`** — generic higher-order helpers over any `[T]`, each taking a `fn`/closure:
  `map` (two type variables — `map(xs: [$T], f: fn($T) -> $U) -> [$U]`, the only one that may change element type, so `iter.map(nums, to_str)` is a `[string]`), `filter`, `reduce`, `count`, `any`.
  Fallible stages get two siblings whose callback returns a `Result`:
  `try_map(xs, f: fn($T) -> Result($U, $E)) -> Result([$U], $E)` and
  `try_filter(xs, keep: fn($T) -> Result(bool, $E)) -> Result([$T], $E)`. Both stop at
  the **first** `Err` and return it unchanged. `reduce`/`count`/`any`
  have no fallible form — write the `for` loop with `or_return` if you need one.
- **`sort`** — `argsort(keys)` / `argsort_desc(keys)`: return the index permutation that
  orders the keys — generic over any comparable key type (int/float/string/char). The way
  to order data by a derived value: keep it in parallel arrays, argsort one, and walk every
  array through the permutation. All stable. Plus `by_key(xs, key)`: sort an array by a
  derived int key (a fn/closure) — **deprecated since 0.5.0, removed in 1.0**:
  it is `sort_by(xs, fn(a, b) -> int: key(a) - key(b))`. When the order needs more than one key, mixed
  directions, or a type with no `comparable` instance, `sort_by(xs, cmp)` takes a
  three-way compare `fn($T, $T) -> int` — bottom-up merge, stable, so equal elements
  keep their input order under a descending compare too. `asc(a, b)` and
  `desc(a, b)` are the two ready-made comparators for that parameter, so the
  common orderings need no lambda: `sort_by(xs, sort.asc)`.
- **`pool`** — a generational node pool for pointer-shaped data (graphs, trees,
  doubly-linked structures). Hold every node in one array and link by integer
  index; `pool` packages that idiom. Generic over the element type `Pool($T)`:
  `add(&p, v) -> Handle`, `get(p, h)`, `set(&p, h, v)`, `remove(&p, h)`,
  `alive(p, h)`, `count(p)`, and `live(p) -> [Handle]` (all live handles in slot
  order — the way to walk a pool). A `Handle` carries a slot index plus a
  generation; every access checks the generation, so a handle to a freed-and-
  reused slot is caught at runtime instead of silently reading the new occupant.
  Create one with `p := pool.Pool([]pool.Slot(int), []int)`. It removes the
  hand-rolled bookkeeping, not the cost: pointer-shaped storage costs more in
  this model (see [value-semantics limits](../internals/value-semantics-limits.md)).
- **`intern`** — hash-consing / interning: canonical storage of values keyed by a hashable
  key. Generic `Interner($K, $V)`: `intern(&i, k, v) -> Handle` returns the canonical handle
  for key `k` — the first sight stores `v` and mints the handle, every later sight of the
  same key returns the **same** handle and ignores its `v` — plus `get(i, h)` and
  `count(i)`. Two handles are equal iff their keys were equal at first sight, so
  interning gives a cheap identity test and one canonical copy of each logical
  object. Handles are stable for the interner's lifetime; values are never
  removed — it is a *canonicalization* table, not a cache, so an unbounded
  working set grows by design (use a plain `[K: V]` map for a bounded cache).
  Keys are any map-key type — `string`, `[int]`, a struct, a tuple. Create one
  with `i := intern.Interner([]int, []string: int)`.
- **`rand`** — deterministic xorshift32 (not cryptographic). No globals in Tycho, so the
  state is an explicit int threaded via `inout` (the `&` marks the `inout` call site,
  [Basics](../reference/basics.md#procedures)). There is no `rand.State` type — a
  function of your own that draws numbers declares the parameter as a plain int,
  `fn pick(st: inout int) -> int`. Calls: `st := rand.seed(42)`,
  `rand.next(&st)` ([1, 2³²)), `rand.below(&st, n)` ([0, n)), `rand.shuffle(&st, xs)`
  (Fisher-Yates, returns a new array).
- **`time`** — wraps the `clock()` (monotonic ns) and `now()` (UNIX seconds) builtins.
  Stopwatch (value-semantic, no inout — a reading is just an int): `sw := time.start()`,
  then `time.elapsed_ns(sw)` / `elapsed_us` / `elapsed_ms`. Duration conversions
  `ns_to_us` / `ns_to_ms` / `ns_to_s`. Wall clock `unix_secs()` (named so, not `now`,
  to avoid shadowing the builtin and recursing). Blocking sleep `sleep_ms(ms)` /
  `sleep_ns(ns)` (a libc-only shim over `nanosleep`): a non-positive duration returns
  immediately, the sleep is *not* interruptible (it retries with the remaining
  time, so the full duration always elapses), and it parks the **calling task only**.
- **`datetime`** — civil (proleptic Gregorian) calendar math over UNIX timestamps, all
  pure integer arithmetic. A `DateTime` struct
  (`year`/`month`/`day`/`hour`/`minute`/`second`/`weekday`, all UTC). `from_unix(secs)`
  and its exact inverse `to_unix(dt)`; `days_from_civil`/`civil_from_days`/
  `weekday_from_days` (the day-count core); `weekday(y,m,d)` (0=Sun..6=Sat), `is_leap`,
  `days_in_month`; `now_utc()` (the only non-pure fn — reads `now()`); formatting
  `format_iso` (`YYYY-MM-DDTHH:MM:SS`), `weekday_name`, `month_name`, `pad2`/`pad4`; and its
  inverse **parsing** `parse_iso` (wall-clock fields; `T` or space separator, trailing zone
  ignored) / `parse_iso_tz` (`+HH:MM` / `-HH:MM` / `Z` folded to the UTC instant), plus
  `parse_clf` / `parse_clf_tz` for the Common Log Format stamp (`dd/Mon/yyyy:HH:MM:SS ±HHMM`,
  as in Apache/nginx access logs) and `month_num` (`"Jan"`..`"Dec"` → 1..12), all
  fail-closed via a `year = -1` sentinel that `ok(dt)` checks. The core is UTC; timezone support is layered on — **fixed offsets** (`from_unix_at`,
  `to_unix_at`, `format_iso_tz`) in pure Tycho, plus DST-aware **system/zone** offsets via a
  small libc shim (`local_offset`, `offset_at`, `now_local`). There is no IANA tz database.
  `format_offset(offset_secs)` renders a UTC offset as `+HH:MM` / `-HH:MM`.
- **`regex`** — POSIX extended regular expressions (ERE) over libc. `compile(pat) ->
  ptr` (opaque handle; `ok`/`is_null` to check), `is_match`, `find` / `find_end`
  (offset or −1), `matched` (first match substring). **Capture groups** of the first
  match by index (0 = whole match): `ngroups`, `group_start` / `group_end` (offset
  or −1), `group(re, s, n)` (substring), `groups(re, s)` (all groups as `[string]`).
  `release` frees the C-owned handle — the compiled pattern is C-malloc'd, **not**
  caller-managed, so call it when done.
- **`http`** — an HTTP(S) client over **libcurl**, and the first module to
  declare an [external dependency](#external-dependencies-c-shim-deps) (`corelib/http/deps`
  → `libcurl`). `get(url) -> ptr` / `post(url, body, content_type) -> ptr` return an opaque
  response handle, or null on a transport failure (`ok`/`is_null`). `status(r)` (e.g. 200),
  `body(r)` (arena-copied), `release(r)` (the handle is C-owned). Convenience: `get_body(url)`
  / `get_status(url)` do the request and free the handle. The body is arena-copied via the
  FFI string-return, so a binary body with interior `0x00` truncates — `body(r)` is for text
  APIs; `body_bytes(r)` is the binary-safe sibling (interior NULs preserved).
  `post(url, body, content_type)` takes a `string` body and stops at the first interior
  `0x00`; `post_bytes(url, body, content_type)` is its binary-safe sibling, taking
  `bytes` and sending the whole length. Tests are skipped where libcurl is absent.
- **`json`** — a recursive-descent JSON parser + serializer. The document is a
  value-semantic tree, the `Json` enum (`JNull`/`JBool`/`JNum`/`JFloat`/`JStr`/`JArr`/
  `JObj`, objects as parallel key/value arrays). **`parse_checked(s) ->
  Result(Json, JsonErr)` is the entry point new code wants** — the grammar it accepts
  is exactly RFC 8259's and every refusal carries the byte offset that caused it
  (`err_reason`, `err_offset`, `err_str`). `parse(s) -> Json` is the lenient wrapper
  that throws the error away and returns `JNull` for the whole document.
  `stringify(j) -> string` (compact; escapes `"` `\` and every control byte, `\b \f
  \n \r \t` where a short form exists). Typed queries: `kind` (the variant tag as a
  string), `get(j, key)` (object field, else `JNull`), `at(j, i)` (array element, else
  `JNull`), `keys` (object keys in order), `len_of` (array/object count or string
  length), `as_num`/`as_str`/`as_bool`/`as_float`/`as_lexeme` (payload with a zero-value
  default). Variants are constructible cross-package (`json.JNum(1)`).
  Round-trip fidelity: an integer that fits 64 bits is `JNum` and everything else — a
  fraction, an exponent, or an integer too large — is `JFloat(value, lexeme)` carrying
  the original digits, so `stringify` re-emits every accepted number byte-for-byte;
  object keys keep insertion order; a duplicate key resolves to its **last** value at
  the first key's position, so a parsed object holds each key once; a `\uXXXX` escape
  decodes to its UTF-8 bytes. Two limits: a number outside binary64's range
  (`1e400`) is refused rather than represented, and `get` returns `JNull` for both
  a null-valued member and an absent one — walk `keys` to tell those apart.
  `err_at(code, off)` builds a `JsonErr` for a given code and byte offset, for a caller
  layering its own parse step on top of this one.
- **`csv`** — an RFC 4180 CSV parser + serializer. A document is rows of fields,
  `[[string]]`. `parse(s) -> [[string]]` handles quoted fields, the `""` escape,
  embedded delimiters/newlines inside quotes, and LF / CRLF / lone-CR line endings;
  it fails closed (an unterminated quote parses leniently, never aborts). Bytes after
  a closing quote JOIN the field (`"ab"cd` is `abcd`). A trailing newline adds no empty
  row; a mid-file blank line is a row with NO fields. `stringify(rows) -> string`
  emits LF endings and quotes only fields containing the delimiter/quote/CR/LF
  (doubling internal quotes) -- `parse`/`stringify` round-trip.
  `parse_delim`/`stringify_delim` take an arbitrary single-byte delimiter (TSV is
  `parse_delim(s, 9)`); `get(rows, r, c)` is a bounds-safe cell read (`""` if OOB).
- **`markdown`** — a pragmatic Markdown → HTML renderer in pure Tycho. `render(src)`
  handles ATX headings, fenced code (HTML-escaped), blockquotes, unordered/ordered
  lists, thematic breaks, and paragraphs; inline `**bold**`, `*italic*`/`_italic_`,
  `` `code` ``, `[text](url)`, `![alt](src)`. All text is HTML-escaped and unknown
  syntax degrades to escaped plain text (never a parse abort). Not full CommonMark
  (no nested lists, reference links, tables, or setext headings) — targets a
  blog/wiki. `esc(s)` is that HTML escaper on its own, for callers assembling
  markup around rendered output.
- **`base64`** — Base64 (RFC 4648) `encode`/`decode`, plus `encode_url` (URL-safe
  `-`/`_` alphabet, no padding). `decode` is lenient: it skips any non-alphabet byte
  (padding `=`, whitespace, newlines in wrapped Base64) and accepts both alphabets.
  Tycho strings are length-carrying and can contain interior `0x00` bytes, so `decode`
  preserves NULs as well as other decoded bytes. Use `bytes` when crossing a binary C API.
- **`hex`** — hexadecimal `encode` (lowercase) / `encode_upper` / `decode`, two digits per
  byte, plus `is_valid` (strict: even length, all hex digits). `decode` is lenient (skips
  any non-hex byte, so `de:ad:be:ef` and spaced/newlined hex work, either case) and reuses
  `core:char`'s `hex_val`. Decoding `"00"` returns a one-byte string containing NUL.
- **`url`** — URL percent-encoding (RFC 3986). `encode` is component-style (like JS
  `encodeURIComponent` — everything but unreserved `A-Z a-z 0-9 - _ . ~` becomes `%XX`,
  uppercase, space → `%20`); `encode_form` / `decode_form` use the
  `x-www-form-urlencoded` convention (space ↔ `+`); `decode` decodes `%XX` (leaving `+`).
  `decode` is lenient (a `%` not followed by two hex digits is emitted literally) and reuses
  `core:char`'s `hex_val`. `%00` produces an interior NUL byte in the returned string.
- **`uuid`** — version-4 UUIDs (RFC 4122) plus the nil UUID and helpers. `v4(&st)`
  draws 16 bytes from `core:rand` (state threaded by `inout`) and sets the
  version/variant bits; it is **not unguessable** — use `crypto.random_hex` for a
  token. `nil()`, `parse(s) -> [int]` (16 bytes, lenient on separators, `[]` unless
  exactly 32 hex digits), `format(bytes) -> string` (canonical 8-4-4-4-12),
  `is_valid` (strict), `version` (the version nibble, `-1` if invalid). v1 and the
  name-based v3/v5 are out of scope. Zero bytes survive formatting.
- **`hash`** — non-cryptographic 32-bit hashes for hash tables / checksums / dedup (NOT for
  security): `fnv1a_32`, `djb2`, `sdbm`, and `crc32` (IEEE/zlib, bit-by-bit), plus `to_hex`
  (8-digit lowercase). All return a non-negative int in `[0, 2^32)`. Matches published vectors:
  `crc32("123456789") = cbf43926`, `fnv1a_32("foobar") = bf9cf968`. For a hash of
  **any** hashable value (struct/tuple/array, not just a string) use the generic builtin
  `hash(x)` — deterministic (the same value hashes the same on every run and machine),
  full 64-bit as a signed int ([builtins §29.7](../spec/16-builtins.md#297-maps)).
- **`md5`** — the MD5 message-digest (RFC 1321): `hex(s)` (32-char lowercase digest) and
  `digest(s)` (16 raw bytes). **MD5 is broken for security** (use it for checksums /
  content-addressing / interop, never passwords or signatures). Bit-exact against the
  RFC 1321 suite (`md5("abc") = 900150983cd24fb0d6963f7d28e17f72`).
- **`sha256`** — the SHA-256 hash (FIPS 180-4): `hex(s)` (64-char lowercase digest) and
  `digest(s)` (32 raw bytes). A **real cryptographic digest** — fine for checksums,
  content addressing, and HMAC building blocks (not a standalone password hash; use a KDF
  for that). Bit-exact against NIST vectors (`sha256("abc") = ba7816bf…f20015ad`).
  For input too large to hold at once there is a **streaming** form: `init()` gives
  a `State`, `update(&st, data)` folds in each chunk, and `final_hex(&st)` closes
  it and returns the digest — the same answer `hex(s)` gives for the concatenation.
- **`bignum`** — arbitrary-precision integers in pure Tycho (sign + base-10⁹ limb array),
  value-semantic: `from_int`/`from_str`/`to_str`/`to_int`, `add`/`sub`/`mul`/`divmod`/`div`/
  `mod`/`pow`, `abs`/`neg`/`cmp`/`is_zero`.
- **`decimal`** — arbitrary-precision base-10 fixed point, composed on `core:bignum` (a `Big`
  coefficient × 10⁻ˢᶜᵃˡᵉ), so decimal fractions are **exact** (`0.1 + 0.2 == 0.3`):
  `from_int`/`from_str`/`to_str`, `add`/`sub`/`mul` (exact), `cmp`, `neg`/`abs`/`is_zero`,
  `from_str_checked(s) -> Result(Decimal, DecErr)` — the bare `from_str` fails open
  (`"1.5x"` is `0.15`), so use the checked one on anything a user or a file supplied —
  `rescale` (truncating). Division takes its policy from the caller:
  `div(a, b, scale, mode) -> Result(Decimal, DivErr)`, where `mode` is `half_up()` (ties away
  from zero) or `toward_zero()` (agrees with `rescale`) — there is no default, because there
  is no correct one. A zero divisor, a negative scale and an unknown mode are each an `Err`.
- **`crypto`** — the security-grade module, a C-shim over OpenSSL `libcrypto` (see
  [C-shim modules](#c-shim-ffi-backed-modules); needs the OpenSSL dev package). Where the
  pure-Tycho `sha256`/`md5` are for non-adversarial integrity, this is what to reach for
  when an attacker is in the threat model: CSPRNG (`random_hex`), `sha256`/`sha512` of
  binary, `hmac_sha256`, `pbkdf2_sha256`, constant-time `ct_equal`, ChaCha20-Poly1305 AEAD
  (`aead_encrypt`/`aead_decrypt`, `"!err"` on auth failure), Ed25519
  (`ed25519_pubkey`/`sign`/`verify`), and X25519 (`x25519_pubkey`/`shared`). Pure Tycho
  arithmetic cannot promise constant-time behavior, so these primitives bind OpenSSL
  rather than reimplementing them. Every value (keys, nonces, ciphertext, signatures,
  digests) crosses as lowercase hexadecimal text at this interface; use `core:hex`
  to convert byte buffers. Checked against independent known-answer vectors (RFC 4231 HMAC,
  RFC 7914 PBKDF2, RFC 8439 ChaCha20-Poly1305, Ed25519/X25519).
  **Keys are opaque handles — the lifecycle first:**
  `key_random(n) -> ptr` makes one from the CSPRNG, `key_from_hex(hex) -> ptr`
  imports one, `key_export_hex(k) -> string` exports it, `key_len(k) -> int` is
  its byte length, and `key_free(k)` releases it. That handle is what
  `aead_encrypt`/`aead_decrypt` take. The signature keys have their own
  constructors — `ed25519_key_from_seed(seed_hex)`, then
  `ed25519_sign(key, msg_hex) -> string` and
  `ed25519_verify(pub_hex, msg_hex, sig_hex) -> bool` — as does key agreement:
  `x25519_key_random()`, `x25519_key_from_secret(secret_hex)`, and
  `x25519_shared(my_key, their_pubkey_hex) -> ptr`, whose result is a key handle
  ready for the AEAD.
- **`result`** — the `Result` / `Option` collapses, generic over `$T` and `$E`:
  `unwrap_or(r, fallback)`, `is_ok(r)`, `is_err(r)`, `err_or(r, fallback)`,
  `map_err(r, replacement)`, `map_err_with(r, f)`, and
  `some_or(o, fallback)` / `is_some(o)` for the `Option` half. `or_return` unwraps a
  `Result` only inside a function that itself returns a compatible `Result`, so a
  `main()`, or a handler that returns a `Response`, needs another way:
  `err_or` plus `==` asks *which* failure happened **without writing a `match` at
  all** — a one-line `if`. `map_err(r, replacement)` re-labels the error so
  `or_return` carries it —
  `req := result.map_err(httpd.read_request(fd), net.Failed) or_return` — at the cost of
  the original cause. **`map_err_with(r, f)` is its cause-preserving sibling** —
  identical job and argument order, except `f` receives the `Err` payload and builds the
  replacement from it, so a `Syntax(7)` arrives as `FromParse(7)` rather than as a
  constant. Use it where the cause carries something the caller still needs (a byte
  offset, a column name) and `map_err` where it does not. A caller who *is* writing
  a `match` uses a nested pattern instead (`Err(io.IsDir):`,
  [§14.3.1](../spec/10-statements.md#1431-nested-patterns)); the corelib's error enums
  stay payload-free so both spellings keep working.
- **`testing`** — a unit-test framework, state threaded like `core:rand`'s:
  `t := testing.new("name")`, then `check(&t, cond, msg)` and generic
  `eq(&t, got, want, msg)` (comparable scalars — int/float/string/char; for a
  composite compare with `==` and pass the message), and
  `exit(testing.report(t))` — prints `ok name (N checks)` or the failing checks
  plus `FAIL name (N of M checks failed)` and returns 0/1 for the exit code.
- **`toml`** — a TOML v1.0-subset parser: `parse(s) -> Result(Value, string)`
  (fail-closed), a recursive `Value` enum (TStr/TInt/TFloat/TBool/TArr/TTable;
  tables are parallel keys/values arrays), and `get(t, "a.b.c") -> Option(Value)`
  / `keys(t)`. `get` accepts quote-aware path segments for keys containing dots. Supports bare + quoted + dotted keys, `[table]` and
  `[[array-of-tables]]`, basic strings with escapes and \uXXXX, literal
  strings, ints (dec/0x/0o/0b/underscores), floats, bools, arrays with
  trailing commas; an ISO-8601 datetime is kept as a string. Not inline tables
  or multi-line strings.
  `empty_table()` gives an empty `Value` table, the starting point for building one up rather than parsing it. Parsing is bounded to 64 KiB, 512 assignments/headers and 128 nesting levels; larger documents return `Err` rather than consuming unbounded time or memory.
- **`zip`** — a ZIP archive reader and writer over core:compress's raw
  deflate: `list(bytes)` (names, methods, sizes, CRCs and local offsets),
  `extract_entry(bytes, meta)` for one-pass iteration, `extract(bytes, name)`
  (fail-closed: missing/corrupt entries or a CRC mismatch yield empty), and
  `create_checked([Entry(name, data)]) -> Result(bytes, string)` validates classic-ZIP count,
  name, size and offset fields before writing; legacy `create(...)` remains a
  fail-fast wrapper. Entries are names, not paths — extracting never touches
  the filesystem. The writer's output is read back byte-exactly by Python's zipfile.
- **`sqlite`** — a thin wrapper over libsqlite3, bound directly (no
  hand-written shim — the `extern "sqlite3"` forms name the pkg-config
  dependency, so the package and its test skip where sqlite3 is absent):
  `open(path)` (or `":memory:"`), `close(&db)`, `exec(&db, sql)` (change
  count), `query(&db, sql)` / `query_params(&db, sql, params)` → `[[string]]`
  (every column read as text), `errmsg(db)`. Parameters bind as text and
  coerce; fail-closed with sqlite's own messages.
  `exec_params(&d, sql, params)` is the parameter-binding form of `exec`, returning the affected-row count.
- **`log`** — a leveled logger, state threaded like `core:rand`'s:
  `l := log.init(log.level_info(), false)`, then `log.debug/info/warn/error(&l,
  msg)` — each prints `[level] message`, dropping messages below the logger's
  level; `to_stderr` selects stderr over stdout. The level constants are
  functions: `level_info()`, `level_warn()`, `level_error()` (and `level_debug()`).
  No timestamps (they would make output non-deterministic); prepend your own.
- **`utf8`** — Unicode validation and codepoint iteration over byte strings:
  `valid(s)` (strict — rejects overlong, surrogates, truncated), `decode(s, at)`
  → `(codepoint, width)` at a byte offset (the iteration idiom), `encode(cp)` →
  its UTF-8 bytes, `count(s)` → codepoints or -1. No case mapping or full
  tables — validation + iteration is the core.
- **`io`** — filesystem helpers over the `read_file`/`write_file`/`list_dir` builtins.
  `read(p)` — **fail-open**: `""` for a missing file, an unreadable one and a genuinely
  empty one alike, so prefer `read_text(p) -> Result(string, IoErr)` or `read_bytes(p)`
  when the difference matters —
  `write(p, s)` (truncate, returns false if unopenable),
  `append(p, s)` (no read-rewrite) and `append_text(p, s) -> Result(void, IoErr)`
  (the same, with the error channel), `read_lines(p)` / `write_lines(p, lines)`
  (newline-terminated round-trip), `list(p)` (entry basenames) and `list_checked(p) ->
  Result([string], IoErr)` (which distinguishes an empty directory from an unreadable
  one, where `list` cannot), `rename(src, dst) -> Result(void, IoErr)` (atomically
  replaces a destination on the same filesystem),
  `lock_exclusive(p) -> Result(bool, IoErr)` is a nonblocking process-lifetime lock
  (`Ok(false)` means busy; the OS releases it when the process exits),
  `exists(p)` (one `stat`, not a listing of the parent; `false` means "`stat` could not
  say yes", so it fails closed). For inputs too large to slurp, a bounded-memory
  streaming line reader over a libc shim: `open_lines(p) -> Result(LineReader, io.IoErr)`
  → `read_line(r)` (`Some(line)` / `None` at EOF; embedded NUL bytes are preserved) →
  `close_lines(r)`. A `LineReader` is opaque, and the opener reports *why* it failed:
  `BadPath` for an interior NUL, else `NotFound` / `IsDir` / `Failed`. There is no
  `defer`, so `close_lines` is the caller's to make. Plus `fold_lines(p, init, f)` —
  peak memory is O(longest line), not O(file). `read_bytes(p) -> Result(bytes, io.IoErr)`
  reads a whole file as raw `bytes` (binary-safe); an **empty file is `Ok`** with zero
  bytes, a missing path is `Err(NotFound)` and a directory is `Err(IsDir)` — three
  outcomes a single empty `bytes` would collapse. The variants are payload-free, so `==`
  and a nested `Err(io.IsDir)` arm both work. Three helpers resolve `list`'s
  empty-directory ambiguity: `is_dir(p) -> Result(bool, io.IoErr)`, and `make_dir(p)` /
  `remove(p)` — **one entry each, never recursive** — answering `Ok(true)` for "changed
  it" and `Ok(false)` for "already how you asked" (`EEXIST` on a directory is `Ok(false)`,
  on anything else `Err(Exists)`; `remove` refuses a **non-empty directory** with
  `Err(Failed)`). `make_dir_all(p)` is the recursive form under a **separate name on
  purpose**: no corelib name is secretly recursive. There is deliberately **no
  `remove_all`**: a recursive delete is the one where a wrong path costs unrecoverable
  data. Three more answer what an HTTP server needs: `mtime(p) -> Result(int, io.IoErr)`
  (whole seconds; a **directory is `Ok`**), `size(p) -> Result(int, io.IoErr)` (a length
  without reading; a **directory is `Err(IsDir)`**), and `read_at(p, off, n) ->
  Result(bytes, io.IoErr)` (allocates only what the file holds past `off`, so a length off
  the wire cannot over-allocate; past EOF is `Ok` with zero bytes). The **writers that can
  only succeed or fail** — `write_bytes(p, b)`, `write_at(p, off, b)`, `set_mtime(p, secs)`,
  `rename(src, dst)` and `sync(p)` — return `Result(void, io.IoErr)`, so
  `io.write_bytes(p, b) or_return` is a statement and a `match` arm is `Ok():`.
  `stat(p)` and `lstat(p)` return `Result(Stat, io.IoErr)` with `kind: FileKind`
  (`KindFile` / `KindDir` / `KindSymlink` / `KindFifo` / `KindSocket` / `KindDevice` /
  `KindOther`), `size`, `mtime`, `inode` and `device`; `stat` follows symlinks and
  `lstat` does not. `same_file(a, b) -> Result(bool, io.IoErr)` compares device +
  inode (volume + file index on Windows), so two names of one hard link answer `true`.
  Nothing aborts.
- **`os`** — `is_windows()` reports the host family at runtime — run external commands via
  a **libc-only FFI shim** (no `deps`, nothing to install). `os.system(cmd)` runs `cmd`
  through the shell with stdout/stderr inherited, returning its exit code (0..255,
  `128+signal` if killed, `-1` if the shell won't start); `os.run(cmd)` additionally
  captures stdout into `Output{code, out}`. `cmd` is a `/bin/sh -c` line — shell
  metacharacters are active, so quote untrusted input yourself.
  **`os.exec(argv)` / `os.exec_out(argv)`** are the array-argv pair and take a
  `[string]`: no shell is started at all, so an argument of `"; rm -rf /"` is one
  ordinary argument rather than a second command — prefer them whenever the arguments
  are not compile-time constants. Neither replaces the calling process despite the
  name — both spawn, wait, and return. Same exit-code contract; `exec_out` captures
  stdout. They **fail closed** with `-1` on an empty argv, more than 4096 entries, an
  allocation failure mid-build, or a program that cannot be spawned — never a silent
  fallback to the shell. Implemented on both platforms — POSIX `posix_spawnp`, Windows
  `CreateProcess` (never `cmd.exe`) — but a program that parses its own command line by
  other rules (a `.bat`/`.cmd` file) can still read it differently, so don't hand a batch
  file untrusted argv.
- **`net`** — TCP/UDP sockets over a **libc-only FFI shim** (POSIX sockets; no `deps`,
  nothing to install). **The transfer calls block; readiness does not.**
  `read`/`write`/`connect` block, so a worker-per-connection server still has its
  worker count as a ceiling on concurrent connections — size the pool for the concurrency
  needed and put a timeout on the socket. But two calls take a deadline and do
  not block on the peer: `wait_readable(fds, ms) -> Result([int], NetErr)` names
  exactly the descriptors that are ready (or an empty list when `ms` elapses), and
  `accept_wait(fd, ms) -> Result(int, NetErr)` waits for one inbound connection
  with a timeout.
  `close_fd(fd)` releases a descriptor obtained this way. Every fallible TCP call returns
  **`Result(T, net.NetErr)`**: `listen`/`accept`/`connect`/`port_of` give `Result(int, …)`,
  `peer_addr(fd)` gives `Result(string, …)` — the connected peer's address as text,
  the field a real access log leads with.
  `write` gives `Result(int, …)` (bytes sent), `read` gives `Result(bytes, …)`. Payloads
  are binary-safe `bytes`. `NetErr` is `Eof` / `Timeout` / `Failed`, all payload-free so
  a single cause can be tested with `==` without opening a `match` at all — or, inside
  one, with a nested `Err(net.Timeout)` arm (§14.3.1).
  `read` **distinguishes** a clean hangup (`Err(Eof)`) from an expired receive deadline
  (`Err(Timeout)`) from a hard error (`Err(Failed)`) — three answers a bare empty `bytes`
  would collapse into one. `set_read_timeout_ms(fd, ms)` arms a receive deadline and stays
  a plain `bool` — one failure, one cause; `ms <= 0` restores the blocking default and
  `false` means the option could not be set (the socket keeps blocking). The **UDP** calls
  (`udp_bind`/`udp_send`/`udp_read`) are still sentinel-based on purpose: a zero-length
  datagram is legal, so `udp_read`'s empty result is a real success value.
- **`httpd`** — a minimal HTTP/1.1 **server** toolkit over `core:net` (no external
  dependency — net is libc-only). The request/response plumbing is pure Tycho; the accept
  loop is the caller's. `parse_request(raw) -> Result(Request, httpd.ReqErr)`
  (method/path/version, case-insensitive `header(r, name)`, `has_header(r, name)`
  distinguishes an absent field from a present-but-empty one, honors Content-Length;
  `Err(Malformed)` on a malformed request line);
  `response(status, body: bytes)` (standard reason phrase) /
  `response_reason(status, reason, body: bytes)` (a custom phrase for a status the table
  does not carry) / `text_response(status, body: string)` /
  `with_header(r, k, v)` / `with_body(r, b)` / `render_head(r) -> string` /
  `render(r) -> bytes` (Content-Length and a default `text/plain` Content-Type are added
  automatically); and the socket glue `read_request(fd) -> Result(Request, httpd.ReqErr)`
  (reads until the header terminator, then exactly Content-Length body bytes, bounded so a
  hostile peer can't spin) — its `Err` says **which** failure, `Malformed` / `Closed` /
  `Timeout` / `Failed` / `TooLarge`, so a server can answer `400` to garbage while hanging
  up silently on a disconnect /
  `read_request_capped(fd, cap) -> (Result(Request, httpd.ReqErr), string)` (the same read
  with a caller byte budget — reaching it with no header terminator is `Err(TooLarge)` —
  plus every byte read as the second tuple element, so an access log can name a request
  that would not parse; `read_request` is this with `cap = MAX_REQUEST`) /
  `read_request_deadline(fd, cap, deadline_ms)` (the same again, with a deadline on the
  **whole** head-and-body rather than on one `read`) /
  `read_request_resume(fd, cap, deadline_ms, have)` (the same again, resumed: `have` is the
  buffer a previous call returned beside `Err(Timeout)` on **this same fd**, so an event
  loop can give a slow head a short slice per visit. Hand the buffer back only after
  `Err(Timeout)` and drop it on every other outcome — carrying it past a completed request
  would splice two heads together; `cap` bounds `have` plus everything read after it) /
  `write_response(fd, r) -> Result(int, net.NetErr)` (Ok = total bytes written;
  the same error type `net.write` returns, so `or_return` propagates a failed send).
  **Binary-safe bodies** — `Request.body` and `Response.body` are `bytes`, so a PNG or a
  font round-trips byte for byte; headers stay `string` (ASCII by spec).
  `content_type(path)` maps a file extension to a MIME type — `.html .htm .css .js .mjs
  .json .txt .xml .svg .png .jpg .jpeg .gif .webp .ico .woff .woff2 .pdf .wasm` — and an
  **unknown extension yields `application/octet-stream`, never `text/plain`**.
  Keep-alive: `connection_close(req)` (HTTP/1.1 defaults open, 1.0 defaults closed, an
  empty `bad_request()` always closes) and `with_connection(r, alive)`; pair it with
  `net.set_read_timeout_ms` so an idle peer cannot pin a worker. CRLF is built with
  `chr(13)` (tycho strings have no `\r` escape).
  **Framing is refused, never guessed**, by all three read entry points: any
  `Transfer-Encoding` field and an empty/invalid `Content-Length` are refused —
  **chunked bodies are not decoded** — a `Content-Length` that is not a plain decimal,
  two `Content-Length` headers that disagree, or `Content-Length` together with
  `Transfer-Encoding`, are each `Err(Malformed)`, and the connection must not be reused.
  A framed size past `cap` is `Err(TooLarge)` *before* the body is read; a body shorter
  than its own `Content-Length` is `Err(Closed)`.
  **Pipelining is not supported.** Exactly the bytes this request framed are parsed;
  anything the peer sent past them is discarded with the buffer, so a client that
  pipelines loses every request after the first. What matters is that the leftover is
  never glued onto this request's body.
  The request-shaping helpers the server layer calls are public too: `reason_phrase(status)`,
  `bodyless(status)` (whether a status may carry a body at all — 204/304 and the 1xx range),
  `content_length_conflict(r)` (a request carrying both `Content-Length` and
  `Transfer-Encoding`), `parse_len(s) -> (int, bool)` (a length header with its validity
  flag), `has_ext(path, ext)`, and `crlf()`.
- **`cli`** — command-line argument parsing, pure string math. `parse(argv) -> Cli` sorts the
  vector into three buckets: `--key=value` **options**, boolean **flags** (`--flag`, and short
  clusters `-abc` → `a`/`b`/`c`), and **positionals** (everything else, plus everything after
  a bare `--`). Values always attach with `=`, so `parse` needs **no schema** of which options
  take a value — `--verbose` is unambiguously a flag and `--out=x` unambiguously an option.
  `argv()` is the builtin `args()` with `args()[0]` (the program path) dropped, which is what
  `parse` wants, so a program's whole front door is `c := cli.parse(cli.argv())`. Pass a
  vector to `parse` yourself and it is used as given; the skip lives in `argv()`, not in
  `parse`. `parse_spec(argv, valued, boolean) -> Cli` is the **schema-carrying** form, and it
  is what buys the space-separated Unix spelling `--root DIR`: `valued` and `boolean` list
  long option names and short flag letters **without dashes**, the same spelling
  `get`/`flag` take. It is an addition, not a migration — `parse` is unchanged and every
  `=`-attached spelling means the same thing in both. Its rules: a `valued` name consumes
  the next argument **as-is** (so `--root --port` sets root to `--port`) or lands in
  `missing(c)` if there is nothing after it; a `boolean` name spelled `--flag=v` sets the
  flag and **drops** `v`; a name in neither list, and a short cluster containing a letter in
  neither, land in `unknown(c)` as written. `parse_checked(argv, valued, boolean) ->
  Result(Cli, CliErr)` is `parse_spec` that refuses instead of sorting: `Err(Unknown(tok))`
  or `Err(NoValue(name))` on the first offending token, so a program cannot forget to look
  at `unknown`/`missing`. No short option takes a value (`-p 80` is not a spelling this
  package has — it would make `-abc` ambiguous); use `--port 80`. Accessors:
  `get(c, key, default)`, `has(c, key)`, `flag(c, name)` (long or short), `positionals(c)`,
  `count(c)`, `missing(c)`, `unknown(c)`. Uses parallel arrays, not maps.
- **`raster`** — pure-Tycho raster image codecs, **BMP** and **QOI**, with no external
  dependency (unlike `core:image`'s libpng-backed PNG). An `Image` is 8-bit RGBA (4
  bytes/pixel, row-major, top-to-bottom); pixel data is `bytes`. `encode_bmp`/`decode_bmp`
  (writes 32-bit BGRA, bottom-up, uncompressed; reads 24- or 32-bpp `BI_RGB`) and
  `encode_qoi`/`decode_qoi` (all six QOI chunk types). Both **round-trip losslessly**; the
  decoders **fail closed** to a 0×0 Image on a malformed / truncated / wrong-format input
  (check `width > 0`). `too_big(w, h)` is the decode guard in front of every codec here:
  it answers whether a declared width and height would allocate past the ceiling.
- **`signal`** — **clean shutdown on SIGTERM/SIGINT, and nothing else**: a libc-only shim
  with nothing to install and nothing to link.
  `on_shutdown(fd) -> bool` installs one handler for both signals whose **only** action is
  to shut down the listening socket, so every thread blocked in `net.accept(fd)` is
  released with `Err(Failed)` and the program winds down through the path it already has.
  It **fails closed**: `false` means the handler was not installed and the default
  disposition (die on SIGTERM) still stands.
  `shutdown_requested() -> bool` reads the flag the handler set, for a loop that wants to
  stop for a reason other than a failed accept. **Order matters** — call `on_shutdown`
  *after* `net.listen`, with the listening fd, and *before* the accept loops start.
  There is deliberately no `signal.on(sig, handler)`: no Tycho code runs in handler
  context, so a general handler API would need answers about allocation, tasks, and
  signal delivery that do not exist here. Waking a blocked `accept()` this way is a
  Linux behavior rather than a portable guarantee, and connections still in the backlog
  are dropped rather than drained.
  For a server that must report which connections it dropped, `register_conn(slot, fd)`
  records a live descriptor against a slot and `retire_conn(slot)` clears it.

## C-shim (FFI-backed) modules

A core module can wrap a C library via FFI. Drop a `<module>/<module>_shim.c` next to
the `.ty`; the transpiler auto-compiles and links it on `import "core:<module>"` (no
`--shim` needed). The `.ty` declares the shim's functions with `extern fn` (and
`extern "Lib" fn` auto-adds `-lLib` for an external library; `core:regex` needs none —
POSIX regex is in libc). Opaque handles cross as `ptr` (carried by value, never
dereferenced or caller-managed — `null` / `is_null`). `tychoc` links a module's shim
automatically — no `--shim` needed for a corelib package.

### Freeing a shim handle

A `ptr` handle is not affine, so nothing in the type system stops a caller freeing one
twice or reading one after free. `core:crypto`, `core:tls` and `core:http` guard that at
run time by keeping the handle's own header: the payload is released on free, so a second
free or a later use aborts by name (for example `tycho: double free of crypto key
handle`) rather than reading freed memory. The kept header is never reused for a new
handle, so a stale handle can never alias a later one; it is also never freed, so each
freed handle keeps a small header for the life of the process — nothing measurable for a
one-shot program, and the figure for a long-lived one to check is in the design note.

`core:image` uses a monotonically numbered handle into a live set instead: releasing an
image frees its header and pixels, and a stale ID is rejected before anything is read.
That keeps the same double-free/use-after-free diagnosis without retaining one header per
decoded image.

### External dependencies (C-shim `deps`)

A shim that needs a system library beyond libc — one with headers to find and a link
flag that **varies by platform** — declares it in a `corelib/<module>/deps` manifest:
pkg-config package names, one per line (`#` comments and blank lines ignored). For
example `corelib/http/deps` is just `libcurl`.

The `deps`-backed modules are `http` (libcurl) and `crypto` (libcrypto), documented above,
plus four more:

- **`gfx`** — SDL3 window/render/audio (`deps: sdl3`); see the package entry above.
- **`compress`** — gzip (RFC 1952) compress/decompress over **zlib** (`deps: zlib`).
  `compress(bytes) -> bytes`; `decompress(bytes) -> Result(bytes, ZErr)` and
  `raw_decompress` likewise, where `ZErr` is `Corrupt` / `Truncated` / `Failed` /
  `TooBig` (decompressed output exceeds its limit).
  **`Ok` with length 0 is a real empty payload**, distinct from every failure. On a RAW
  deflate stream `Truncated` also covers "not a deflate stream": there is no wrapper or
  checksum to tell those apart.
  **`compress` is byte-deterministic** and callers may rely on it: for one zlib build
  it is a pure function of its input — same bytes in, byte-identical bytes out. The
  compressed *length* is a zlib tuning detail — it is not stable across zlib versions.
  `raw_compress(data)` is the same deflate without the gzip framing, for callers that
  supply their own container. Each decompression is capped at 1 GiB (or the zlib `uInt`
  limit when lower).
- **`image`** — PNG decode/encode over **libpng** (`deps: libpng`): `decode(bytes) ->
  Result(Image{width, height, pixels}, ImgErr)` (8-bit RGBA) and `encode(Image) ->
  Result(bytes, ImgErr)`. `ImgErr` names which failure: `Empty`, `NotPng`, `Corrupt` on the
  decode side, `BadDims`, `ShortPixels` on the encode side, `Failed` for an allocation or
  a libpng failure. PNG has no 0-dimension image, so an `Ok` always carries `width >= 1`.
  JPEG is a demand-gated follow-up.
- **`tls`** — a TLS 1.2/1.3 client over **OpenSSL** (`deps: openssl`). Secure by default:
  `connect(host, port)` verifies the certificate against the system CA store, checks the
  hostname, and sends SNI; failure returns a null handle (never a silent insecure
  connection). `connect_timeout(host, port, ms)` bounds TCP connect and handshake,
  and `set_timeout_ms(conn, ms)` bounds later reads/writes; timeout values outside
  0..2,147,483,647 fail closed. `write`/`read`/`close_conn` use the encrypted stream.

A libc-only shim needs no `deps` — `core:regex`, `core:os`, `core:net` and `core:signal`
among them; a library with no `.pc` file can still use `extern "Lib" fn` for a bare `-lLib`.

## Testing

`make corelib` (→ `corelib/run.sh`): every `corelib/test/<name>/main.ty` is built and
run, and must produce the golden `corelib/test/<name>.out`. Re-record goldens with `RECORD=1 sh corelib/run.sh`. Part of
`make ci`.

## Examples

Every module also has a small, readable **usage example** at
`examples/corelib/<name>/main.ty` — idiomatic calls with human-friendly output, as
opposed to the assertion-style tests above. `make corelib-examples`
(→ `examples/corelib/run.sh`) validates them the same way against
`examples/corelib/<name>.out`, with the same dependency skip, and is part of `make ci`.

## The corelib surface (pre-1.0 — NOT frozen)

**The corelib API is not frozen.** The packages above are locked by `make corelib` and
`make corelib-examples`, which is what makes the surface worth relying on in practice.
What follows is the promise:

- **Not frozen.** A public function may change name, signature, or semantics
  before 1.0, with a `CHANGELOG.md` entry and no deprecation window. In practice
  changes are additive, but "in practice" is not a contract.
- **Still a regression, not a licence.** A bug in a package's documented
  behavior is a bug to fix, not an excuse to redesign the API around it.
- **Caveats inside the surface stay:** `core:hash` is non-cryptographic and
  `core:md5` is broken for security (use `core:sha256`), and the FFI-backed packages
  (`core:http`, `core:tls`, `core:crypto`, `core:image`, `core:sqlite`, `core:compress`)
  need their external library present at build time (the skip-if-absent convention above).
- **New packages still arrive only through the demand gate** — a real program
  that needs them — and they join the surface the day they ship, with their
  test + golden + this guide entry.
- **The surface is the same on native Windows (MSYS2/mingw)**, and so is every
  signature. Three packages *behave* differently there — `core:signal`,
  `core:datetime`'s POSIX-`TZ` offsets, and `core:os`'s shell-out; each difference is
  spelled out in [SECURITY.md](../../SECURITY.md).

**Deprecation path.** A package or function is deprecated by (1) a `deprecated:`
notice in its doc comment, (2) an entry in `CHANGELOG.md` naming the
replacement, and (3) a `warning:` on use emitted by the compiler.

Step (3) is mechanical: a `# deprecated: <text>` comment line **directly
above** a `fn` marks it, and every call site warns with `<text>`. Deprecated
members keep working for the current major version. **Removal is a breaking change**:
it happens only in a major version bump, is recorded in `CHANGELOG.md` and
`RELEASE_NOTES.md`, and ships with the migration spelled out.

Before 1.0 none of that binds: a removal needs the changelog entry and nothing
else. Which packages are in the frozen surface is a decision 1.0 has to make,
not one inherited from this list — see
[ROADMAP.md](../../ROADMAP.md#what-10-requires).

---

Why corelib is shaped this way — the handle guards, the path-check layering,
and the shim conventions — is in [design-corelib](../internals/design-corelib.md),
for maintainers.
