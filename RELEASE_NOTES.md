<!--
Draft release notes. Edit this before publishing, then:
  make ci
  make release-check                    # builds twice; smoke-tests and compares the native tarballs
  scripts/release.sh <version> --mingw  # builds and smoke-tests the Windows tarball
  gh release create <version> dist/tycho-<version>-*.tar.gz dist/tycho-<version>-*.sha256 --notes-file RELEASE_NOTES.md
  gh release edit <version> --prerelease
Build one tarball per platform (there is no hosted CI); attach them all to the release.
Name the version in the globs: dist/ still holds the previous release's tarballs.
-->

Tycho 0.8.9 — pre-1.0, no stability guarantees (see the [README](README.md) for
what that means in practice). Prebuilt binaries are attached, so you can try the
language without building from source.

**This is the friction release: seven pieces of real-program feedback from the
`tycho-dupe` probe, plus two benchmarked compiler optimizations.** It adds no
references and no escape hatches. The language surface is unchanged in shape;
`surface.lock` moved by addition only.

## Breaking

- **`strings.parse_int` is now the strict parser.** It returns
  `Result(int, IntErr)` and refuses empty input, trailing garbage and
  out-of-range values. The old fail-open scanner is now
  `strings.parse_int_prefix`; `parse_int_checked` is gone. Every call site,
  test, example, tool and doc was updated. This is the one change a 0.8.7
  program is likely to notice.
- **`eprintln(s)` is a builtin.** It writes `s` and a trailing newline to
  stderr. `eprint(s + "\n")` still works.
- **`path.real` is implemented through `real_checked`.** Its fail-closed
  behavior is unchanged; the new spelling returns a named `PathErr`.

## What the probe changed

- **`core:io` can now classify a file.** `FileKind`, `Stat`, `stat`, `lstat`
  and `same_file` (device + inode on POSIX, handle identity on Windows) give a
  filesystem tool one syscall for kind, size, mtime and identity. Symlinks,
  hard links, FIFOs, sockets and devices now have answers instead of a C shim.
- **`core:path` can say why a resolution failed.** `real_checked` returns
  `NotFound` / `Denied` / `TooLong` / `BadPath` / `Failed`; `real` remains the
  fail-closed convenience.
- **`path.quote`** gives a POSIX shell-safe single-quoted path for text reports.
- **`tycho-dupe`** is in `tools/tycho-dupe`, with `make dupe-check`. It is the
  probe program, not a demo: content hashing, filesystem identity, JSON output,
  error paths and both compilers.
- **The copy/borrow boundary is written down.** The reference, guide and spec
  now say exactly which values are borrowed and which copy: heap descriptors
  share storage; `[N]T`, `vector[N]T` and inline-array structs copy the whole
  value.
- **`inout` forwarding is documented** with a compiling example.
- **`tools/tycho-dupe/FRICTION-OUTSIDE.md`** records what went right and what
  remains, with status notes for each finding.

## Compiler optimizations

**O1 ships in 0.8.9: immortal allocation + literal lifetime elision.** String
literals now carry a negative length header; the runtime masks it, returns an
immortal literal unchanged on copy, and skips it on recycle. Every copy, push
and assign path inherits the elision without a compiler branch.

On 6,000,000 literal pushes into a `[string]` field: **207,460 KB / 61 ms →
114,056 KB / 28 ms** (median of five), byte-identical output. The result is in
`bench/opt/RESULTS.md`.

Making O1 real also exposed a latent escape-analysis bug: `return Env(m)` now
marks `m` as escaping in both compilers, so a mutated map/array local returned
through a zero-cost newtype no longer keeps its storage in a scope that is
freed on the way out. `tests/newtype_agg` is the regression.

The roadmap also carries O3–O7: last-use moves, borrowed string/bytes slices,
destination passing, conservative CoW, and SROA/bounds elimination. They are
the 0.9 line; O1 was the prerequisite for O4 and O6.

**O2: read-only pass-by-pointer in the reference compiler.** It passes
non-heap fixed arrays of four or more elements as `const T*` on direct calls.
On 5,000,000 calls through `fn first(xs: [256]int)`: **140 ms → 66 ms** (median
of ten, `-O2 -fno-inline`), same RSS. Function values, lambdas and spawns
bridge the ABI; FFI, `inout` and `sink` are unchanged.

The native archive ships `tychoc1`, the self-hosted compiler. It already
lowers `[N]T` to a 24-byte descriptor, so it never copied the 2 KB payload:
the same fixture measures **10 ms** with the packaged compiler. O2 therefore
closes the reference compiler's ABI-copy gap and brings `./tychoc` to the
representation the shipped compiler already had; the packaged binary's
performance is unchanged.

## Verification

`make ci N=0` — CI GREEN (821s). `make test` — 1156 passed, 0 failed.
`make corelib` — 46 ok. `make parse-check` — all green, 1505 files.
`make bench` — all benchmarks within bounds. The fuzz lanes were run
separately at 60 seeds each: differential, reject and leak all green.

0.8.9 is pre-1.0 and there are **no stability guarantees**: anything here may
change before 1.0. See [CHANGELOG.md](CHANGELOG.md) for the accumulating record.
