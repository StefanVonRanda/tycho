<!--
Draft release notes. Edit this before publishing, then:
  make ci
  make release-check                    # builds twice; smoke-tests and compares the native tarballs
  scripts/release.sh <version> --mingw  # builds and smoke-tests the Windows tarball
  gh release create <version> dist/tycho-*.tar.gz dist/*.sha256 --notes-file RELEASE_NOTES.md
Build one tarball per platform (there is no hosted CI); attach them all to the release.
-->

Tycho 0.7 — pre-1.0, no stability guarantees (see the [README](README.md) for
what that means in practice). Prebuilt binaries are attached, so you can try the
language without building from source.

**This release breaks source compatibility with 0.6.** Several shapes that
compiled before are now refused, and two of them were live memory errors rather
than style: a copied `handle` gave one pointer two owners and glibc reported
`double free detected in tcache 2` from a four-line program, and a copied channel
aliased one channel to two owners so a send through one was received through the
other. Also refused now: `sink`/`inout` on an affine type, a bare handle as a
struct field, and a `bounded[N]T` generic field that had silently degraded to a
plain fixed array. `JsonErr` gains a variant, which breaks an exhaustive `match`
on it. Every migration is mechanical; [CHANGELOG.md](CHANGELOG.md)'s breaking
sections state each as "what you wrote" → "what you write" and are the list, not
this paragraph.

## Security fixes

`core:crypto` — two of these are credential-grade, and both failed by
**collision** rather than by weakness, which is the mode that does not look like
a bug:

- **A password was truncated at its first NUL.** The shim called `strlen`, so
  `"secret\0A"` and `"secret\0B"` derived the *same* key — and the same one as
  `"secret"`. Two distinct credentials authenticated against each other. The
  length is passed explicitly now.
- **`ct_equal` compared two hex strings by their prefixes**, for the same reason.
  Not reachable in the MAC-verification shape — a computed MAC carries no NUL, so
  the lengths disagreed and the answer was already false — but a caller comparing
  two *supplied* values had a collision.
- **The plaintext was left in freed memory.** `aead_encrypt` released the
  plaintext it decoded and `aead_decrypt` the one it recovered, neither wiped.
  The recycled hex return buffer also kept its last value — including a key from
  `key_export_hex` — for the life of the thread.
- **Key import was not constant-time.** The hex decode rejected at the first bad
  digit, which times the offset of the error. Branch-free now, verified under
  valgrind rather than by timing: 7 secret-dependent branches before, 0 after.

Elsewhere:

- **`core:http` can be pointed at a private CA** (`SSL_CERT_FILE` /
  `SSL_CERT_DIR`), which is what makes its certificate verification testable at
  all. It was correct before and *unprovable*; `make http-verify` now holds it.
- **`markdown.render` no longer emits `javascript:` or `data:` hrefs**, and
  escapes attribute values as well as text.
- **`json.parse_checked` refuses a document nested past 2000** instead of
  aborting the process. A deeply nested document used to kill it with
  `tycho: stack overflow`, so the one entry point with an error channel had a
  hole exactly where an attacker controls the input.
- **`sqlite` bound parameters survive an interior NUL**, and `toml.parse` refuses
  a repeated `[table]` header that used to delete the first one.

There is still **no third-party audit** of any of this — see
[SECURITY.md](SECURITY.md), and
[docs/internals/audit-brief.md](docs/internals/audit-brief.md) if you are willing
to be one.

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

0.7 is pre-1.0 and there are **no stability guarantees**: anything here may
change — this release broke source compatibility in six places and the next one
may too. [ROADMAP.md](ROADMAP.md#what-1-0-requires) lists what 1.0 requires, and
the blocking item is not engineering — it is that nobody outside this repo has
written a real program in Tycho yet. If you write one, the friction you hit is
the most useful thing you can send back.
