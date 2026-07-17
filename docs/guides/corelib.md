# corelib — Tycho's standard library

> **Thesis context:** The standard library tests the arena model against real-world
> patterns — I/O, crypto, serialization, hashing, HTTP. Every corelib function allocates
> into the caller's arena and returns independent values. Pure-Tycho modules (json, csv,
> sha256) prove the model handles production-like logic; C-shim modules (regex, http,
> crypto) prove the FFI boundary stays narrow without dragging C's memory model into
> Tycho's world.

corelib is Tycho's standard library: a set of packages under `corelib/`, imported with the
`core:` collection root. It's experimental, like the rest of Tycho, but the modules below
are usable today.

```
import "core:math"

fn main():
    println(f"gcd={math.gcd(12, 18)}")
```

## Quick start

1. `import "core:<pkg>"` in your source. The name `<pkg>` is bound to the package.
   The transpiler finds `corelib/` next to its own binary by default, so no setup is
   needed in this repo. Set `TYCHO_CORELIB` to a corelib directory to override.
2. Call its functions either free-standing (`math.gcd(12, 18)`) or method-style via UFCS
   (`x.abs()`).

## Resolution

`import "core:<pkg>"` resolves to `<corelib>/<pkg>` and binds the name `<pkg>`, where
`<corelib>` is `TYCHO_CORELIB` if set, otherwise `corelib/` next to the transpiler binary.
Non-`core:` imports stay relative to the importing package, unchanged. Both Tycho transpilers
(`tychoc` and the self-hosted `tychoc0`) resolve `core:` natively, so `tychoc0` is a
standalone corelib-aware transpiler with no bundling step required.

## How corelib is shaped

corelib is **generic free functions over a type parameter `$T`**, with `where` constraints
(`comparable(T)`, `numeric(T)`, `has_str(T)`) picking which element types each function
accepts. Tycho monomorphizes generics, so array and iterator utilities are single generic
packages over any element type `[$T]` (`core:arrays`, `core:iter`) rather than one package
per element type. The functions are also callable method-style through UFCS, so
`index_of(xs, v)` and `xs.index_of(v)` are the same call.

Higher-order helpers (map / filter / reduce that take a function) live in `core:iter` and
its siblings, and take a first-class `fn`/closure argument. Lambdas in Tycho are
expression-bodied; pass a named function where you need a multi-line body.

This generic, single-package shape keeps the surface small: one `core:arrays` covers every
element type instead of a family of per-type siblings.

## Packages

- **`math`** — scalar math. `min`/`max`/`clamp(x, lo, hi)` are generic over any
  comparable type (int/float/string/char) and `sign(x)` over any numeric type
  (returns an int −1/0/1); `abs`, `gcd`, `ipow(base, exp)` (exp ≥ 0) are integer-
  specific. (`sqrt`/`pow`/`floor`/`fabs` are float builtins.)
- **`fmath`** — float-only helpers (scalar `min`/`max`/`clamp`/`sign` live in `math`):
  `pi`, `e`, `round` (half away from zero), `trunc`, `lerp(a, b, t)`,
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
- **`arrays`** — generic utilities over any element type `[T]`: `contains`, `index_of`
  (−1 if absent), `count`, `reverse`, `take(xs, n)`, `drop(xs, n)`, `concat(a, b)`,
  `fill(n, v)`, `dedup` (consecutive — sort first for a full dedup); `sort` (ascending),
  `is_sorted`, `min`, `max` (`where comparable` — int/char/float/string); `sum`, `product`
  (`where numeric` — int/float); `join(xs, sep)` (`where` the element converts to a string).
  All return a new array — value semantics, the input is never mutated. `min`/`max`/`sum`/
  `product` seed from `xs[0]`, so they need a non-empty array. (`push`/`pop`/`len`/`range`
  are builtins; higher-order `map`/`filter`/`reduce` live in `iter`.)
- **`iter`** — generic higher-order helpers over any `[T]`, each taking a `fn`/closure:
  `map`, `filter`, `reduce`, `count`, `any`. (Predicates return an int used as a bool.)
- **`sort`** — `argsort(keys)` / `argsort_desc(keys)`: return the index permutation that
  orders the keys — generic over any comparable key type (int/float/string/char). The way
  to order data by a derived value: keep it in parallel arrays, argsort one, and walk every
  array through the permutation. All stable. Plus `by_key(xs, key)`: sort an array by a
  derived int key (a fn/closure).
- **`rand`** — deterministic xorshift32 (not cryptographic). No globals in Tycho, so the
  state is an explicit int threaded via `inout` (the `&` marks the `inout` call site,
  [Basics](../reference/basics.md#procedures)): `st := rand.seed(42)`,
  `rand.next(&st)` ([1, 2³²)), `rand.below(&st, n)` ([0, n)), `rand.shuffle(&st, xs)`
  (Fisher-Yates, returns a new array). Every left shift is masked to 32 bits inside the
  signed 64-bit int, so the generator is UB-free by construction.
- **`time`** — wraps the `clock()` (monotonic ns) and `now()` (UNIX seconds) builtins.
  Stopwatch (value-semantic, no inout — a reading is just an int): `sw := time.start()`,
  then `time.elapsed_ns(sw)` / `elapsed_us` / `elapsed_ms`. Duration conversions
  `ns_to_us` / `ns_to_ms` / `ns_to_s`. Wall clock `unix_secs()` (named so, not `now`,
  to avoid shadowing the builtin and recursing).
- **`datetime`** — civil (proleptic Gregorian) calendar math over UNIX timestamps, all
  pure integer arithmetic (Howard Hinnant's `days_from_civil`/`civil_from_days`, which
  port verbatim because tycho's int `/` truncates like C's). A `DateTime` struct
  (`year`/`month`/`day`/`hour`/`minute`/`second`/`weekday`, all UTC). `from_unix(secs)`
  and its exact inverse `to_unix(dt)`; `days_from_civil`/`civil_from_days`/
  `weekday_from_days` (the day-count core); `weekday(y,m,d)` (0=Sun..6=Sat), `is_leap`,
  `days_in_month`; `now_utc()` (the only non-pure fn — reads `now()`); formatting
  `format_iso` (`YYYY-MM-DDTHH:MM:SS`), `weekday_name`, `month_name`, `pad2`/`pad4`; and its
  inverse **parsing** `parse_iso` (wall-clock fields; `T` or space separator, trailing zone
  ignored) / `parse_iso_tz` (`+HH:MM` / `-HH:MM` / `Z` folded to the UTC instant), plus
  `parse_clf` / `parse_clf_tz` for the Common Log Format stamp (`dd/Mon/yyyy:HH:MM:SS ±HHMM`,
  as in Apache/nginx access logs) and `month_num` (`"Jan"`..`"Dec"` → 1..12), all
  fail-closed via a `year = -1` sentinel that `ok(dt)` checks (a real 4-digit parse is never
  negative). The core is UTC; timezone support is layered on — **fixed offsets** (`from_unix_at`,
  `to_unix_at`, `format_iso_tz`) in pure Tycho, plus DST-aware **system/zone** offsets via a
  small libc shim (`local_offset`, `offset_at`, `now_local`). There is no IANA tz database.
- **`regex`** — POSIX extended regular expressions (ERE), the first **C-shim-backed**
  core module (FFI over `<regex.h>`, libc). `compile(pat) -> ptr` (opaque handle;
  `ok`/`is_null` to check), `is_match`, `find` / `find_end` (offset or −1), `matched`
  (first match substring). **Capture groups** of the first match by index (0 = whole
  match): `ngroups`, `group_start` / `group_end` (offset or −1), `group(re, s, n)`
  (substring), `groups(re, s)` (all groups as `[string]`). `release` frees the C-owned
  handle — the compiled pattern is C-malloc'd, **not** arena-managed, so call it when done.
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
- **`markdown`** — a pragmatic Markdown → HTML renderer in pure Tycho. `render(src)`
  handles ATX headings, fenced code (HTML-escaped), blockquotes, unordered/ordered
  lists, thematic breaks, and paragraphs; inline `**bold**`, `*italic*`/`_italic_`,
  `` `code` ``, `[text](url)`, `![alt](src)`. All text is HTML-escaped and unknown
  syntax degrades to escaped plain text (never a parse abort). Not full CommonMark
  (no nested lists, reference links, tables, or setext headings) — targets a
  blog/wiki.
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
  draws 16 bytes from `core:rand` (state threaded by `inout`) and sets the version/variant
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
- **`bignum`** — arbitrary-precision integers in pure Tycho (sign + base-10⁹ limb array),
  value-semantic: `from_int`/`from_str`/`to_str`/`to_int`, `add`/`sub`/`mul`/`divmod`/`div`/
  `mod`/`pow`, `abs`/`neg`/`cmp`/`is_zero`.
- **`decimal`** — arbitrary-precision base-10 fixed point, composed on `core:bignum` (a `Big`
  coefficient × 10⁻ˢᶜᵃˡᵉ), so decimal fractions are **exact** (`0.1 + 0.2 == 0.3`):
  `from_str`/`to_str`, `add`/`sub`/`mul` (exact), `cmp`, `neg`/`abs`, `rescale` (truncating).
  Division is deferred (needs a rounding policy).
- **`crypto`** — the security-grade module, a C-shim over OpenSSL `libcrypto` (see
  [C-shim modules](#c-shim-ffi-backed-modules); needs the OpenSSL dev package). Where the
  pure-Tycho `sha256`/`md5` are for non-adversarial integrity, this is what you reach for
  when an attacker is in the threat model: CSPRNG (`random_hex`), `sha256`/`sha512` of
  binary, `hmac_sha256`, `pbkdf2_sha256`, constant-time `ct_equal`, ChaCha20-Poly1305 AEAD
  (`aead_encrypt`/`aead_decrypt`, `"!err"` on auth failure), Ed25519
  (`ed25519_pubkey`/`sign`/`verify`), and X25519 (`x25519_pubkey`/`shared`). Real crypto must
  be constant-time and audited, which pure Tycho can't promise, so the primitives are bound
  to OpenSSL rather than reimplemented. Every value (keys, nonces, ciphertext, signatures,
  digests) crosses as lowercase hex, because a Tycho string can't hold a `0x00`; use
  `core:hex` to convert text. Checked against independent known-answer vectors (RFC 4231 HMAC,
  RFC 7914 PBKDF2, RFC 8439 ChaCha20-Poly1305, Ed25519/X25519).
- **`io`** — filesystem helpers over the `read_file`/`write_file`/`list_dir` builtins,
  and **the first corelib module to compose others** (imports `core:strings` for line
  splitting, `core:path` for `exists`). `read(p)` (`""` if missing/unreadable),
  `write(p, s)` (truncate, returns false if unopenable), `append(p, s)` (read-rewrite,
  not atomic), `read_lines(p)` / `write_lines(p, lines)` (newline-terminated round-trip),
  `list(p)` (entry basenames), `exists(p)` (no exists builtin, so it lists the parent and
  checks membership). For inputs too large to slurp, a **bounded-memory streaming line
  reader** over a libc `getline` shim: `open_lines(p)` → `read_line(r)` (`Some(line)` /
  `None` at EOF) → `close_lines(r)`, plus `fold_lines(p, init, f)` — peak memory is
  O(longest line), not O(file). `read_bytes(p)` reads a whole file as raw `bytes`
  (binary-safe — interior NUL bytes are preserved, unlike `read`'s string).
  Error model mirrors the builtins — nothing aborts.
- **`os`** — run external commands, via a **libc-only FFI shim** (`popen`/`system`; no
  `deps`, nothing to install). `os.system(cmd)` runs `cmd` through the shell with stdout/
  stderr inherited, returning its exit code (0..255, `128+signal` if killed, `-1` if the
  shell won't start); `os.run(cmd)` additionally captures stdout into `Output{code, out}`.
  `cmd` is a `/bin/sh -c` line — shell metacharacters are active, so quote untrusted input
  yourself (no array-argv form yet). The stdout-capture read loop lives in `os_shim.c`, so
  Tycho only ever receives the finished, NUL-terminated string.
- **`net`** — TCP/UDP sockets over a **libc-only FFI shim** (`net_shim.c`, POSIX sockets;
  no `deps`, nothing to install). `listen`/`accept`/`connect`/`port_of`/`write`/`read`/
  `close_fd` and `udp_bind`/`udp_send`/`udp_read`; fds are `int` (negative = failure),
  payloads are binary-safe `bytes`.
- **`httpd`** — a minimal HTTP/1.1 **server** toolkit over `core:net` (no external
  dependency — net is libc-only). The request/response plumbing is pure Tycho; you own the
  accept loop. `parse_request(raw) -> Request` (method/path/version, case-insensitive
  `header(r, name)`, honors Content-Length; `method == ""` on a malformed line);
  `response(status, body)` / `with_header(r, k, v)` / `render(r)` (Content-Length and a
  default `text/plain` Content-Type are added automatically); and the socket glue
  `read_request(fd)` (reads until the header terminator, then exactly Content-Length body
  bytes, bounded so a hostile peer can't spin) / `write_response(fd, r)`. **Text bodies** —
  they cross as tycho strings, the same interior-`0x00` limit `core:http` notes (fine for
  HTML/JSON/form APIs, not binary blobs). CRLF is built with `chr(13)` (tycho strings have no
  `\r` escape).
- **`cli`** — command-line argument parsing, pure string math. `parse(argv) -> Cli` (pass
  your arguments **without** `argv[0]`) sorts the vector into three buckets: `--key=value`
  **options**, boolean **flags** (`--flag`, and short clusters `-abc` → `a`/`b`/`c`), and
  **positionals** (everything else, plus everything after a bare `--`). Values always attach
  with `=`, so the parser needs no schema of which options take a value — `--verbose` is
  unambiguously a flag. Accessors: `get(c, key, default)`, `has(c, key)`, `flag(c, name)`
  (long or short), `positionals(c)`, `count(c)`. Uses parallel arrays, not maps.
- **`raster`** — pure-Tycho raster image codecs, **BMP** and **QOI**, with no external
  dependency (unlike `core:image`'s libpng-backed PNG). An `Image` is 8-bit RGBA (4
  bytes/pixel, row-major, top-to-bottom); pixel data is `bytes`. `encode_bmp`/`decode_bmp`
  (writes 32-bit BGRA, bottom-up, uncompressed; reads 24- or 32-bpp `BI_RGB`) and
  `encode_qoi`/`decode_qoi` (all six QOI chunk types, spec-exact 14-byte header and end
  marker). Both **round-trip losslessly**; the decoders **fail closed** to a 0×0 Image on a
  malformed / truncated / wrong-format input (check `width > 0`). Assembling the binary
  output is what the `to_bytes([int])` builtin enables — pure Tycho otherwise can't build a
  `bytes` with an interior `0x00` (a black or transparent pixel is `0x00` components).

## C-shim (FFI-backed) modules

A core module can wrap a C library via FFI. Drop a `<module>/<module>_shim.c` next to
the `.ty`; the transpiler auto-compiles and links it on `import "core:<module>"` (no
`--shim` needed). The `.ty` declares the shim's functions with `extern fn` (and
`extern "Lib" fn` auto-adds `-lLib` for an external library; `core:regex` needs none —
POSIX regex is in libc). Opaque handles cross as `ptr` (carried by value, never
dereferenced or arena-managed — `null` / `is_null`). Both transpilers link a module's shim,
so `tychoc`, `tychoc0 --bundle`, and standalone `tychoc0` all build C-shim modules.

### External dependencies (C-shim `deps`)

A shim that needs a system library beyond libc — one with headers to find and a link
flag that **varies by platform** — declares it in a `corelib/<module>/deps` manifest:
pkg-config package names, one per line (`#` comments and blank lines ignored). For
example `corelib/http/deps` is just `libcurl`.

The `deps`-backed modules are `http` (libcurl) and `crypto` (libcrypto), documented above,
plus three more:

- **`compress`** — gzip (RFC 1952) compress/decompress over **zlib** (`deps: zlib`);
  `bytes -> bytes`, fail-closed on corrupt or truncated input.
- **`image`** — PNG decode/encode over **libpng** (`deps: libpng`): `decode(bytes) ->
  Image{width, height, pixels}` (8-bit RGBA) and `encode(Image) -> bytes`; fail-closed on a
  non-PNG. JPEG is a demand-gated follow-up.
- **`tls`** — a TLS 1.2/1.3 client over **OpenSSL** (`deps: openssl`). Secure by default:
  `connect(host, port)` verifies the certificate against the system CA store, checks the
  hostname, and sends SNI; failure returns a null handle (never a silent insecure
  connection). `write`/`read`/`close_conn` over the encrypted stream.

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

`make corelib` (→ `corelib/run.sh`): every `corelib/test/<name>/main.ty` is built three
ways — by the C transpiler, via `tychoc --bundle | tychoc0`, and via **standalone** `tychoc0`
(which resolves `core:` itself) — and all three must produce the golden
`corelib/test/<name>.out`. Re-record goldens with `RECORD=1 sh corelib/run.sh`. Part of
`make ci`.

## Examples

Every module also has a small, readable **usage example** at
`examples/corelib/<name>/main.ty` — usage as documentation (idiomatic calls, human-friendly
output), as opposed to the assertion-style tests above. `make corelib-examples`
(→ `examples/corelib/run.sh`) validates them the same three ways against
`examples/corelib/<name>.out`, with the same dependency skip, and is part of `make ci`.
