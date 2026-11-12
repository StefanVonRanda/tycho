Tycho 0.9.2 — pre-1.0, no stability guarantees (see the [README](README.md) for
what that means in practice). Prebuilt binaries are attached, so you can try the
language without building from source.

**0.9.2 is the corelib parity checkpoint.** The Odin `core` capability matrix now
has no partial or missing package rows: every row is either covered or an
explicit non-goal. The library is 89 packages, 101 golden test trees and 1,306
locked public functions.

This is capability parity, not API parity. Tycho keeps its value semantics,
implicit arenas and `Result` model; Odin code does not compile unchanged. The
explicit non-goals remain `mem`, `relative`, runtime `reflect`, raw `sys`, `c`,
`odin`, `rexcode`, SIMD intrinsics and `slice`.

## Corelib expansion

The parity push added or completed:

- **Compression** — PPMd7/PPMd8, PKZIP Shrink/Reduce/Implode, Deflate64 (ZIP
  method 9), shoco, BCJ2, delta, and BCJ filters for x86/ARM/ARM Thumb/ARM64/
  PowerPC/SPARC/IA64/RISC-V, plus swap2/swap4.
- **Encoding and assets** — HxA v3 reader/writer, BNG decode/encode, Qt `.qm`
  catalogs, locale data and native Lua `%b`/`%f`/non-greedy patterns.
- **Platform and runtime** — leap-second TZif records, epoll/kqueue/IOCP
  completion loops, deterministic prof sampling plus Spall traces, terminal
  cursor queries and Windows tty fallbacks, thread-local storage and a
  synchronous OS-thread runner.
- **C interop** — typed C function-pointer invocation in `core:dynlib`, and
  named C-to-Tycho callbacks through compiler-generated C-ABI trampolines.
  Callback parameters and results are limited to scalars, `ptr` and `void`;
  closures and function values are rejected.

PPMd and the PKZIP legacy codecs are vendored public-domain C (7-Zip SDK and
hwzip). Deflate64 uses zlib's `infback9`. HxA uses the reference C bit-transfer
shim; BNG uses the Unlicense reference codec. There is no new runtime library
dependency for the vendored codecs.

## Platforms

- **Android** — corelib tests pass on x86_64 and aarch64 at API 34.
- **iOS** — corelib tests pass in the simulator; the arm64 device build succeeds.
- **Linux, Windows, macOS** — the existing native and mingw gates remain green.

## Fuzzing

`make fuzz-newcomers` feeds mutated valid fixtures and random bytes to HXA parse,
BNG decode, shoco, PPMd7/PPMd8, PKZIP Shrink/Reduce/Implode, Deflate64 and BCJ2
under ASan+UBSan. The lane runs in `make ci`.

A deeper campaign ran 56,000 mutated inputs across four deterministic seeds with
no ASan/UBSan finding, no crash and no hang. Each decoder has a valid seed that
reaches its output, not just a header rejection.

## Verification

On the 0.9.2 tree:

- `make ci N=0` — green, recorded for the exact tree
- `make corelib` — 101 test trees, all green
- `make check-fast` — green
- `make fuzz N=200` — 200/200
- `make fuzz-reject N=200` — 200/200 (19 accepted, 181 rejected)
- `make fuzz-leak N=150` — 150/150
- `make fuzz-tsan N=200` — 63 ran, 137 skipped by design, 0 failed
- `make fuzz-newcomers` — green in CI; 56,000 inputs in the deeper campaign
- `make android-check` — green on x86_64 and aarch64
- `make ios-check` — green on the simulator, with the device-SDK build
- `make release-check` — byte-identical native archives; mingw archive rebuilt
- `make release-content` — 44 legs over both archives, 0 failed
- `make bench-suite-gate` — green on saved GCC and clang results

0.9.2 is pre-1.0 and there are **no stability guarantees**: anything here may
change before 1.0. The two 1.0 blockers remain a real program written by someone
other than the author, and an external security review; see
[docs/internals/1.0-readiness.md](docs/internals/1.0-readiness.md).
