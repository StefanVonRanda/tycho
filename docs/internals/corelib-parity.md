# Odin core parity

Target: **capability parity with Odin's `core` package tree**, adapted to
Tycho's value semantics and implicit arenas. Not line-for-line, not
API-for-API. Odin's `vendor/` tree is a separate ecosystem and out of scope.

Reference: Odin `dev-2026-11a` (`core` is 40 packages, 1,433 files, ~609k
lines). The current Tycho corelib is 66 packages, ~13.3k lines, 824 locked
public functions.

Status values:

- **covered** — the capability exists and is usable.
- **partial** — a useful subset exists; the named gap is real.
- **missing** — no equivalent.
- **non-goal** — deliberately excluded by the language design.

## Matrix

| Odin core | Status | Tycho today | Plan |
|---|---|---|---|
| `bufio` | partial | `io.LineReader`, `io.read_lines`; no reusable buffered reader/writer or token scanner | `bufio`-style buffered reader/writer and scanner in the text stage |
| `bytes` | partial | `bytes` builtin is immutable; `[int]` + `push` or concatenation build buffers | add a mutable buffer type only where a measured caller needs it |
| `c` | non-goal | `extern fn` FFI plus internal `*_shim.c` files | none |
| `compress` | partial | gzip/zlib via zlib: `compress`, `decompress`, `raw_compress`, `raw_decompress` | split gzip/zlib framing, add LZ4 and a streaming API |
| `container` | partial | `Deque`/`Queue`, `PriorityQueue`, `BitArray`, `Lru`; `pool` covers generational handles | ordered map/tree, small array, topological sort |
| `crypto` | partial | OpenSSL shims: ChaCha20-Poly1305 and AES-GCM AEAD, AES-CTR, RSA PKCS#1 v1.5, Ed25519, X25519, SHA-256/512, SHA-3/SHAKE, BLAKE2, HMAC-SHA256/512, PBKDF2, HKDF, CSPRNG; ASN.1 DER/BER in its own package | AES-CBC, ECDSA, Argon2id, X.509; post-quantum only if dependency policy allows |
| `debug` | partial | runtime traps and `tycho-debug bt`; no library API | `trace`, stack-trace capture, assertion helpers |
| `diff` | covered | `diff.lines` and `diff.unified` (LCS, bounded fallback) | none |
| `dynlib` | missing | none | `dlopen`/`LoadLibrary` shim |
| `encoding` | partial | `base64`, `base32`, `hex`, `json`, `csv`, `uuid`, `pem`, `ini`, `varint`, `endian`, `xml`, `cbor`, `asn1` | entity tables, hxa |
| `flags` | partial | `cli` parses argv, but with no declarative flag set | decide whether to extend `cli` or add `flags` |
| `fmt` | partial | `str`, `f"..."` interpolation, `strings.format_g17`; no general format-spec API | add width/base/precision formatting where interpolation is not enough |
| `hash` | partial | FNV-1a, djb2, sdbm, CRC32 | add xxHash (Odin's only `core:hash` package) |
| `image` | partial | `image` is PNG; `raster` is BMP/QOI | JPEG, TGA, Netpbm, BNG |
| `io` | covered | file read/write/append/copy/stat/dirs, checked variants, stdin/stdout, line reader | no generic `Reader`/`Writer` interface; concrete values are the design |
| `log` | covered | leveled logger, stderr/file choice, `new`/`line` | none |
| `math` | partial | `math`, `fmath` (trig/exp/log/hypot), `decimal`, `bignum`, `rand` | linear algebra, complex, fixed-point, easing, noise, bit operations |
| `mem` | non-goal | implicit arenas; no public allocator API | none — explicit allocators, virtual memory and TLSF are a different memory model |
| `nbio` | missing | `net.wait_readable`/`accept_wait` cover small poll loops | epoll/kqueue/IOCP wrapper |
| `net` | partial | TCP/UDP, timeouts, TLS, HTTP/HTTPD; DNS `resolve`; reuse-addr/nodelay/keepalive/broadcast/nonblocking options | interface enumeration, Unix sockets, address parsing helpers |
| `odin` | non-goal | Tycho has its own compiler packages | none |
| `os` | partial | process `system`/`run`/`exec`/`exec_capture`; `env`/`set_env`/`unset_env`, `cwd`/`chdir`, `pid`, `hostname`, `home_dir`, `temp_dir`; `io.walk` | permissions, users/groups, process signals |
| `path` | partial | POSIX/slash path: clean, join, split, ext, relative, real, containment checks | Windows `filepath` semantics; native paths currently live in `io` |
| `prof` | missing | none | sampling profiler and trace output |
| `reflect` | non-goal | compile-time `typeof`, generics, `size_of$`; no runtime type metadata | keep runtime reflection out unless metadata is added deliberately |
| `relative` | non-goal | no references by decision | none |
| `rexcode` | non-goal | Odin's RISC-V toolchain, not a library capability | none |
| `simd` | partial | language vector types; no intrinsic package | expose intrinsics only with a measured codegen need |
| `slice` | non-goal | `[T]` is the slice; no separate heap-slice type | none |
| `sort` | covered | `sort` with generic ordering | none |
| `strconv` | partial | decimal/base 2..36 parse and format, fixed float with 0..17 digits, `format_g17`, builtin `str` | widths, float32-specific forms |
| `strings` | covered | case, trim, split, find, replace, pad, reverse, escapes, parsing | add a builder if measured concatenation cost demands it |
| `sync` | partial | C11 atomics (int/bool/pointer), `Mutex`, `RwLock`, `Cond`, `Semaphore`, `WaitGroup`, `Once`; channels are language builtins | `Barrier`, recursive/ticket mutexes |
| `sys` | non-goal | libc and shims are the portability boundary | none |
| `terminal` | partial | ANSI escapes, tty/size, raw mode, byte read | key decoding, cursor queries, non-console Windows fallbacks |
| `testing` | partial | `T`, `check`, `eq`, `report`; no subtests, benchmarks or fuzz hooks | expand after `container`/`sync` |
| `text` | partial | `strings`, `utf8`, `utf16`, `char`, `regex`, `edit` | scanner, i18n, table, pattern matching |
| `thread` | partial | language `spawn`/`wait`, channels, `parallel for`; no raw thread handle or TLS | keep tasks as the model; add `Cond`/`Semaphore` before raw threads |
| `time` | covered | monotonic stopwatch, unix time, sleep, civil date, ISO/CLF parse and format, local offset | none |
| `unicode` | partial | `utf8` validation and decoding; no UTF-16, case mapping or segmentation | add UTF-16, case tables, segmentation |

## What parity does not mean

- No general allocator parameter on corelib APIs. Arenas stay implicit, so
  Odin's `mem` allocator/virtual/TLSF surface is a non-goal rather than a gap.
- No references, `defer`, or manual `free` contract introduced to mirror Odin.
- No raw syscall surface. Platform depth arrives through shims with a
  documented portable API.
- No runtime type metadata solely to support `reflect`.
- No package added without golden tests, reference docs and a `surface.lock`
  record.

## Sequence

1. **Parity matrix** — done; this page is the record.
2. **Foundations** — done: `core:container` and `core:sync` with atomics.
3. **Platform** — partial: `os` gained environment, cwd, temp, identity and
   `io.walk`; `path` gained `relative`. `mem` is a non-goal, not deferred work.
4. **Data and text** — partial: `base32`, `varint`, `endian`, `pem`, `ini`,
   `utf16`, `edit`, and `strings` base/fixed-float formatting landed. XML,
   CBOR, ASN.1, a scanner, `fmt`, `bufio`, `diff`, and Unicode tables remain.
5. **Crypto** — partial: SHA-3/SHAKE, BLAKE2, HMAC-SHA512 and HKDF joined the
   existing ChaCha20-Poly1305/Ed25519/X25519/SHA-2/PBKDF2 surface. AES modes,
   RSA, ECDSA/ECDH, Argon2id, X.509 and post-quantum remain.
6. **Formats and media** — partial: TGA joined PNG/BMP/QOI. LZ4, JPEG, Netpbm,
   BNG and archive breadth remain.
7. **Runtime and diagnostics** — not started: `debug`, `prof`, `terminal`,
   `dynlib`, `nbio`, limited `simd`; `reflect` stays a non-goal.
8. **Continuous gates** — every package lands with `corelib/test/<pkg>`,
   `docs/reference/corelib.md` entries, and a deliberate `surface.lock`
   update. `make check-fast` per change; full CI at milestones.

Stages 2–6 are deliberately ordered so each lands as tested library surface
before the runtime work in stage 7.
