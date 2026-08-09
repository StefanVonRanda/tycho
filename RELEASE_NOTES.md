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

- **Correctness hardening.** A parity-discipline sweep closed every remaining
  accept/reject divergence between the two compilers (sum-payload, scalar-coercion, and
  newtype-identity checks now agree at all supplying positions), and base-type mismatches
  now produce clean diagnostics instead of a downstream C error. The differential fuzzer
  was extended to cover these surfaces.
- **Documentation & repo restructure.** A getting-started [tutorial](docs/tutorial.md), a
  gentle [memory-model walkthrough](docs/from-c-to-arenas.md), and a documentation
  [index](docs/README.md); internal work logs moved out of the public tree.

Full detail is in the commit history.
