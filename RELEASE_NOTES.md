Tycho 0.9.2 — pre-1.0, no stability guarantees.

0.9.2 expands the core library to cover the Odin `core` capability set and adds
Android and iOS compiler target gates. This is capability parity, not API parity:
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

Prebuilt archives are attached for **Linux x86-64**, **Windows x86-64** and
**macOS arm64**. The macOS archive is built natively on Apple Silicon and
smoke-tested there; it is not a cross-compiled binary.

**Android** and **iOS** are compiler target gates, not end-user platforms in
0.9.2. `make android-check` and `make ios-check` build and run dependency-free
corelib tests for those targets; no app archive, APK, Xcode project, JNI binding
or signed-device run is shipped.

## Known limitations

- C-to-Tycho callbacks accept named functions only. Callback parameters and
  results are limited to scalars, `ptr` and `void`; closures and function
  values are rejected.
- `mem`, `relative`, runtime `reflect`, raw `sys`, `c`, `odin`, `rexcode`, SIMD
  intrinsics and `slice` remain explicit non-goals.
- Tycho is pre-1.0. The language surface and corelib API may change before 1.0.

Prebuilt archives and checksums are attached to this release.
