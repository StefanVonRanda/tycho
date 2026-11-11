# Odin core parity

Target: **capability parity with Odin's `core` package tree**, adapted to
Tycho's value semantics and implicit arenas. Not line-for-line, not
API-for-API. Odin's `vendor/` tree is a separate ecosystem and out of scope.

Reference: Odin `dev-2026-11a` (`core` is 40 packages, 1,433 files, ~609k
lines). The current Tycho corelib is 84 packages, ~16.1k lines, 1036 locked
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
| `bytes` | covered | immutable `bytes` plus mutable `core:buffer` | none |
| `c` | non-goal | `extern fn` FFI plus internal `*_shim.c` files | none |
| `compress` | covered | gzip/zlib one-shot and streaming, raw DEFLATE, LZ4 block | shoco (not a capability gap) |
| `container` | partial | `Deque`/`Queue`, `PriorityQueue`, `BitArray`, `Lru`, ordered `Treap`, `topo_sort`; `pool` covers generational handles | small array (fixed arrays cover it) |
| `crypto` | partial | OpenSSL shims: ChaCha20-Poly1305 and AES-GCM AEAD, AES-CTR, AES-CBC, RSA PKCS#1 v1.5, ECDSA P-256, Ed25519, X25519, SHA-256/512, SHA-3/SHAKE, BLAKE2, HMAC-SHA256/512, PBKDF2, HKDF, Argon2id, CSPRNG; ASN.1 DER/BER and X.509 parsing/verification in their own packages | post-quantum algorithms (ML-KEM/ML-DSA) only if the OpenSSL provider exposes them |
| `debug` | partial | `assert`, `trace`, `enabled`, `stack_trace`; runtime traps and `tycho-debug bt` | PE introspection |
| `diff` | covered | `diff.lines` and `diff.unified` (LCS, bounded fallback) | none |
| `dynlib` | partial | `open`, `symbol`, `close_lib` over `dlopen`/`LoadLibrary` | function-pointer invocation (Tycho has no callbacks into Tycho) |
| `encoding` | partial | `base64`, `base32`, `hex`, `json`, `csv`, `uuid`, `pem`, `ini`, `varint`, `endian`, `xml`, `cbor`, `asn1`, `entity` | hxa (3D asset format) |
| `flags` | partial | `cli` parses argv, but with no declarative flag set | decide whether to extend `cli` or add `flags` |
| `fmt` | covered | `format_int`, `format_float`, `pad_left`/`pad_right` plus `str`/f-strings | none |
| `hash` | covered | FNV-1a, djb2, sdbm, CRC32 and xxHash 32/64 | none |
| `image` | partial | PNG encode/decode, baseline JPEG encode/decode, `raster` BMP/QOI/TGA/PPM/PGM/PBM | BNG |
| `io` | covered | file read/write/append/copy/stat/dirs, checked variants, stdin/stdout, line reader | no generic `Reader`/`Writer` interface; concrete values are the design |
| `log` | covered | leveled logger, stderr/file choice, `new`/`line` | none |
| `math` | partial | `math`, `fmath` (trig/exp/log/hypot), `decimal`, `bignum`, `rand` | linear algebra, complex, fixed-point, easing, noise, bit operations |
| `mem` | non-goal | implicit arenas; no public allocator API | none — explicit allocators, virtual memory and TLSF are a different memory model |
| `nbio` | partial | `nbio.poll` over `poll(2)`/`WSAPoll` with READ/WRITE masks | epoll/kqueue/IOCP completion loops |
| `net` | covered | TCP/UDP, timeouts, TLS, HTTP/HTTPD; DNS `resolve`; socket options; POSIX Unix sockets; `interfaces` | address parsing helpers (callers use `resolve`) |
| `odin` | non-goal | Tycho has its own compiler packages | none |
| `os` | covered | process execution, environment, cwd/chdir, pid/ppid, hostname, home/temp/cache/config/data dirs, `chmod`/`umask`, uid/gid, `io.walk` | process signals live in `core:signal` |
| `path` | covered | POSIX `path` plus Windows lexical `filepath` (clean/join/volume/base/dir/ext/stem) | native path opening still goes through `io` |
| `prof` | partial | manual `Span` timing and `report` | sampling profiler and trace output |
| `reflect` | non-goal | compile-time `typeof`, generics, `size_of$`; no runtime type metadata | keep runtime reflection out unless metadata is added deliberately |
| `relative` | non-goal | no references by decision | none |
| `rexcode` | non-goal | Odin's RISC-V toolchain, not a library capability | none |
| `simd` | partial | language vector types; no intrinsic package | expose intrinsics only with a measured codegen need |
| `slice` | non-goal | `[T]` is the slice; no separate heap-slice type | none |
| `sort` | covered | `sort` with generic ordering | none |
| `strconv` | partial | decimal/base 2..36 parse and format, fixed float with 0..17 digits, `format_g17`, builtin `str` | widths, float32-specific forms |
| `strings` | covered | case, trim, split, find, replace, pad, reverse, escapes, parsing | add a builder if measured concatenation cost demands it |
| `sync` | covered | C11 atomics (int/bool/pointer), `Mutex`, `RwLock`, `Cond`, `Semaphore`, `WaitGroup`, `Once`, `Barrier`, recursive mutex; channels are language builtins | ticket mutex (not a capability gap) |
| `sys` | non-goal | libc and shims are the portability boundary | none |
| `terminal` | partial | ANSI escapes, tty/size, raw mode, byte read, key decoding | cursor queries and non-console Windows fallbacks |
| `testing` | partial | `T`, `check`, `eq`, `report`, `subtest`/`merge`, `bench`, `report_bench` | fuzz hooks |
| `text` | partial | `strings`, `utf8`, `utf16`, `char`, `regex`, `edit`, `scanner`, `table`, `glob` | i18n tables and locale data |
| `thread` | partial | language `spawn`/`wait`, channels, `parallel for`; `thread.id`/`yield_now`/`cpus` | raw thread creation/TLS need callbacks Tycho does not support |
| `time` | covered | monotonic stopwatch, unix time, sleep, civil date, ISO/CLF parse and format, local offset | none |
| `unicode` | partial | `utf8`, `utf16`, simple case mapping and folding | multi-codepoint folds, segmentation, normalization |

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
