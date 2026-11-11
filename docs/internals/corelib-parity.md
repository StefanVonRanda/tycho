# Odin core parity

Target: **capability parity with Odin's `core` package tree**, adapted to
Tycho's value semantics and implicit arenas. Not line-for-line, not
API-for-API. Odin's `vendor/` tree is a separate ecosystem and out of scope.

Reference: Odin `dev-2026-11a` (`core` is 40 packages, 1,433 files, ~609k
lines). The current Tycho corelib is 88 packages, ~24.3k lines, 1235 locked
public functions.

Status values:

- **covered** — the capability exists and is usable.
- **partial** — a useful subset exists; the named gap is real.
- **missing** — no equivalent.
- **non-goal** — deliberately excluded by the language design.

## Matrix

| Odin core | Status | Tycho today | Plan |
|---|---|---|---|
| `bufio` | covered | `core:bufio` in-memory reader/writer plus `core:scanner` | none |
| `bytes` | covered | immutable `bytes` plus mutable `core:buffer` | none |
| `c` | non-goal | `extern fn` FFI plus internal `*_shim.c` files | none |
| `compress` | partial | gzip/zlib, raw DEFLATE, LZ4, xz, bzip2, PPMd7/PPMd8, PKZIP Shrink/Reduce/Implode, Deflate64 (ZIP method 9), shoco, delta, BCJ for x86/ARM/ARM Thumb/ARM64/PowerPC/SPARC/IA64/RISC-V, and swap2/swap4 | BCJ2 |
| `container` | covered | `Deque`/`Queue`, `PriorityQueue`, `BitArray`, `Lru`, ordered `Treap`/`Avl`/`RbTree`, `HandleMap`, `SmallArray`, `Xar`, `topo_sort` | intrusive containers need references (non-goal) |
| `crypto` | covered | OpenSSL shims: ChaCha20-Poly1305 and AES-GCM AEAD, AES-CTR, AES-CBC, RSA PKCS#1 v1.5, ECDSA P-256, Ed25519, X25519, ML-DSA, ML-KEM, SHA-256/512, SHA-3/SHAKE, BLAKE2, HMAC-SHA256/512, PBKDF2, HKDF, Argon2id, CSPRNG; ASN.1 DER/BER and X.509 in their own packages | none |
| `debug` | covered | `assert`, `trace`, `enabled`, `stack_trace`; runtime traps and `tycho-debug bt` | PE introspection is platform-specific tooling |
| `diff` | covered | `diff.lines` and `diff.unified` (LCS, bounded fallback) | none |
| `dynlib` | partial | `open`, `symbol`, `close_lib` over `dlopen`/`LoadLibrary` | function-pointer invocation (Tycho has no callbacks into Tycho) |
| `encoding` | partial | `base64`, `base32`, `hex`, `json`, `csv`, `uuid`, `pem`, `ini`, `varint`, `endian`, `xml`, `cbor`, `asn1`, `entity` | hxa (3D asset format) |
| `flags` | covered | `cli.parse`/`parse_spec` and `cli.flag` | none |
| `fmt` | covered | `format_int`, `format_float`, `pad_left`/`pad_right` plus `str`/f-strings | none |
| `hash` | covered | FNV-1a 32/64, djb2, sdbm, CRC16/32/64, Adler-32, Murmur3 and xxHash 32/64 | none |
| `image` | partial | PNG encode/decode, baseline JPEG encode/decode, `raster` BMP/QOI/TGA/PPM/PGM/PBM | BNG |
| `io` | covered | file read/write/append/copy/stat/dirs, checked variants, stdin/stdout, line reader | no generic `Reader`/`Writer` interface; concrete values are the design |
| `log` | covered | leveled logger, stderr/file choice, `new`/`line` | none |
| `math` | covered | `math`, `fmath` (trig/exp/log/hypot), `decimal`, `bignum`, `rand`, `linalg`, `complex`, `fixed`, `ease`, `noise`, `bits` | none |
| `mem` | non-goal | implicit arenas; no public allocator API | none — explicit allocators, virtual memory and TLSF are a different memory model |
| `nbio` | partial | `nbio.poll` over `poll(2)`/`WSAPoll` with READ/WRITE masks | epoll/kqueue/IOCP completion loops |
| `net` | covered | TCP/UDP, timeouts, TLS, HTTP/HTTPD; DNS `resolve` plus MX/TXT/SRV parsing, UDP querying and TCP fallback; socket options; POSIX Unix sockets; `interfaces`; IPv4/IPv6/endpoint parse and format; prefix network/containment | none |
| `odin` | non-goal | Tycho has its own compiler packages | none |
| `os` | covered | process execution, environment, cwd/chdir, pid/ppid, hostname, home/temp/cache/config/data dirs, temp files/dirs, user name, pipes/fd I/O, `chmod`/`umask`, uid/gid, `io.walk` | process signals live in `core:signal` |
| `path` | covered | POSIX `path` plus Windows lexical `filepath` (clean/join/volume/base/dir/ext/stem) | native path opening still goes through `io` |
| `prof` | partial | manual `Span` timing and `report` | sampling profiler and trace output |
| `reflect` | non-goal | compile-time `typeof`, generics, `size_of$`; no runtime type metadata | keep runtime reflection out unless metadata is added deliberately |
| `relative` | non-goal | no references by decision | none |
| `rexcode` | non-goal | Odin's RISC-V toolchain, not a library capability | none |
| `simd` | non-goal | language `vector[N]T` arithmetic is the SIMD surface; architecture intrinsics are codegen, not a library package | none |
| `slice` | non-goal | `[T]` is the slice; no separate heap-slice type | none |
| `sort` | covered | `sort` with generic ordering | none |
| `strconv` | covered | decimal/base 2..36 parse and format, fixed float, widths via `fmt`, `format_g17`, builtin `str` | none |
| `strings` | covered | case, trim, split, fields, find, replace, pad, reverse, escapes, parsing | add a builder if measured concatenation cost demands it |
| `sync` | covered | C11 atomics (int/bool/pointer), `Mutex`, `RwLock`, `Cond`, `Semaphore`, `WaitGroup`, `Once`, `Barrier`, recursive mutex; channels are language builtins | ticket mutex (not a capability gap) |
| `sys` | non-goal | libc and shims are the portability boundary | none |
| `terminal` | partial | ANSI escapes, tty/size, raw mode, byte read, key decoding | cursor queries and non-console Windows fallbacks |
| `testing` | covered | `T`, `check`, `eq`, `report`, `subtest`/`merge`, `bench`, `report_bench` | none |
| `text` | partial | `strings`, `utf8`, `utf16`, `char`, `regex`, `textmatch`, `edit`, `scanner`, `table`, `glob`, GNU MO `i18n` | Qt `.qm` catalogs, locale data, Lua `%b`/`%f`/non-greedy `-` |
| `thread` | partial | language `spawn`/`wait`, channels, `parallel for`; `thread.id`/`yield_now`/`cpus` | raw thread creation/TLS need compiler/runtime support |
| `time` | partial | monotonic stopwatch, unix time, sleep, civil date, ISO/CLF parse and format, local offset, IANA TZif zones via `core:tz` including the POSIX footer | leap-second records |
| `unicode` | covered | `utf8`, `utf16`, full case mapping/folding, titlecase mapping, UAX #15 normalization, UAX #29 grapheme/word segmentation, UCD general-category/script/block/emoji classification | none |

## What parity does not mean

- No general allocator parameter on corelib APIs. Arenas stay implicit, so
  Odin's `mem` allocator/virtual/TLSF surface is a non-goal rather than a gap.
- No references, `defer`, or manual `free` contract introduced to mirror Odin.
- No raw syscall surface. Platform depth arrives through shims with a
  documented portable API.
- No runtime type metadata solely to support `reflect`.
- No package added without golden tests, reference docs and a `surface.lock`
  record.

## Platform targets

Odin's core also carries platform targets: `core/sys/android` (45 files, ~4k
lines) and `core/sys/darwin` (122 files, ~48k lines), with Android/Darwin
branches in `os`, `net`, `log`, `sync`, `thread` and `time`.

| Target | Status | Evidence / gap |
|---|---|---|
| Linux x86-64 | supported | full `make ci`, release archive |
| Windows x86-64 | supported | mingw build + Wine smoke, release archive |
| macOS arm64 | supported | darwin-arm64 `make ci`, release archive |
| Android | **missing** | no NDK target, JNI bridge, asset manager or APK packaging; `os`/`net` POSIX shims are untested there |
| iOS | **missing** | no Xcode target or Objective-C/Darwin bridge; `core/sys/darwin` equivalents are not bound |

Android/iOS support is a platform/toolchain project, not a package that can be
added to this tree without an NDK/Xcode verification path. `os.platform()` and
the `is_android`/`is_ios` predicates are present so code can branch, but they
do not make those targets build or pass.

## Residual partials

These are the remaining rows that are not complete capability parity. None of
them blocks the application-facing surface above:

- `dynlib` — Tycho has no callback/function-pointer call path, so symbols are
  opaque handles for C shims.
- `encoding` — HXA (Odin's 3D asset container) is not a general encoding.
- `image` — BNG (Odin's image format) is not implemented; PNG/JPEG/BMP/QOI/TGA/
  Netpbm are.
- `nbio` — `poll` is present; epoll/kqueue/IOCP completion loops are runtime work.
- `prof` — manual spans are present; sampling needs runtime hooks.
- `terminal` — cursor queries and some non-console Windows paths are absent.
- `text` — i18n/locale tables are not implemented.
- `thread` — raw threads/TLS need compiler/runtime callbacks.

## Sequence

1. **Parity matrix** — done; this page is the record.
2. **Foundations** — done: `core:container` and `core:sync` with atomics.
3. **Platform** — done: environment, cwd, identities, cache/config/data dirs,
   `io.walk`, `path.relative`, Unix sockets, interface enumeration.
4. **Data and text** — done: Base32, varint, endian, PEM, INI, UTF-16, edit,
   XML, CBOR, ASN.1, scanner, `fmt`, `bufio`, `diff`, entity decoding, full
   Unicode case mapping/folding, UAX #15 normalization and UAX #29
   grapheme/word segmentation. i18n tables remain a residual partial.
5. **Crypto** — done: SHA-3/SHAKE, BLAKE2, HMAC-SHA512, HKDF, AES-GCM/CTR/CBC,
   RSA, ECDSA, Ed25519, X25519, ML-DSA, ML-KEM, Argon2id and X.509.
6. **Formats and media** — done: TGA, Netpbm PPM/PGM/PBM, LZ4 and baseline
   JPEG encode/decode joined PNG/BMP/QOI. BNG remains a residual partial.
7. **Runtime and diagnostics** — done: `debug`, manual `prof` spans, `terminal`,
   `dynlib`, `nbio.poll`, `thread.id`/`yield_now`; raw SIMD intrinsics stay a
   non-goal because `vector[N]T` is the language surface.
8. **Continuous gates** — every package lands with `corelib/test/<pkg>`,
   `docs/reference/corelib.md` entries, and a deliberate `surface.lock`
   update. `make check-fast` per change; full CI at milestones.
