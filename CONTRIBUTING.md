# Contributing to Tycho

Thanks for trying Tycho and wanting to help. **Tycho is an experimental
proof-of-concept** (see the status note in the [README](README.md)) — the most
valuable contributions right now are **bug reports, repros, and design
feedback**, more than large features. Don't be shy about filing an issue.

## Reporting bugs and giving feedback

- **Found a miscompile, crash, or wrong output?** Open a
  [bug report](.github/ISSUE_TEMPLATE/bug_report.md) — the single most useful
  thing is a **small `.ty` program that reproduces it** plus what you expected
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
make                 # build ./tychoc
./tychoc f.ty && ./f  # compile + run a program
```

## The local CI gate (run it before a PR)

**Tycho has no cloud CI — by design.** There are no GitHub Actions; the gate is
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

1. **Every language feature lands in BOTH compilers, or not at all.** Tycho has a
   C reference compiler (`src/tychoc.c`) and a self-hosted one written in Tycho
   (`compiler/tychoc0.ty`). `make fixpoint` asserts the Tycho-built compiler
   reproduces itself byte-identically **and** matches the C compiler across the
   whole suite. A feature in only one compiler turns the fixpoint red. This is
   the parity discipline that keeps the two from drifting — plan for it.

2. **The arena memory model is the whole point.** Value semantics + implicit
   per-scope arenas (no GC, no manual `free`) is the thesis
   ([docs/thesis.md](docs/thesis.md)). Changes that quietly break the in-place
   optimizations (string append, the map accumulator, move-on-last-use) turn an
   O(n) idiom into O(n²) — `make bench` / `bench/` guard against that. When in
   doubt, read [docs/memory-model.md](docs/memory-model.md).

## Where feature work is useful

The language is **feature-complete for its proof-of-concept thesis** — value
semantics, implicit arenas, concurrency, generics, closures, UFCS, FFI, and the
`sink` consuming convention all ship in both compilers. The useful feature work
now is **ergonomics polish, not new pillars**:

- **User-defined projections** — yielding subscripts that generalize the built-in
  `&m[k]` (zero-copy views into part of a value). This is the one
  limited-reference idea that fits the arena + deep-copy-thread-boundary model;
  see [docs/rfc/limited-references-spike.md](docs/rfc/limited-references-spike.md).
  Low priority, scope it if a real need appears.
- **Small rough edges** real use turns up — clearer diagnostics (e.g. a
  keyword-used-as-variable message), FFI read-once-borrow docs, corelib gaps.

Also out of scope **by decision** (please don't propose them): a
ternary/conditional expression, a package manager, user-defined traits /
type-classes, Swift-style reference-counted copy-on-write, and **shared-mutable /
`remote-parts`-style references** for graphs — resolved against the model; store
graph-shaped data as an index pool (see
[docs/rfc/limited-references-spike.md](docs/rfc/limited-references-spike.md) and
[docs/internals/value-semantics-limits.md](docs/internals/value-semantics-limits.md)).
Generics, on the other hand, *are* supported — `$T`, see
[docs/generics.md](docs/generics.md).

## Code style

- Match the surrounding code — its comment density, naming, and idioms.
- C in `src/`/`runtime/` follows the existing C89/C11-ish style; Tycho in
  `compiler/`/`corelib/` follows the existing Tycho style (run `tycho fmt` /
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
