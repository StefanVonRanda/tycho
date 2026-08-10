<!--
Draft release notes. Edit this before publishing, then:
  scripts/release.sh <version>          # builds dist/tycho-<version>-<os>-<arch>.tar.gz
  gh release create <version> dist/tycho-*.tar.gz dist/*.sha256 --notes-file RELEASE_NOTES.md
Build one tarball per platform (there is no hosted CI); attach them all to the release.
-->

Tycho 0.5 — pre-1.0, no stability guarantees (see the [README](README.md) for
what that means in practice). This release ships prebuilt binaries so you can
try the language without building from source.

It was labelled 1.0 for four days and demoted before anything was ever tagged:
the engineering was not in doubt, but 1.0 is a promise not to break people and
nothing had shipped to anyone. [ROADMAP.md](ROADMAP.md#what-1-0-requires) lists
what 1.0 now requires.

## Install

Download the tarball for your platform below, verify it, and unpack it:

```
tar xzf tycho-<version>-<os>-<arch>.tar.gz
cd tycho-<version>-<os>-<arch>
./tychoc examples/hello.ty && ./hello
```

The core library ships inside the tarball, beside the compiler, so there's nothing to
configure. You still need a C compiler (`cc`) on your `PATH` — Tycho transpiles to C. Each
tarball's SHA-256 is published alongside it.

## What's changed

Since the work recorded on 2026-08-05, when this was briefly labelled 1.0:

- **`Result(void, E)`** — an operation that either fails with a reason or
  succeeds with nothing to report. `void` is spellable as a `Result`'s ok
  payload and nowhere else: build it with `Ok()`, match it with a bare `Ok:`
  arm. Shipped with it, because the feature is unusable without it,
  `f() or_return` is now a statement when the payload is void.
- **`pass`** — the no-op statement, for a block with nothing to do. Contextual,
  not reserved: `pass` stays usable as a variable name.
- **`sort.sort_by(xs, cmp)`** — a comparator sort, so an order can use several
  keys, mixed directions, or a type with no `comparable` instance. Stable
  (bottom-up merge). **`sort.by_key` is deprecated** in favour of it and warns
  on use; it keeps working for all of 0.x and is removed at 1.0.
- **A `len` that shadows a builtin now warns** at the declaration instead of
  silently answering wrong, and **a line ending in an operator continues** onto
  the next.
- **`tychofmt` no longer writes `-1` as `- 1`.**

Two limits are now stated rather than implied, because you will hit them:

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

0.5 is pre-1.0 and there are **no stability guarantees**: anything here may
change. [ROADMAP.md](ROADMAP.md#what-1-0-requires) lists what 1.0 requires, and
the blocking item is not engineering — it is that nobody outside this repo has
written a real program in Tycho yet. If you write one, the friction you hit is
the most useful thing you can send back.
