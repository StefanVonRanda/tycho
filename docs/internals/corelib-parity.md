# Odin core parity

Target: **capability parity with Odin's `core` package tree**, adapted to
Tycho's value semantics and implicit arenas. Not line-for-line, not
API-for-API. Odin's `vendor/` tree is a separate ecosystem and out of scope.

Reference: Odin `dev-2026-11a` (`core` is 40 packages, 1,433 files, ~609k
lines). The Tycho corelib is 89 packages, ~26.5k lines, about 1,300 locked
public functions (`surface.lock`).

Every row is **covered** or an explicit **non-goal**; there are no open partials.

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
| `compress` | covered | gzip/zlib, raw DEFLATE, LZ4, xz, bzip2, PPMd7/PPMd8, PKZIP Shrink/Reduce/Implode, Deflate64 (ZIP method 9), shoco, BCJ2, delta, BCJ for x86/ARM/ARM Thumb/ARM64/PowerPC/SPARC/IA64/RISC-V, and swap2/swap4 | none |
| `container` | covered | `Deque`/`Queue`, `PriorityQueue`, `BitArray`, `Lru`, ordered `Treap`/`Avl`/`RbTree`, `HandleMap`, `SmallArray`, `Xar`, `topo_sort` | intrusive containers need references (non-goal) |
| `crypto` | covered | OpenSSL shims: ChaCha20-Poly1305 and AES-GCM AEAD, AES-CTR, AES-CBC, RSA PKCS#1 v1.5, ECDSA P-256, Ed25519, X25519, ML-DSA, ML-KEM, SHA-256/512, SHA-3/SHAKE, BLAKE2, HMAC-SHA256/512, PBKDF2, HKDF, Argon2id, CSPRNG; ASN.1 DER/BER and X.509 in their own packages | none |
| `debug` | covered | `assert`, `trace`, `enabled`, `stack_trace`; runtime traps and `tycho-debug bt` | PE introspection is platform-specific tooling |
| `diff` | covered | `diff.lines` and `diff.unified` (LCS, bounded fallback) | none |
| `dynlib` | covered | `open`, `symbol`, `close_lib` over `dlopen`/`LoadLibrary`, typed invocation of C function pointers (`call_i32_0`..`call_ptr_1`), and C-to-Tycho callbacks for named scalar/`ptr` functions (`apply_i32_i32`). Closures and function variables are not accepted as C callbacks | none |
| `encoding` | covered | `base64`, `base32`, `hex`, `json`, `csv`, `uuid`, `pem`, `ini`, `varint`, `endian`, `xml`, `cbor`, `asn1`, `entity`, `hxa` | none |
| `flags` | covered | `cli.parse`/`parse_spec` and `cli.flag` | none |
| `fmt` | covered | `format_int`, `format_float`, `pad_left`/`pad_right` plus `str`/f-strings | none |
| `hash` | covered | FNV-1a 32/64, djb2, sdbm, CRC16/32/64, Adler-32, Murmur3 and xxHash 32/64 | none |
| `image` | covered | PNG encode/decode, baseline JPEG encode/decode, BNG decode/encode, `raster` BMP/QOI/TGA/PPM/PGM/PBM | none |
| `io` | covered | file read/write/append/copy/stat/dirs, checked variants, stdin/stdout, line reader | no generic `Reader`/`Writer` interface; concrete values are the design |
| `log` | covered | leveled logger, stderr/file choice, `new`/`line` | none |
| `math` | covered | `math`, `fmath` (trig/exp/log/hypot), `decimal`, `bignum`, `rand`, `linalg`, `complex`, `fixed`, `ease`, `noise`, `bits` | none |
| `mem` | non-goal | implicit arenas; no public allocator API | none — explicit allocators, virtual memory and TLSF are a different memory model |
| `nbio` | covered | `nbio.poll` over `poll(2)`/`WSAPoll`; `CompletionLoop` with epoll (Linux), kqueue (BSD/macOS) and IOCP (Windows), including posted completions and fd readiness. epoll and IOCP are runtime-tested here; kqueue is implemented but has no BSD host in this tree | none |
| `net` | covered | TCP/UDP, timeouts, TLS, HTTP/HTTPD; DNS `resolve` plus MX/TXT/SRV parsing, UDP querying and TCP fallback; socket options; POSIX Unix sockets; `interfaces`; IPv4/IPv6/endpoint parse and format; prefix network/containment | none |
| `odin` | non-goal | Tycho has its own compiler packages | none |
| `os` | covered | process execution, environment, cwd/chdir, pid/ppid, hostname, home/temp/cache/config/data dirs, temp files/dirs, user name, pipes/fd I/O, `chmod`/`umask`, uid/gid, `io.walk` | process signals live in `core:signal` |
| `path` | covered | POSIX `path` plus Windows lexical `filepath` (clean/join/volume/base/dir/ext/stem) | native path opening still goes through `io` |
| `prof` | covered | manual `Span` timing and `report`, deterministic `sample_spans`, and Spall trace `encode_trace`/`decode_trace` | none |
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
| `terminal` | covered | ANSI escapes, tty/size, raw mode, byte read, key decoding, `cursor_query`/`cursor_position` with DSR parsing, and non-console Windows tty fallbacks | none |
| `testing` | covered | `T`, `check`, `eq`, `report`, `subtest`/`merge`, `bench`, `report_bench` | none |
| `text` | covered | `strings`, `utf8`, `utf16`, `char`, `regex`, `textmatch` (including `%b`, `%f` and non-greedy `-`), `edit`, `scanner`, `table`, `glob`, GNU MO and Qt `.qm` `i18n`, and locale data | none |
| `thread` | covered | language `spawn`/`wait` (one OS thread per task), channels, `parallel for`; `thread.id`/`yield_now`/`cpus`, synchronous `thread.run`, and 32-slot per-thread `local_set`/`local_get` | none |
| `time` | covered | monotonic stopwatch, unix time, sleep, civil date, ISO/CLF parse and format, local offset, IANA TZif zones via `core:tz` including the POSIX footer and leap-second records | none |
| `unicode` | covered | `utf8`, `utf16`, full case mapping/folding, titlecase mapping, UAX #15 normalization, UAX #29 grapheme/word segmentation, UCD general-category/script/block/emoji classification | none |

## What parity does not mean

- No general allocator parameter on corelib APIs. Arenas stay implicit, so
  Odin's `mem` allocator/virtual/TLSF surface is a non-goal rather than a gap.
- No references, `defer`, or manual `free` contract introduced to mirror Odin.
- No raw syscall surface. Platform depth arrives through shims with a
  documented portable API.
- No runtime type metadata solely to support `reflect`.
- No package added without `corelib/test/<pkg>` goldens,
  `docs/reference/corelib.md` entries and a deliberate `surface.lock` update.
  Byte-decoding codecs are also fuzzed by `make fuzz-newcomers` under ASan+UBSan.

## Platform targets

Odin's core also carries platform targets: `core/sys/android` (45 files, ~4k
lines) and `core/sys/darwin` (122 files, ~48k lines), with Android/Darwin
branches in `os`, `net`, `log`, `sync`, `thread` and `time`.

| Target | Status | Evidence / gap |
|---|---|---|
| Linux x86-64 | supported | full `make ci`, release archive |
| Windows x86-64 | supported | mingw build + Wine smoke, release archive |
| macOS arm64 | supported | darwin-arm64 `make ci`, release archive |
| Android | **supported** | NDK cross-compile + qemu-user (bionic) runtime: `make android-check` runs the corelib tests on x86_64 and aarch64 at API 34. Runtime evidence is qemu-user, not the emulator |
| iOS | **supported** | iOS Simulator: `make ios-check` runs the corelib tests; the same emitted C also builds for the arm64 device SDK. No signed physical-device run |

`supported` in this table means the corelib target gate is green. For Android
and iOS that is not an end-user platform claim: no APK, Xcode project, JNI or
asset-manager binding, or signed-device run ships.

`make android-check` runs on Linux with the NDK and qemu-user; `make ios-check`
on macOS with Xcode and a simulator runtime. `os.platform()` and the
`is_android`/`is_ios` predicates are exercised by both.
