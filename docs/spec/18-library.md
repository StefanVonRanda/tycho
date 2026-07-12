# 31. The library contract · 32. Core-tier packages · 33. Extended-tier packages

This part specifies the **corelib** — Tycho's standard library, a set of
packages under `corelib/` imported through the `core:` collection root
([§28](15-program.md)). It is **normative but tiered**: §31 states
the contract every corelib package satisfies and defines the two conformance
tiers of [§1.3](00-conventions.md#13-conformance); §32 catalogs the core-tier
packages a conforming implementation MUST provide; §33 catalogs the extended-tier
packages it MAY omit. This is a **catalog** — each subsection gives a package's
purpose, its realization kind, and its key exported functions and types. Full
per-function API semantics (argument-by-argument behavior, edge cases, error
values) are **deferred**; until they are written, the
[reference pages](../reference/index.md) and each package's source and golden
tests are the authoritative description of a function's exact behavior.

> Provenance: `docs/corelib.md`; package sources under `corelib/`; tiers
> [§1.3](00-conventions.md#13-conformance); shim/`deps` mechanics
> `src/tychoc.c` (`add_pkg_deps`), `corelib/run.sh`.

## 31. The library contract

### 31.1 The allocation contract

Every corelib function obeys the same value-semantic boundary as any Tycho call
([§9](07-memory-model.md)): it **allocates its results into the caller's arena**
and returns independent values, never aliasing the caller's inputs and never
retaining a reference to them past the call. A corelib call is, for the purposes
of the memory model, an ordinary function call; corelib introduces no hidden
global state, no shared mutable buffers, and no lifetime escaping the caller's
arena. Where a package threads mutable state (for example a PRNG seed), it does
so through an explicit `inout` parameter at the call site, not through a global
([§11](07-memory-model.md#11-inout)).

> Provenance: `docs/corelib.md:4-8`.

The one deliberate exception is a **C-owned opaque handle** returned as `ptr`
(§31.3): such a handle is *not* arena-managed and MUST be released by the
package's stated free function (for example `regex.release`, `http.release`,
`image` frees internally). The `ptr` itself is carried by value and is never
dereferenced by Tycho ([§24](14-ffi.md), forthcoming).

### 31.2 Conformance tiers

The corelib is partitioned by [§1.3](00-conventions.md#13-conformance) into two
tiers, determined by what each package depends on:

- **Core tier.** Packages that are **pure Tycho** or depend only on the **C
  standard library** (a libc-only shim; §31.3). A conforming implementation
  **MUST** provide every core-tier package (§32). Core-tier packages are the ones
  the reference test harness builds and runs unconditionally.
- **Extended tier.** Packages whose shim depends on an **external C library** via
  `pkg-config`, declared in a `corelib/<pkg>/deps` manifest (§33). An
  implementation **MAY** omit the extended tier and still conform at the core
  tier. A program that imports an absent extended package **MUST** be diagnosed
  before producing an executable — never silently mis-linked. The reference
  harness (`corelib/run.sh`) probes each `deps` with `pkg-config --exists` and
  **skips** (does not fail) a package's test when its C library is absent.

A package's tier is mechanically identifiable from its directory: a `deps` file
present ⇒ extended tier; a `<pkg>_shim.c` with no `deps` ⇒ libc-only shim, core
tier; neither ⇒ pure Tycho, core tier.

### 31.3 Realization kinds

A corelib package is realized in exactly one of three ways. The kind is stated
for each package in §32–§33.

1. **Pure Tycho** — no shim, no FFI. The package is ordinary Tycho source
   (`math`, `json`, `sha256`, `bignum`, …).
2. **Libc-only C shim** — a sibling `<pkg>_shim.c` the transpiler auto-compiles
   and links on `import "core:<pkg>"`, calling only libc (POSIX). No `deps`
   manifest, nothing external to install. The `.ty` declares the shim's entry
   points with `extern fn` (`regex`, `os`, `net`, and `datetime`'s timezone
   helpers). **Core tier.**
3. **pkg-config `deps`** — a `<pkg>_shim.c` plus a `corelib/<pkg>/deps` manifest
   naming pkg-config packages, one per line. On import the transpiler runs
   `pkg-config --cflags --libs <name>` for each and splices the result onto the
   cc line (`add_pkg_deps`), so the include path and link flags resolve per
   platform. **Extended tier.**

The `core:<pkg>` resolution, the auto-compile/link of shims, the `extern "Lib" fn`
bare-`-lLib` form, and the `deps` skip-don't-fail policy are specified in
[§28](15-program.md); this part references them without restating
the mechanism.

## 32. Core-tier packages

Each subsection gives: **realization kind** and the **key exported
functions/types**. Unless noted, a package is pure Tycho and every function
returns a fresh value (value semantics; §31.1).

### 32.1 `math`

Scalar math (pure Tycho). Generic `min`/`max`/`clamp(x, lo, hi)` over any
comparable type, `sign(x)` over any numeric (returns int −1/0/1); integer-specific
`abs`, `gcd`, `ipow(base, exp)`. (`sqrt`/`pow`/`floor`/`fabs` are float builtins,
not here.) `docs/corelib.md:55-58`.

### 32.2 `fmath`

Float-only helpers (pure Tycho): constant-valued nullary functions `pi()`, `e()`; `round` (half away from
zero), `trunc`, `lerp(a, b, t)`, `approx_eq(a, b, eps)`. No trig (no libm sin/cos
builtin). `docs/corelib.md:59-61`.

### 32.3 `char`

Byte/char classification and conversion over the int byte values `s[i]`/`chr`
produce (pure Tycho): `is_digit`, `is_alpha`, `is_alnum`, `is_upper`, `is_lower`,
`is_space`, `is_hex`; `to_upper`/`to_lower` (one byte); `digit_val` (−1 if not a
digit), `hex_val` (0..15 or −1). The lexer/parser workhorse. `docs/corelib.md:62-65`.

### 32.4 `strings`

String utilities (pure Tycho): `to_upper`, `to_lower`, `starts_with`,
`ends_with`, `contains`, `repeat(s, n)`, `trim`, `parse_int`, `is_space`,
`lines`, `replace`, `count(s, sub)`, `strip_prefix`/`strip_suffix`,
`pad_left`/`pad_right(s, width, pad)`, `reverse`, `capitalize`,
`split_once(s, sep) -> (before, after)`. (`split`/`find`/`substr`/`len`/`chr` are
builtins.) `docs/corelib.md:66-72`.

### 32.5 `path`

POSIX path string math, no filesystem access (pure Tycho): `base`, `dir`, `ext`,
`stem`, `join(a, b)`, `is_abs`, `split_path(p) -> (dir, base)` (inverse of
`join`), and `clean` (lexical normalize). `docs/corelib.md:73-80`.

### 32.6 `arrays`

Generic utilities over any element type `[T]` (pure Tycho), all returning a new
array: `contains`, `index_of`, `count`, `reverse`, `take`, `drop`, `concat`,
`fill(n, v)`, `dedup`; `sort`, `is_sorted`, `min`, `max` (`where comparable`);
`sum`, `product` (`where numeric`); `join(xs, sep)` (element convertible to
string). (`push`/`pop`/`len`/`range` are builtins.) `docs/corelib.md:81-88`.

### 32.7 `iter`

Generic higher-order helpers over any `[T]`, each taking a `fn`/closure (pure
Tycho): `map`, `filter`, `reduce`, `count`, `any`. `docs/corelib.md:89-90`.

### 32.8 `sort`

Stable index-permutation sorting (pure Tycho): `argsort(keys)` /
`argsort_desc(keys)` return the permutation ordering any comparable key type;
`by_key(xs, key)` sorts by a derived int key (a `fn`/closure).
`docs/corelib.md:91-95`.

### 32.9 `rand`

Deterministic (non-cryptographic) xorshift32 (pure Tycho); state is an explicit
int threaded via `inout`: `seed(n)`, `next(&st)` (`[1, 2^32)`), `below(&st, n)`
(`[0, n)`), `shuffle(&st, xs)` (Fisher-Yates, new array). `docs/corelib.md:96-101`.

### 32.10 `time`

Wraps the `clock()` (monotonic ns) and `now()` (UNIX seconds) builtins (pure
Tycho): stopwatch `start()`, `elapsed_ns`/`elapsed_us`/`elapsed_ms`; conversions
`ns_to_us`/`ns_to_ms`/`ns_to_s`; wall clock `unix_secs()`. `docs/corelib.md:102-106`.

### 32.11 `datetime`

Civil (proleptic Gregorian) calendar math over UNIX timestamps (Howard Hinnant's
`days_from_civil`/`civil_from_days`). A `DateTime` struct (UTC). `from_unix(secs)`
/ `to_unix(dt)`; the day-count core; `weekday(y,m,d)`, `is_leap`, `days_in_month`;
`now_utc()`; formatting `format_iso`, `weekday_name`, `month_name`, `pad2`/`pad4`.
The calendar core is pure Tycho, but the package now carries a **libc-only shim**
(`datetime_shim.c`) exposing DST-aware timezone offsets: `dtx_local_offset(secs)`
and `dtx_offset_at(tz, secs)`. Core tier. `docs/corelib.md:107-115`; shim
`corelib/datetime/datetime_shim.c`.

> Editor's note: `docs/corelib.md` describes `datetime` as fully pure; the
> `datetime_shim.c` timezone helpers (`dtx_local_offset`, `dtx_offset_at`)
> post-date that text. The doc drift is logged for [Appendix H](appendix-h-differences.md).

### 32.12 `json`

Recursive-descent JSON parser + serializer (pure Tycho). Value-semantic `Json`
enum (`JNull`/`JBool`/`JNum`/`JStr`/`JArr`/`JObj`). `parse(s) -> Json` (fails
closed to `JNull`), `stringify(j) -> string` (compact); typed queries `kind`,
`get(j, key)`, `at(j, i)`, `keys`, `len_of`, `as_num`/`as_str`/`as_bool`.
Variants are constructible cross-package (`json.JNum(1)`). Scope: integers only,
four common escapes. `docs/corelib.md:129-138`.

### 32.13 `csv`

RFC 4180 CSV parser + serializer (pure Tycho); a document is `[[string]]`.
`parse(s) -> [[string]]` (small state machine; quoted fields, `""` escape,
LF/CRLF/lone-CR; fails closed), `stringify(rows) -> string` (round-trips
`parse`); `parse_delim`/`stringify_delim` for an arbitrary single-byte delimiter
(TSV = `parse_delim(s, 9)`); `get(rows, r, c)` (bounds-safe). `docs/corelib.md:139-147`.

### 32.14 `base64`

Base64 (RFC 4648) `encode`/`decode`, plus `encode_url` (URL-safe alphabet, no
padding) (pure Tycho, no bit-operators). `decode` is lenient (skips non-alphabet
bytes). Byte-safety caveat: `decode` silently drops interior `0x00`.
`docs/corelib.md:148-156`.

### 32.15 `hex`

Hexadecimal `encode` (lowercase) / `encode_upper` / `decode`, plus `is_valid`
(strict) (pure Tycho). `decode` is lenient (skips non-hex bytes), reuses
`char.hex_val`. Same `0x00` decode caveat as `base64`. `docs/corelib.md:157-160`.

### 32.16 `url`

URL percent-encoding (RFC 3986) (pure Tycho): component-style `encode` (like
`encodeURIComponent`), `encode_form`/`decode_form` (`x-www-form-urlencoded`,
space ↔ `+`), and `decode` (`%XX`, leaving `+`; lenient). Reuses `char.hex_val`.
Same `0x00` decode caveat. `docs/corelib.md:161-166`.

### 32.17 `uuid`

Random version-4 UUIDs (RFC 4122) (pure Tycho): `v4(&st)` (draws 16 bytes from
`core:rand`, state via `inout`), `nil()`, `parse(s) -> [int]` (16 bytes, lenient),
`format(bytes) -> string` (canonical 8-4-4-4-12), `is_valid` (strict), `version`.
`docs/corelib.md:167-173`.

### 32.18 `hash`

Non-cryptographic 32-bit hashes — for hash tables / checksums / dedup, **not
security** (pure Tycho): `fnv1a_32`, `djb2`, `sdbm`, `crc32` (IEEE/zlib), plus
`to_hex`. All return a non-negative int in `[0, 2^32)`. `docs/corelib.md:174-180`.

### 32.19 `md5`

MD5 message-digest (RFC 1321) (pure 32-bit-arithmetic Tycho): `hex(s)` (32-char)
and `digest(s)` (16 raw bytes). **Broken for security** — checksums/interop only.
Bit-exact against RFC 1321 vectors. `docs/corelib.md:181-186`.

### 32.20 `sha256`

SHA-256 (FIPS 180-4) (pure 32-bit-arithmetic Tycho): `hex(s)` (64-char) and
`digest(s)` (32 raw bytes). A real cryptographic digest (not a standalone
password hash — use a KDF). Bit-exact against NIST vectors. `docs/corelib.md:187-191`.

### 32.21 `io`

Filesystem helpers over the `read_file`/`write_file`/`list_dir` builtins (pure
Tycho; composes `core:strings` and `core:path`): `read(p)`, `write(p, s)`,
`append(p, s)`, `read_lines(p)`/`write_lines(p, lines)`, `list(p)`, `exists(p)`.
Nothing aborts; the error model mirrors the builtins. `docs/corelib.md:204-210`.

### 32.22 `os`

Run external commands via a **libc-only shim** (`os_shim.c`, `popen`/`system`; no
`deps`). Core tier. `system(cmd)` runs `cmd` through `/bin/sh -c` returning its
exit code; `run(cmd)` also captures stdout into `Output{code, out}`. `cmd` is a
shell line — quote untrusted input yourself. `docs/corelib.md:211-217`.

### 32.23 `regex`

POSIX extended regular expressions (ERE) via a **libc-only shim** (`regex_shim.c`
over `<regex.h>`; no `deps`). Core tier. `compile(pat) -> ptr` (opaque handle;
`ok`/`is_null`), `is_match`, `find`/`find_end` (offset or −1), `matched` (first
match), `release` (the pattern is C-malloc'd, **not** arena-managed — release when
done; §31.1). `docs/corelib.md:116-120`.

### 32.24 `net`

TCP and UDP sockets via a **libc-only shim** (`net_shim.c`, pure POSIX sockets; no
`deps`, same self-contained model as `core:os`). Core tier. A socket is a file
descriptor (`int`); every open/move call returns a **negative int on failure**;
payloads are `bytes` (binary-safe). TCP: `listen(host, port)` (port 0 ⇒
ephemeral), `accept(fd)`, `connect(host, port)`, `port_of(fd)`, `write(fd, data)`,
`read(fd, max)`, `close_fd(fd)`. UDP: `udp_bind(host, port)`,
`udp_send(fd, host, port, data)`, `udp_read(fd, max)`. Source
`corelib/net/net.ty`, `corelib/net/net_shim.c`.

> Editor's note: `core:net` is not described in `docs/corelib.md`; this section
> is authored from the package source. Logged for [Appendix H](appendix-h-differences.md).

### 32.25 `bignum`

Arbitrary-precision integers (pure Tycho, no FFI, no floats). A `Big` is a sign
plus a little-endian array of base-10^9 limbs; every op returns a fresh `Big`
(value semantics, arena-managed). Constructors `zero`, `from_int(n)`,
`from_str(s)`; predicates/queries `is_zero`, `cmp(a, b)`, `neg`, `abs`; arithmetic
`add`, `sub`, `mul`, `divmod(a, b) -> (Big, Big)`, `div`, `mod`, `pow(a, e)`;
conversions `to_str`, `to_int`. Source `corelib/bignum/bignum.ty`.

> Editor's note: `core:bignum` is not described in `docs/corelib.md`; this section
> is authored from the package source. Logged for [Appendix H](appendix-h-differences.md).

### 32.26 `decimal`

Arbitrary-precision base-10 fixed-point (pure Tycho; built on `core:bignum`). A
`Decimal` is an integer coefficient (`bignum.Big`) times `10^(-scale)`, so it is
base-10 exact (`0.1 + 0.2 == 0.3`) — the right type for money. `from_int(n)`,
`from_str(s)`; `neg`, `abs`, `is_zero`; `add`, `sub`, `mul`, `cmp` (exact,
scale-preserving); `rescale(a, newscale)` (truncates toward zero); `to_str`.
Division is deferred (needs a target scale + rounding policy). Source
`corelib/decimal/decimal.ty`.

> Editor's note: `core:decimal` is not described in `docs/corelib.md`; this
> section is authored from the package source. Logged for [Appendix H](appendix-h-differences.md).

## 33. Extended-tier packages

Each package below declares a `corelib/<pkg>/deps` manifest naming a pkg-config
library (§31.3). A conforming implementation **MAY** omit any of them; a program
importing an absent one **MUST** be diagnosed (§31.2). Every value still crosses
the FFI boundary into the caller's arena (§31.1).

### 33.1 `http` — libcurl

An HTTP(S) client over **libcurl** (`deps` → `libcurl`). `get(url) -> ptr` /
`post(url, body, content_type) -> ptr` return an opaque, **C-owned** response
handle (or null on transport failure; `ok`/`is_null`); `status(r)`, `body(r)`
(arena-copied), `release(r)`. Convenience `get_body(url)` / `get_status(url)` do
the request and free the handle. The arena-copied body truncates at an interior
`0x00` (text APIs). `docs/corelib.md:121-128`.

### 33.2 `crypto` — libcrypto (OpenSSL)

The security-grade module, a shim over OpenSSL **libcrypto** (`deps` →
`libcrypto`). Reach for it when an attacker is in the threat model (vs the pure
`sha256`/`md5` for non-adversarial integrity): CSPRNG (`random_hex`),
`sha256`/`sha512` of binary, `hmac_sha256`, `pbkdf2_sha256`, constant-time
`ct_equal`, ChaCha20-Poly1305 AEAD (`aead_encrypt`/`aead_decrypt`, `"!err"` on
auth failure), Ed25519 (`ed25519_pubkey`/`sign`/`verify`), X25519
(`x25519_pubkey`/`shared`). Every value crosses as lowercase hex (a Tycho string
can't hold `0x00`; use `core:hex` to convert text). Bound to OpenSSL rather than
reimplemented because real crypto must be constant-time and audited.
`docs/corelib.md:192-203`.

### 33.3 `compress` — zlib

gzip (RFC 1952) compression over **zlib** (`deps` → `zlib`). `compress(data) ->
bytes` (gzip-compress), `decompress(data) -> bytes` (inflate a gzip *or* zlib
stream; empty bytes on corrupt/truncated input — fails closed). Binary-safe
(`bytes`, interior NUL preserved). Source `corelib/compress/compress.ty`,
`corelib/compress/deps`.

> Editor's note: `core:compress` is not described in `docs/corelib.md`; authored
> from the package source. Logged for [Appendix H](appendix-h-differences.md).

### 33.4 `image` — libpng

PNG decode/encode over **libpng** ≥ 1.6 (simplified `png_image` API; `deps` →
`libpng`). Images are 8-bit RGBA, row-major, top-to-bottom; a value-semantic
`Image` struct (`width`, `height`, `pixels: bytes`). `decode(png_bytes) -> Image`
(a 0×0 Image on any error — check `width > 0`), `encode(img) -> bytes` (empty on
a bad image), `make(w, h, pixels) -> Image`. The libpng decode handle is freed
internally (not exposed). Source `corelib/image/image.ty`, `corelib/image/deps`.

> Editor's note: `core:image` is not described in `docs/corelib.md`; authored
> from the package source. Logged for [Appendix H](appendix-h-differences.md).

### 33.5 `tls` — openssl

A TLS 1.2/1.3 **client** over **OpenSSL** (`deps` → `openssl` ⇒ `-lssl
-lcrypto`). Secure by default: `connect` verifies the server certificate against
the system CA store, checks the hostname, and sends SNI; a failed
resolve/connect/handshake/verify returns a **null** handle (fail closed — guard
with `is_null`/`tls.ok`). `connect(host, port) -> ptr`, `write(conn, data) -> int`
(bytes sent or −1), `read(conn, max) -> bytes` (empty on close/error),
`close_conn(conn)` (safe on null), `ok(conn) -> bool`. Payloads are binary-safe
`bytes`. Source `corelib/tls/tls.ty`, `corelib/tls/deps`.

> Editor's note: `core:tls` is not described in `docs/corelib.md`; authored from
> the package source. Logged for [Appendix H](appendix-h-differences.md).
