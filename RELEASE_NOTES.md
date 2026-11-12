Tycho 0.9.2 — pre-1.0, no stability guarantees.

0.9.2 expands the core library to cover the Odin `core` capability set and adds
Android and iOS target support. This is capability parity, not API parity:
Tycho keeps its value semantics, implicit arenas and `Result` model, so Odin
code does not compile unchanged.

## Core library

- **Compression** — PPMd7/PPMd8, PKZIP Shrink/Reduce/Implode, Deflate64,
  shoco, BCJ2, delta, BCJ filters for x86/ARM/ARM Thumb/ARM64/PowerPC/SPARC/
  IA64/RISC-V, and swap2/swap4.
- **Encoding and assets** — HxA v3 reader/writer, BNG decode/encode, Qt `.qm`
  catalogs, locale data and native Lua `%b`/`%f`/non-greedy patterns.
- **Platform and runtime** — leap-second TZif records, epoll/kqueue/IOCP
  completion loops, deterministic prof sampling with Spall traces, terminal
  cursor queries and Windows tty fallbacks, thread-local storage and a
  synchronous OS-thread runner.
- **C interop** — typed C function-pointer invocation in `core:dynlib`, and
  named C-to-Tycho callbacks through compiler-generated C-ABI trampolines.

PPMd and the PKZIP legacy codecs are vendored public-domain C (7-Zip SDK and
hwzip). Deflate64 uses zlib's `infback9`. BNG uses the Unlicense reference
codec. The vendored codecs add no runtime library dependency.

## Platforms

- Linux x86-64
- Windows x86-64
- macOS arm64
- Android x86-64 and aarch64
- iOS simulator and arm64 device builds

## Known limitations

- C-to-Tycho callbacks accept named functions only. Callback parameters and
  results are limited to scalars, `ptr` and `void`; closures and function
  values are rejected.
- `mem`, `relative`, runtime `reflect`, raw `sys`, `c`, `odin`, `rexcode`, SIMD
  intrinsics and `slice` remain explicit non-goals.
- Tycho is pre-1.0. The language surface and corelib API may change before 1.0.

Prebuilt archives and checksums are attached to this release.

Tycho 0.9.2 is pre-1.0; the language surface and corelib API may change.
