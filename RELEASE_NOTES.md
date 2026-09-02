<!--
Draft release notes. Edit this before publishing, then:
  make ci
  make release-check                    # builds twice; smoke-tests and compares the native tarballs
  scripts/release.sh <version> --mingw  # builds and smoke-tests the Windows tarball
  gh release create <version> dist/tycho-*.tar.gz dist/*.sha256 --notes-file RELEASE_NOTES.md
  gh release edit <version> --prerelease
Build one tarball per platform (there is no hosted CI); attach them all to the release.
-->

Tycho 0.8 — pre-1.0, no stability guarantees (see the [README](README.md) for
what that means in practice). Prebuilt binaries are attached, so you can try the
language without building from source.

**This release does not break source compatibility with 0.7.** That is measured,
not assumed: the 0.7.0 compiler was rebuilt and run beside this one over the
whole accept corpus — 300 programs, 297 of which 0.7.0 compiled, all 297 still
compile, and three that 0.7.0 refused now work.

## The compiler is now written in Tycho

`tychoc` in the Linux tarball is `tychoc1`, the self-hosted compiler. It is built
in two stages: the C bootstrap in `src/tychoc.c` builds stage 1, and stage 1
builds the compiler you get, so it carries its own optimisations. The bootstrap
is not going away — it is how a clean checkout gets started.

**On Windows the tarball still contains the C bootstrap.** The mingw64 build
cross-compiles `src/tychoc.c` and does not cross-build the self-hosted compiler.
Same language, same version, different implementation.

On its own source the self-hosted compiler now compiles faster than the
bootstrap — 73 ms against 105 ms, minimum of ten runs — and is level on the other
inputs measured. It started this cycle five times slower.

## Install

Download the tarball for your platform below, verify it, and unpack it:

```
tar xzf tycho-<version>-<os>-<arch>.tar.gz
cd tycho-<version>-<os>-<arch>
./tychoc examples/hello.ty && ./examples/hello
```

`tychoc` writes the binary beside its source unless you pass `-o`, so the
program built from `examples/hello.ty` is `examples/hello`.

The core library ships inside the tarball, beside the compiler, so there is
nothing to configure. You still need a C compiler (`cc`) on your `PATH` — Tycho
transpiles to C. Each tarball's SHA-256 is published alongside it.

## What's new

- **Three generic shapes that 0.7 refused now compile**: a generic function
  returning an array under a `where` clause, a generic constructor taking a bare
  `[]`, and a lambda argument at a generic call site.
- **`io.make_dir_all`** — `mkdir -p`, which `tycho-ar` had hand-rolled — plus
  `io.append_text`, `io.copy` and `strings.format_g17`.
- **Array-literal codegen was quadratic.** 30,000 elements went from over a
  minute to 20 ms.
- **Three new tools**: `tycho-diff`, `tycho-fold`, `tycho-hash`.
- **Diagnostics**: several messages named the compiler's internals rather than
  the mistake, and two offered confident suggestions built on a wrong
  assumption. Those are rewritten.

## Deprecated

- **`decimal.from_str` fails open** — it returned `0.15` for `"1.5x"` — and is
  deprecated. Use `decimal.parse` for a checked read, or
  `decimal.parse_unchecked` if you really want the old behaviour. It still
  works in 0.8 and warns.

## The language surface is frozen

Since 2026-08-22 the keyword set, the builtin set and every corelib signature are
locked and gated: 101 keywords, 41 builtins, no additions or removals. Corelib
may gain a function; it may not lose one or change a signature. **No new
language features before 1.0.** The point is that the surface stops moving long
enough to be learned and depended on.

## What this release does not have

There has been **no third-party security review**, and the FFI boundary is
unsafe by design — see [SECURITY.md](SECURITY.md), and
[docs/internals/audit-brief.md](docs/internals/audit-brief.md) if you are
willing to be one.

One limit is stated rather than fixed, because you may hit it:

- **On Windows, a server winds down within its idle timeout** rather than
  within a millisecond: a thread parked in `recv` is not released by the
  shutdown handler as it is on Linux. Nothing is lost or corrupted.

## Status

0.8 is pre-1.0 and there are **no stability guarantees**: anything here may
change. [ROADMAP.md](ROADMAP.md#what-10-requires) lists what 1.0 requires, and
the blocking item is not engineering — it is that nobody outside this repo has
written a real program in Tycho yet. If you write one, the friction you hit is
the most useful thing you can send back.
