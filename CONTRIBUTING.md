# Contributing to Tycho

Thanks for trying Tycho and wanting to help. **Tycho is 0.5 — pre-1.0** (see the
status note in the [README](README.md)): the language surface and the corelib API
can still change, so the most useful thing you can send is a **bug report, a
repro, or some design feedback** — much more than a big feature. Feedback on
ergonomics is worth more than usual right now: what 1.0 is waiting on is real
programs written by someone other than the author. Please don't be shy about
filing an issue.

## Reporting bugs and giving feedback

- **Found a miscompile, crash, or wrong output?** Open a
  [bug report](.github/ISSUE_TEMPLATE/bug_report.md). The single most useful
  thing is a **small `.ty` program that reproduces it**, plus what you expected
  vs. what happened, and your OS.
- **Have an idea, a rough edge, or a "why does it work this way?"** Open an
  [idea / feedback](.github/ISSUE_TEMPLATE/idea.md) issue. Even "I bounced off
  X" tells me something useful.
- A miscompile that a fixture in `tests/` would have caught is gold — it shows
  me where the fuzzer and suite have a blind spot.

## Building and running

All you need is a C compiler (`cc`) and `make` — the transpiler is a single
dependency-free C file. See the README's [Trying it](README.md#trying-it).

```
make                 # build ./tychoc
./tychoc f.ty && ./f  # compile + run a program
```

## The local CI gate (run it before a PR)

**Tycho has no cloud CI — that's on purpose.** There are no GitHub Actions; the
gate is `scripts/ci.sh`, run locally:

```
make ci              # build · test · ilp32 · asan-self · corelib + examples ·
                     # concurrency · FFI · the three fuzz lanes · tooling ·
                     # perf guard · recursion · spec-check · link+citation check
make ci N=0          # same, skipping the (slow) fuzz lanes for a quick check
```

A change is "green" iff `make ci` passes.

**Run `make hooks` once after you clone.** It points `core.hooksPath` at
`.githooks/`, so `git push` runs the citation/link gate, then `make ci N=0`,
then a fuzz smoke, and blocks the push if any of them fails. That setting is
**per-clone git config and is not tracked**, so a fresh clone has no hooks at
all until you run it — which is exactly how a red citation gate reached `main`
on 2026-08-10.

The sweep is not as long as it looks: **`make ci` measured 495–499s across four
runs on a 16-core box, 2026-08-10** — all thirteen steps including 200 fuzz
seeds. `N=0`, the hook's setting, is **274s**. It parallelises (`run_lanes`
forks each lane group), which is why the whole sweep costs barely more than
`make test` alone.

So do not run the lanes one at a time hoping to save time — the sum is far
slower than the sweep. Run a single lane when you are ITERATING on one failure
and want its verdict in seconds; run `make ci` to cover the tree. If something
is watching its output, redirect to a log (`make ci > ci.log 2>&1`) rather than
piping it — a pipeline dies with whatever is reading it and you lose the partial
result.

The one gate to never skip is the cheapest:

```
make check-links     # relative links + every path:line citation, under a second
```

`path:line` refs are load-bearing in this tree and drift on any insertion into a
cited file. `scripts/reanchor_citations.py` remaps them mechanically after you
move lines yourself — read its header first, it is the wrong tool when only some
refs are stale.

## Two rules that will surprise you

1. **There is one maintained compiler, and `compiler/tychoc0.ty` is not it.**
   `src/tychoc.c` (`tychoc`) is the reference transpiler; the
   [spec](docs/spec/) is normative. The tree also holds `compiler/tychoc0.ty`, a
   transpiler for Tycho written in Tycho — the artifact that proved self-hosting
   (compiled by itself, it reproduced its own emitted C byte-for-byte).

   **It was frozen on 2026-07-26.** Do not update it, do not mirror a language
   change into it, and do not read it to learn how Tycho behaves — no gate builds
   or runs it, so it compiles the language as it stood on the freeze date and
   already diverges from `tychoc` (which now rejects `fn handle(...)`, a reserved
   word as a procedure name, where `tychoc0` still accepts it). Until that date the
   opposite rule applied: every feature had to land in both, enforced by
   `make fixpoint` and the accept/reject parity lanes. Those gates are gone.

2. **The arena memory model is the whole point.** Value semantics + implicit
   per-scope arenas (no GC, no manual `free`) is the thesis
   ([docs/thesis.md](docs/thesis.md)). Changes that quietly break the in-place
   optimizations (string append, the map accumulator, move-on-last-use) turn an
   O(n) idiom into O(n²), and `make bench` / `bench/` guard against that. When in
   doubt, read [docs/guides/memory-model.md](docs/guides/memory-model.md).

## Where feature work is useful

The language surface is **feature-complete but not frozen** — value semantics,
implicit arenas, concurrency, generics, closures, UFCS, FFI, and the `sink`
consuming convention are all in. Pre-1.0 means they can still change; what a
freeze is waiting on is in
[ROADMAP.md](ROADMAP.md#what-1-0-requires). So the feature work I find useful
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
[docs/guides/generics.md](docs/guides/generics.md).

## Code style

- Match the surrounding code — its comment density, naming, and idioms.
- C in `src/`/`runtime/` follows the existing C89/C11-ish style; Tycho in
  `compiler/`/`corelib/` follows the existing Tycho style (run `tycho fmt` and
  `make tools-check`).
- One focused change per commit; the commit message says **what was wrong** and
  **how the fix was verified** (which test / gate / fuzz run).
- New behavior gets a regression test under `tests/` (or a `corelib/test/`
  fixture) with a recorded golden, so it can't silently regress.

## Submitting

Open a pull request against `main`. Confirm `make ci` is green locally and say so
in the PR. Small, well-scoped PRs with a test are the easiest for me to accept.

By contributing, you agree your contributions are licensed under the project's
[MIT License](LICENSE).
