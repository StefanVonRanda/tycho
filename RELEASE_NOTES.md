<!--
Draft release notes. Edit this before publishing, then:
  make ci
  make release-check                    # builds twice; smoke-tests and compares the native tarballs
  scripts/release.sh <version> --mingw  # builds and smoke-tests the Windows tarball
  gh release create <version> dist/tycho-*.tar.gz dist/*.sha256 --notes-file RELEASE_NOTES.md
Build one tarball per platform (there is no hosted CI); attach them all to the release.
-->

Tycho 0.6 — pre-1.0, no stability guarantees (see the [README](README.md) for
what that means in practice). Prebuilt binaries are attached, so you can try the
language without building from source.

**This release breaks source compatibility with 0.5.** Six changes refuse or
re-shape code that compiled before, and every migration is mechanical. Read the
BREAKING CHANGES section of [CHANGELOG.md](CHANGELOG.md) before upgrading — it
states each one as "what you wrote" → "what you write".

## Install

Download the tarball for your platform below, verify it, and unpack it:

```
tar xzf tycho-<version>-<os>-<arch>.tar.gz
cd tycho-<version>-<os>-<arch>
./tychoc examples/hello.ty && ./examples/hello
```

`tychoc` writes the binary beside its source unless you pass `-o`, so the
program built from `examples/hello.ty` is `examples/hello`.

The core library ships inside the tarball, beside the compiler, so there's nothing to
configure. You still need a C compiler (`cc`) on your `PATH` — Tycho transpiles to C. Each
tarball's SHA-256 is published alongside it.

## Breaking changes

Full detail and migrations in [CHANGELOG.md](CHANGELOG.md); the short form:

- **A binding may not start with an uppercase letter.** Locals, parameters,
  loop variables and pattern bindings must start lowercase or `_`. The
  uppercase namespace belongs to types, enum variants and consts, so a binding
  can no longer shadow a constructor. `const` is exempt. Rename the binding.
- **`is` is now a reserved keyword** (the variant test, below). A variable or
  function named `is` no longer compiles.
- **A function may not be named `Ok`, `Err`, `Some`, `None`,** or after any
  enum variant or const. Rename it.
- **`core:iter` predicates return `bool`, not `int`** — `filter`, `try_filter`,
  `count`, `any`. Write `fn(x: int) -> bool: x % 2 != 0` where you wrote
  `fn(x: int) -> int: x % 2`.
- **`image.decode`/`encode` and `compress.decompress` return a `Result`**
  instead of an empty-or-sentinel value, so a corrupt input is no longer
  indistinguishable from a legitimately empty one. Add `or_return` or a `match`.
- **`tycho-ar x` exits 1 when an mtime could not be restored.** It now finishes
  the extraction and warns per member rather than dying part-way; exit 0 still
  means fully restored.

## What's new

- **`is`, the variant test** — `x is VariantName` for an enum, an `Option` or a
  `Result`, so you can ask without destructuring: `if r is Ok:`, `o is Some`.
- **A `[string]` may cross the FFI** as an extern parameter, arriving in C as
  `(const char *const *, long)`. This retired `core:os`'s whole argv-builder
  protocol; `os.exec` and `os.exec_out` are unchanged for callers.
- **`io.mtime` / `io.set_mtime`**, **`strings.slice_bytes` / `slice_str`** (a
  fail-closed slice, where the built-in one clamps), **`iter.try_map` /
  `try_filter`** (short-circuit on the first `Err`), **`result.map_err_with`**
  (translate an error and keep the cause), and **`decimal.div`** with rounding
  modes and three named failure causes.
- **f-string interpolations evaluate left to right.** They printed in source
  order but *called* in reverse, because the holes became arguments to one C
  function and C leaves argument order unspecified. Observable whenever a hole
  carries a call with a side effect.
- **A compiler use-after-free is fixed.** A stale pointer into the signature
  table, dangling after a generic instantiation reallocated it, produced bogus
  refusals of valid programs that came and went with unrelated edits.
- **Better diagnostics**: a refusal inside a generic now names the call site
  that instantiated it, and a non-exhaustive `Result`/`Option` match names the
  uncovered variant instead of claiming a side was missing.

Two limits remain stated rather than fixed, because you will hit them:

- **`core:net` has no readiness polling.** No `poll`/`select`/`epoll`/
  `O_NONBLOCK` anywhere in the package, so a server's worker count is a hard
  ceiling on concurrent connections and one slow client occupies one worker.
- **On Windows, a server winds down within its idle timeout** rather than
  within a millisecond: a thread parked in `recv` is not released by the
  shutdown handler as it is on Linux. Nothing is lost or corrupted.

## What this release does not have

Stated plainly, because the previous draft of these notes advertised the
opposite. Until 2026-07-29 every program was compiled by two independent
implementations — `tychoc` and the self-hosted `tychoc0` — which had to agree.
**That differential is retired and nothing replaces it.** It caught real
defects, specifically changes that silently narrowed what the language accepts,
and that class of defect now has no second opinion. `compiler/tychoc0.ty`
remains in the tree as the artifact proving Tycho self-hosts, and as the
compiler's hardest sanitizer input, but it is no longer a check.

There has also been **no third-party security review**, and the FFI boundary is
unsafe by design — see [SECURITY.md](SECURITY.md).

## Status

0.6 is pre-1.0 and there are **no stability guarantees**: anything here may
change — this release broke source compatibility in six places and the next one
may too. [ROADMAP.md](ROADMAP.md#what-1-0-requires) lists what 1.0 requires, and
the blocking item is not engineering — it is that nobody outside this repo has
written a real program in Tycho yet. If you write one, the friction you hit is
the most useful thing you can send back.
