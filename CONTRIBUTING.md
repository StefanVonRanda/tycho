# Contributing to Hier

Thanks for trying Hier and wanting to help. **Hier is an experimental
proof-of-concept** (see the status note in the [README](README.md)) — the most
valuable contributions right now are **bug reports, repros, and design
feedback**, more than large features. Don't be shy about filing an issue.

## Reporting bugs and giving feedback

- **Found a miscompile, crash, or wrong output?** Open a
  [bug report](.github/ISSUE_TEMPLATE/bug_report.md) — the single most useful
  thing is a **small `.hi` program that reproduces it** plus what you expected
  vs. what happened, and your OS.
- **Have an idea, a rough edge, or a "why does it work this way?"** Open an
  [idea / feedback](.github/ISSUE_TEMPLATE/idea.md) issue. Even "I bounced off
  X" is useful signal.
- A miscompile that a fixture in `tests/` would have caught is gold — it tells
  us where the fuzzer and suite have a blind spot.

## Building and running

Prerequisites are just a C compiler (`cc`) and `make` — the compiler is a single
dependency-free C file. See the README's [Getting started](README.md#getting-started).

```
make                 # build ./hierc
./hierc f.hi && ./f  # compile + run a program
```

## The local CI gate (run it before a PR)

**Hier has no cloud CI — by design.** There are no GitHub Actions; the gate is
`scripts/ci.sh`, run locally:

```
make ci              # build · test · self-host fixpoint · corelib + examples ·
                     # concurrency · FFI · the three fuzz lanes · tooling · perf guard
make ci N=0          # same, skipping the (slow) fuzz lanes for a quick check
```

A change is "green" iff `make ci` passes. You can also install the local
pre-push hook (`make hooks`), which blocks a push if `make test` or
`make fixpoint` fails.

## Two rules that will surprise you

1. **Every language feature lands in BOTH compilers, or not at all.** Hier has a
   C reference compiler (`src/hierc.c`) and a self-hosted one written in Hier
   (`compiler/hierc0.hi`). `make fixpoint` asserts the Hier-built compiler
   reproduces itself byte-identically **and** matches the C compiler across the
   whole suite. A feature in only one compiler turns the fixpoint red. This is
   the parity discipline that keeps the two from drifting — plan for it.

2. **The arena memory model is the whole point.** Value semantics + implicit
   per-scope arenas (no GC, no manual `free`) is the thesis
   ([docs/thesis.md](docs/thesis.md)). Changes that quietly break the in-place
   optimizations (string append, the map accumulator, move-on-last-use) turn an
   O(n) idiom into O(n²) — `make bench` / `bench/` guard against that. When in
   doubt, read [docs/memory-model.md](docs/memory-model.md).

Also out of scope **by decision** (please don't propose them): a
ternary/conditional expression and a package manager. Generics, on the other
hand, *are* supported — `$T`, see [docs/generics.md](docs/generics.md).

## Code style

- Match the surrounding code — its comment density, naming, and idioms.
- C in `src/`/`runtime/` follows the existing C89/C11-ish style; Hier in
  `compiler/`/`corelib/` follows the existing Hier style (run `hier fmt` /
  `make tools-check`).
- One focused change per commit; the commit message says **what was wrong** and
  **how the fix was verified** (which test / fixpoint / fuzz run).
- New behavior gets a regression test under `tests/` (or a `corelib/test/`
  fixture) with a recorded golden, so it can't silently regress.

## Submitting

Open a pull request against `main`. Confirm `make ci` is green locally and say so
in the PR. Small, well-scoped PRs with a test are the easiest to accept.

By contributing, you agree your contributions are licensed under the project's
[Apache License 2.0](LICENSE).
