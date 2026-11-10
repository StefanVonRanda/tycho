Tycho 0.9.1 — pre-1.0, no stability guarantees (see the [README](README.md) for
what that means in practice). Prebuilt binaries are attached, so you can try the
language without building from source.

**0.9.1 is the single-compiler, tooling and measured-performance checkpoint.**
The language surface is unchanged. The historical C compiler is gone, the
self-hosted compiler is the only implementation, user-program builds are
substantially faster, and the remaining benchmark losses were closed or reduced.

## One compiler

- **`src/tychoc.c` is deleted.** Its final source remains in git history at
  `c7d46528ac6efcf9dc93359d14d98bedcb7ddbad`. Historical citations resolve
  against that revision; the reject classifier reads the frozen
  `compiler/reject_sites.json`; `single-compiler-check` fails if the file
  reappears.
- **`tycho-debug bt` is source-aware.** Demangled `pkg.name` frames, `.ty`
  file:line, with runtime glue and native frames tagged.

## Compile speed

- **Optimized user builds use LTO.** `hello.ty` drops from ~1.6 s to ~0.53 s;
  `compiler/main.ty` from ~26.6 s to ~6.3 s on gcc 16.2.1. Generated units under
  1 MiB use `-flto`; the compiler unit uses `-flto=auto`. Debug builds
  (`-O0 -g`) keep the plain path.
- **The development loop is explicit.** `make check-fast` runs the fixture,
  parser, citation, corelib, debugger, surface and doc-count gates in ~3 min;
  `make parse-check` is a real target and is in CI.

## Performance

Large-size rows, same checksum per workload:

| row | 0.9.0 / before | 0.9.1 | C |
|---|---:|---:|---:|
| `json_tree` time | 84 ms | **51 ms** | 49 ms |
| `json_tree` RSS | 80.9 MB | 81.7 MB | 103.6 MB |
| `lru` RSS | 20.2 MB | **14.7 MB** | 12.0 MB |
| `lru` time | 244 ms | **226 ms** | 233 ms |
| `trie` time (GCC) | 116 ms | **89 ms** | 79 ms |
| `dispatch` expert | ~24 ms | **12–13 ms** | 13 ms |
| `binary/arena` (GCC) | 423 ms | **377 ms** | 292 ms |
| `binary/arena` (clang) | — | **516 ms** | 504 ms |

The wins are structural: inlined string byte/equality paths with out-of-line
slow paths, thin 8-byte function values for programs that cannot create
closures, NULL-leaf recursive two-variant enums, small-map linear scans, and
map compaction at 1/8 dead. The remaining `lru` memory gap is the generic
map's duplicate key/value/index arrays; `binary/arena` is at clang parity and
still spends GCC cycles in recursive arena allocation.

## Corelib

- Checked siblings for the last sentinel APIs: `io.write_text`,
  `io.write_lines_checked`, `io.flush_stdout_checked`,
  `net.udp_{bind,send,read}_checked`, `path.safe_join_checked` and
  `path.resolve_under_checked`.
- UDP read distinguishes a legal zero-length datagram from timeout and failure.
- All additions are recorded in `surface.lock`; no corelib function was removed.

## Verification

On the 0.9.1 tree:

- `make ci N=0` — green, recorded for the exact tree
- `make test` — 1,157 fixture jobs, 0 failed
- `make corelib` — 49 test trees, all green
- `make fuzz N=200` — 200/200
- `make fuzz-reject N=200` — 200/200
- `make fuzz-leak N=150` — 150/150
- `make fuzz-tsan N=200` — 63 ran, 137 skipped by design, 0 failed
- `make bench-suite-gate` — green on GCC and clang results
- `make release-check` — byte-identical native archives
- `scripts/release.sh v0.9.1 --mingw` — Wine smoke-tested Windows archive

0.9.1 is pre-1.0 and there are **no stability guarantees**: anything here may
change before 1.0. The two 1.0 blockers are still a real program written by
someone other than the author, and an external security review; see
[docs/internals/1.0-readiness.md](docs/internals/1.0-readiness.md).
