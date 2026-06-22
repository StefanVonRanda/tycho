# corelib — Tycho's standard library

corelib is Tycho's standard library: a set of packages under `corelib/`, imported with the
`core:` collection root. It is experimental, like the rest of Tycho, but the modules below
are usable today.

```
import "core:math"

fn main():
    print(f"gcd={math.gcd(12, 18)}\n")
```

## Quick start

1. `import "core:<pkg>"` in your source. The name `<pkg>` is bound to the package.
   The compiler finds `corelib/` next to its own binary by default, so no setup is
   needed in this repo. Set `TYCHO_CORELIB` to a corelib directory to override.
2. Call its functions either free-standing (`math.gcd(12, 18)`) or method-style via UFCS
   (`x.abs()`).

## Resolution

`import "core:<pkg>"` resolves to `<corelib>/<pkg>` and binds the name `<pkg>`, where
`<corelib>` is `TYCHO_CORELIB` if set, otherwise `corelib/` next to the compiler binary.
Non-`core:` imports stay relative to the importing package, unchanged. Both Tycho compilers
(`tychoc` and the self-hosted `tychoc0`) resolve `core:` natively, so `tychoc0` is a
standalone corelib-aware compiler with no bundling step required.

## How corelib is shaped

corelib is **concrete free functions over concrete types**. Tycho has no generics, so array
utilities come as one package per element type (for example `[int]`, `[string]`, and
`[float]` each get their own sibling package). The functions are also callable method-style
through UFCS, so `index_of(xs, v)` and `xs.index_of(v)` are the same call.

Higher-order helpers (map / filter / reduce that take a function) live in `core:iter` and
its siblings, and take a first-class `fn`/closure argument. Lambdas in Tycho are
expression-bodied; pass a named function where you need a multi-line body.

This concrete, per-type shape is a consequence of the language's minimalism rather than a
corelib limitation.

## Packages

- **`math`** — integer utilities: `abs`, `imin`, `imax`, `clamp(x, lo, hi)`, `sign`,
  `gcd`, `ipow(base, exp)` (exp ≥ 0). (`sqrt`/`pow`/`floor`/`fabs` are float builtins.)
- **`fmath`** — float utilities over the float builtins: `pi`, `e`, `min`, `max`,
  `clamp`, `sign`, `round` (half away from zero), `trunc`, `lerp(a, b, t)`,
  `approx_eq(a, b, eps)`. (No trig — the language has no libm sin/cos builtin.)
- **`char`** — byte/char classification & conversion over int byte values (what
  `s[i]`/`chr` use): `is_digit`, `is_alpha`, `is_alnum`, `is_upper`, `is_lower`,
  `is_space`, `is_hex`, `to_upper`/`to_lower` (one byte), `digit_val` (−1 if not a
  digit), `hex_val` (0..15 or −1). The lexer/parser workhorse.
- **`strings`** — `to_upper`, `to_lower`, `starts_with`, `ends_with`, `contains`,
  `repeat(s, n)`, `trim` (ASCII whitespace), `parse_int`, `is_space`, `lines` (splits on
  `\n`, drops one trailing `\r` per line, trailing newline adds no empty line), `replace`
  (non-overlapping, left to right; empty `old` returns the input unchanged), `count(s, sub)`,
  `strip_prefix`/`strip_suffix`, `pad_left`/`pad_right(s, width, pad)` (pad is one byte),
  `reverse`, `capitalize`, `split_once(s, sep) -> (before, after)` (`(s, "")` if absent).
  (`split`/`find`/`substr`/`len`/`chr` are builtins.)
- **`path`** — POSIX path utilities, all pure string math (separator `/`, no
  filesystem access, every function returns a fresh value): `base` (final element,
  trailing slashes ignored; `""`→`.`, `"/"`→`/`), `dir` (all but the final element),
  `ext` (extension incl. the dot, `""` if none; a leading dot is a dotfile, not an
  ext), `stem` (base minus ext), `join(a, b)` (exactly one separator, empty operands
  drop out), `is_abs`, `split_path(p) -> (dir, base)` (the inverse of `join`), and
  `clean` (lexical normalize: collapse `//`, drop `.`, resolve `..` but never above
  the root or past a leading `..`). `last_slash(s)` is the shared scan helper.
- **`arrays`** — `[int]` utilities: `contains`, `index_of` (−1 if absent), `count`, `sum`,
  `product`, `imin`, `imax`, `reverse`, `is_sorted`, `sort` (ascending), `take(xs, n)`,
  `drop(xs, n)`, `concat(a, b)`, `fill(n, v)`, `dedup` (consecutive — sort first for a full
  dedup). All return a new array — value semantics, the input is never mutated. (`push`/
  `pop`/`len`/`range` are builtins.)
- **`arrays_str`** — the same over `[string]` (no overloading in tycho, so per-type variants
  are sibling packages): `contains`, `index_of`, `count`, `join(xs, sep)`, `smin`, `smax`
  (lexicographic), `reverse`, `is_sorted`, `sort`.
- **`arrays_float`** — the same over `[float]`: `contains`, `index_of`, `count`, `sum`,
  `fmin`, `fmax`, `reverse`, `is_sorted`, `sort`. (Equality/`contains` use exact float `==`.)
- **`iter`** — higher-order over `[int]`, each taking a `fn`/closure: `map`, `filter`,
  `reduce`, `count`, `any`.
- **`iter_str`**, **`iter_float`** — the same five over `[string]` / `[float]`.
- **`sort`** — `argsort(keys)` / `argsort_desc(keys)` (`[int]`) / `argsort_str(keys)`:
  return the index permutation that orders the keys — the no-generics way to sort
  anything (keep data in parallel arrays, argsort one, walk all through the permutation).
  All stable. Plus `by_key(xs, key)`: sort `[int]` by a derived key fn/closure.
- **`rand`** — deterministic xorshift32 (not cryptographic). No globals in tycho, so the
  state is an explicit int threaded via `mut`: `st := rand.seed(42)`,
  `rand.next(&st)` ([1, 2³²)), `rand.below(&st, n)` ([0, n)), `rand.shuffle(&st, xs)`
  (Fisher-Yates, returns a new array). Every left shift is masked to 32 bits inside the
  signed 64-bit int, so the generator is UB-free by construction.
- **`time`** — wraps the `clock()` (monotonic ns) and `now()` (UNIX seconds) builtins.
  Stopwatch (value-semantic, no mut — a reading is just an int): `sw := time.start()`,
  then `time.elapsed_ns(sw)` / `elapsed_us` / `elapsed_ms`. Duration conversions
  `ns_to_us` / `ns_to_ms` / `ns_to_s`. Wall clock `unix_secs()` (named so, not `now`,
  to avoid shadowing the builtin and recursing).
- **`datetime`** — civil (proleptic Gregorian) calendar math over UNIX timestamps, all
  pure integer arithmetic (Howard Hinnant's `days_from_civil`/`civil_from_days`, which
  port verbatim because tycho's int `/` truncates like C's). A `DateTime` struct
  (`year`/`month`/`day`/`hour`/`minute`/`second`/`weekday`, all UTC). `from_unix(secs)`
  and its exact inverse `to_unix(dt)`; `days_from_civil`/`civil_from_days`/
  `weekday_from_days` (the day-count core); `weekday(y,m,d)` (0=Sun..6=Sat), `is_leap`,
  `days_in_month`; `now_utc()` (the only non-pure fn — reads `now()`); and formatting
  `format_iso` (`YYYY-MM-DDTHH:MM:SS`), `weekday_name`, `month_name`, `pad2`/`pad4`. No
  timezone database — everything is UTC.
- **`regex`** — POSIX extended regular expressions (ERE), the first **C-shim-backed**
  core module (FFI over `<regex.h>`, libc). `compile(pat) -> ptr` (opaque handle;
  `ok`/`is_null` to check), `is_match`, `find` / `find_end` (offset or −1), `matched`
  (first match substring), `release` (free the C-owned handle). The compiled pattern
  is C-malloc'd, **not** arena-managed, so call `release` when done.
- **`http`** — an HTTP(S) client over **libcurl** (C-shim, FFI), and the first module to
  declare an [external dependency](#external-dependencies-c-shim-deps) (`corelib/http/deps`
  → `libcurl`). `get(url) -> ptr` / `post(url, body, content_type) -> ptr` return an opaque
  response handle, or null on a transport failure (`ok`/`is_null`). `status(r)` (e.g. 200),
  `body(r)` (arena-copied), `release(r)` (the handle is C-owned). Convenience: `get_body(url)`
  / `get_status(url)` do the request and free the handle. The body is arena-copied via the
  FFI string-return, so a binary body with interior `0x00` truncates — this is for text
  APIs. Tests are skipped where libcurl is absent.
- **`json`** — a recursive-descent JSON parser + serializer (the `examples/json.ty`
  demo promoted to a reusable module). The document is a value-semantic tree, the
  `Json` enum (`JNull`/`JBool`/`JNum`/`JStr`/`JArr`/`JObj`, objects as parallel
  key/value arrays). `parse(s) -> Json` (truncated input fails closed to `JNull`,
  no abort), `stringify(j) -> string` (compact, escapes `" \ \n \t`). Typed queries:
  `kind` (the variant tag as a string), `get(j, key)` (object field, else `JNull`),
  `at(j, i)` (array element, else `JNull`), `keys` (object keys in order), `len_of`
  (array/object count or string length), `as_num`/`as_str`/`as_bool` (payload with a
  zero-value default). Variants are constructible cross-package (`json.JNum(1)`) for
  building trees by hand. Scope: integers (no floats), the four common escapes.
- **`csv`** — an RFC 4180 CSV parser + serializer. A document is rows of fields,
  `[[string]]`. `parse(s) -> [[string]]` is a small state machine handling quoted
  fields, the `""` escape, embedded delimiters/newlines inside quotes, and LF / CRLF /
  lone-CR line endings; it fails closed (an unterminated quote parses leniently, never
  aborts). A trailing newline adds no empty row; a mid-file blank line is `[""]`.
  `stringify(rows) -> string` emits LF endings and quotes only fields containing the
  delimiter/quote/CR/LF (doubling internal quotes) -- `parse`/`stringify` round-trip.
  `parse_delim`/`stringify_delim` take an arbitrary single-byte delimiter (TSV is
  `parse_delim(s, 9)`); `get(rows, r, c)` is a bounds-safe cell read (`""` if OOB).
- **`base64`** — Base64 (RFC 4648) `encode`/`decode`, plus `encode_url` (URL-safe
  `-`/`_` alphabet, no padding). Pure arithmetic — the 6-bit packing uses `/` and `%`
  (exact on the unsigned 0..255 that `s[i]` returns), no bit-operators. `decode` is
  lenient: it skips any non-alphabet byte (padding `=`, whitespace, newlines in wrapped
  Base64) and accepts both alphabets. **Byte-safety caveat** (a tycho string-model limit,
  not a Base64 one): `encode` is fully byte-safe, but `decode` builds its result with
  `chr()`, and a tycho string can't hold an interior `0x00` (`chr(0)` appends nothing), so
  decoding plaintext that contains a NUL byte silently drops it — `decode` is exact for
  text and any non-NUL binary, lossy only for data containing `0x00`.
- **`hex`** — hexadecimal `encode` (lowercase) / `encode_upper` / `decode`, two digits per
  byte, plus `is_valid` (strict: even length, all hex digits). `decode` is lenient (skips
  any non-hex byte, so `de:ad:be:ef` and spaced/newlined hex work, either case) and reuses
  `core:char`'s `hex_val`. Same `0x00` decode caveat as `base64` (`decode("00")` is `""`).
- **`url`** — URL percent-encoding (RFC 3986). `encode` is component-style (like JS
  `encodeURIComponent` — everything but unreserved `A-Z a-z 0-9 - _ . ~` becomes `%XX`,
  uppercase, space → `%20`); `encode_form` / `decode_form` use the
  `x-www-form-urlencoded` convention (space ↔ `+`); `decode` decodes `%XX` (leaving `+`).
  `decode` is lenient (a `%` not followed by two hex digits is emitted literally) and reuses
  `core:char`'s `hex_val`. Same `0x00` decode caveat as `base64`/`hex`.
- **`uuid`** — random version-4 UUIDs (RFC 4122) plus the nil UUID and helpers. `v4(&st)`
  draws 16 bytes from `core:rand` (state threaded by `mut`) and sets the version/variant
  bits with plain arithmetic; `nil()`, `parse(s) -> [int]` (16 bytes, lenient on separators,
  `[]` unless exactly 32 hex digits), `format(bytes) -> string` (canonical 8-4-4-4-12),
  `is_valid` (strict), `version` (the version nibble, `-1` if invalid). v1 and the name-based
  v3/v5 are out of scope (they need a MAC / MD5 / SHA-1, none of which corelib has). Bytes are
  formatted with an inline byte→hex, not `chr(b)`, so zero bytes survive.
- **`hash`** — non-cryptographic 32-bit hashes for hash tables / checksums / dedup (NOT for
  security): `fnv1a_32`, `djb2`, `sdbm`, and `crc32` (IEEE/zlib, bit-by-bit), plus `to_hex`
  (8-digit lowercase). All return a non-negative int in `[0, 2^32)` and are kept UB-free the
  same way `core:rand` is — values never leave `[0, 2^32)` and shifts/masks use `* / %` so no
  signed 64-bit overflow (only `^` among the bit-operators). Hashing only reads bytes, so
  there is no `0x00` caveat on the input. Matches published vectors:
  `crc32("123456789") = cbf43926`, `fnv1a_32("foobar") = bf9cf968`.
- **`md5`** — the MD5 message-digest (RFC 1321): `hex(s)` (32-char lowercase digest) and
  `digest(s)` (16 raw bytes). Pure 32-bit arithmetic — adds masked with `% 4294967296`, the
  32-bit NOT is `4294967295 - x`, and the left-rotate uses `* / +` (disjoint halves), all
  UB-free. **MD5 is broken for security** (use it for checksums / content-addressing / interop,
  never passwords or signatures). Bit-exact against the RFC 1321 suite
  (`md5("abc") = 900150983cd24fb0d6963f7d28e17f72`).
- **`sha256`** — the SHA-256 hash (FIPS 180-4): `hex(s)` (64-char lowercase digest) and
  `digest(s)` (32 raw bytes). Pure 32-bit arithmetic, UB-free like `md5`/`hash`; big-endian
  (words, length suffix, output). A **real cryptographic digest** — fine for checksums,
  content addressing, and HMAC building blocks (not a standalone password hash; use a KDF
  for that). Bit-exact against NIST vectors (`sha256("abc") = ba7816bf…f20015ad`).
- **`io`** — filesystem helpers over the `read_file`/`write_file`/`list_dir` builtins,
  and **the first corelib module to compose others** (imports `core:strings` for line
  splitting, `core:path` for `exists`). `read(p)` (`""` if missing/unreadable),
  `write(p, s)` (truncate, returns false if unopenable), `append(p, s)` (read-rewrite,
  not atomic), `read_lines(p)` / `write_lines(p, lines)` (newline-terminated round-trip),
  `list(p)` (entry basenames), `exists(p)` (no exists builtin, so it lists the parent and
  checks membership). Error model mirrors the builtins — nothing aborts.

## C-shim (FFI-backed) modules

A core module can wrap a C library via FFI. Drop a `<module>/<module>_shim.c` next to
the `.ty`; the compiler auto-compiles and links it on `import "core:<module>"` (no
`--shim` needed). The `.ty` declares the shim's functions with `extern fn` (and
`extern "Lib" fn` auto-adds `-lLib` for an external library; `core:regex` needs none —
POSIX regex is in libc). Opaque handles cross as `ptr` (carried by value, never
dereferenced or arena-managed — `null` / `is_null`). Both compilers link a module's shim,
so `tychoc`, `tychoc0 --bundle`, and standalone `tychoc0` all build C-shim modules.

### External dependencies (C-shim `deps`)

A shim that needs a system library beyond libc — one with headers to find and a link
flag that **varies by platform** — declares it in a `corelib/<module>/deps` manifest:
pkg-config package names, one per line (`#` comments and blank lines ignored). For
example `corelib/http/deps` is just `libcurl`.

- **Build:** when `tychoc` auto-discovers a module's shim it reads the sibling `deps` and
  runs `pkg-config --cflags --libs <name>` for each, splicing the result onto the cc line
  (`add_pkg_deps` in `src/tychoc.c`). pkg-config resolves the right include path and libs
  per platform, so `#include <curl/curl.h>` + `-lcurl` just work without hardcoding paths.
- **Skip, don't fail:** `corelib/run.sh` probes the same `deps` with `pkg-config --exists`;
  if any dependency is missing it **skips** that module's test (printing `skip <name>
  (missing dependency: …)`) instead of failing, so machines without the library still
  build. When present, it passes the same `--cflags --libs` on the tychoc0 link paths.

This keeps the test suite libc-only and portable while still allowing library-backed modules.
A libc-only shim (`core:regex`) needs no `deps`; a library with no `.pc` file can still use
`extern "Lib" fn` for a bare `-lLib`.

## Testing

`make corelib` (→ `corelib/run.sh`): every `corelib/test/<name>/main.ty` is compiled three
ways — by the C compiler, via `tychoc --bundle | tychoc0`, and via **standalone** `tychoc0`
(which resolves `core:` itself) — and all three must produce the golden
`corelib/test/<name>.out`. Re-record goldens with `RECORD=1 sh corelib/run.sh`. Part of
`make ci`.

## Examples

Every module also has a small, readable **usage example** at
`examples/corelib/<name>/main.ty` — usage as documentation (idiomatic calls, human-friendly
output), as opposed to the assertion-style tests above. `make corelib-examples`
(→ `examples/corelib/run.sh`) validates them the same three ways against
`examples/corelib/<name>.out`, with the same dependency skip, and is part of `make ci`.
