# Roadmap

Tycho is **0.8.6 — pre-1.0**, and this file is a direction, not a promise of
dates. Day-to-day work is tracked in [GitHub
issues](https://github.com/StefanVonRanda/tycho/issues); this file is the
high-level shape. What 1.0 now requires is [below](#what-10-requires).
Terms (`surface.lock`, `tychoc`/`tychoc1`, lanes, FRICTION) are defined in
[STATUS](STATUS.md).

## Where it is

The language and corelib are feature-complete for the thesis they exist to
prove; the strength is the correctness harness (fuzzer, sanitizer lanes,
golden-locked suite), green in `make ci`. See [STATUS](STATUS.md) for what
works and how it is known, and [docs/architecture.md](docs/architecture.md)
for what each gate proves.

Production readiness is [below](#what-production-ready-requires). What is left
is not engineering: an outside program and an external review, both needing
another person.

## The self-hosted compiler

`compiler/` is Tycho written in Tycho — about 21,000 lines in six
packages (`lex`, `parse`, `ast`, `types`, `emit`, `driver`), built as `tychoc1`.
The test, corelib, example and server runners default to it, so the gates run
on the self-hosted compiler; `src/tychoc.c` bootstraps it and scores it.

- **The bootstrap is two-stage and the fixpoint holds**, gated as
  `make fixpoint-check`.
- **Two gates carry it.** `make parse-check` scores its front end against
  `tychoc` verdicts with AST/resolution/type censuses; `make tychoc1-check`
  runs the real suites under it.
- **Decided 2026-09-11: `tychoc1` stays the second opinion.** Where the two
  compilers diverge it is a defect in one of them (FRICTION 87 found a worse
  diagnostic in the reference this way). `src/tychoc.c` is normative,
  `tychoc1` is the oracle; see
  [architecture](docs/architecture.md#which-compiler-is-right).

## The language surface moved once, deliberately

Since 2026-08-22 the keyword, builtin and corelib surface is locked by
`surface.lock` and gated by `make surface-check`, with no new language
features before 1.0. The lock broke once, on purpose in 0.8.5, for layout and
SIMD (`packed`, `align(N)`, `vector[N]T`, simultaneous assignment, swizzling);
it records additions rather than forbidding them.

Corelib may still gain functions; it may not lose one or change a signature.
Since 2026-09-03 a gained function must be recorded in the same commit.
To record deliberately: `python3 scripts/surface_lock.py --record`.

### Taken since the lock

All three have shipped; kept here as the record of what the freeze was broken for:

- **Alignment and packed layout** (shipped 2026-09-04/05):
  `packed struct` gives a byte-exact layout;
  `align(N) struct` raises alignment to `N` (max 8)
  ([spec 17.1a](docs/spec/12-aggregates.md#171a-packed-layout)).
- **Vectors** (shipped 2026-09-04): `vector[N]T` is a fixed array whose
  arithmetic is one machine vector operation
  ([spec 5.3.11](docs/spec/03-types.md#5311-vectornt)); `N` is a power of two, 2–64.
- **Groups** (shipped): simultaneous assignment of 2–8 places and swizzling
  2–8 components of one value
  ([spec 17.5](docs/spec/12-aggregates.md#175-destructuring)), with `.x/.y/.z/.w`
  lane names on vectors.

Nothing further is planned before 1.0. Not queued: compile-time execution
(deferred) and field promotion (`using`, refused).

## Near-term

Foundation before feature breadth. In rough priority:

1. **Demand-gated corelib / tooling.** New library and tooling work is built against a
   real program that needs it, never ahead of one. Requests belong in the issue tracker.
2. **Keeping the compiler honest.** New language features are adversarially fuzzed
   and gated against recorded goldens before shipping. Ongoing, not a milestone.
3. **Overloading — open, demand-gated.** A name may have exactly one definition.
   The trigger is a real program that needs it, not a design impulse.

## What 1.0 requires

A 1.0 date was declared 2026-08-05 and withdrawn 2026-08-09 — there was no one
to make the promise to yet. What remains is §1 and §7, neither a task this list
can complete: an outside program and an external review, both needing another
person.

### 1. Someone other than the author has written a real program

The blocking one. Every ergonomic finding so far came from the author
dogfooding; only a stranger's mental model produces the rest.

Concretely: **three non-trivial programs written against the docs alone**,
each with its friction written down, each targeting a different half of the
surface.

Four exist across four halves — `tools/tycho-diff` (algorithms),
`tools/tycho-hash` (concurrency),
`tools/tycho-fold` (text and codepoints), and the 2026-08-19 FFI probe
([record](docs/internals/probe-ffi-2026-08-19.md)) — each with its
`FRICTION-OUTSIDE.md`. Left open deliberately: the probes keep finding what
the author could not, and generics, `subscript`, `bounded[N]`, `select` and
error paths still have no non-author program
([procedure](docs/internals/probe-procedure.md)).

### 2–6. ~~Papercuts, expressiveness, `core:net`, Windows, a shipped release~~ — **all CLOSED**

Closed between 2026-08-10 and 2026-08-15, in that order. The record is in
[`docs/internals/roadmap-closed-2026-08.md`](docs/internals/roadmap-closed-2026-08.md).

### 7. An external security review

[SECURITY.md](SECURITY.md) says plainly that there has been no third-party
audit. A reviewer packet exists
([audit-brief](docs/internals/audit-brief.md)); three internal passes found
5, 9 and 10 issues with zero overlap, which is the argument for this section.

### What is explicitly NOT required

More features, more corelib packages, more benchmarks, or a package manager.
The language is feature-complete for the thesis it exists to prove.

## What production-ready requires

Set 2026-08-15. **The thesis is proven**; the goal now is a complete,
production-ready language. "Feature-complete for a thesis" no longer settles
scope; the non-goals resting on it are open for re-decision by the owner.

### 1. It runs where developers are

Artifacts exist for `linux-x86_64`, `mingw64-x86_64` and **`darwin-arm64`**
(built 2026-09-19). None is published for macOS yet — `gh release create` is
the owner's call. ARM64 Linux runs `make test` green in a VM on Apple Silicon;
no Graviton artifact is built yet.

`make ci` is green on `darwin-arm64` with enumerated skips, one a real coverage
loss (no LeakSanitizer on the platform). The run found three defects, two
latent in shipped code (FRICTION 100–102). All three cross-compile targets have
had binaries run, including a float-determinism defect only two machines could
show (FRICTION 112–113). See [STATUS](STATUS.md) for the current platform scope.

Why no porting was needed:

- `runtime/tycho_rt.c` already carries macOS support (stack bounds,
  per-architecture stack-pointer extraction, `__aarch64__` branch).
- `src/tychoc.c` has zero platform conditionals.
- With `zig cc` as cross driver, the compiler and the emitted C build for
  `aarch64-macos`, `x86_64-macos` and `aarch64-linux` with no source change.

### 2. ~~A story for using other people's code~~ — **DECIDED 2026-08-15: vendoring, Odin-style**

Follow Odin and Go: vendor dependencies as directories you commit. No package
manager, no registry, no lockfile. A non-`core:` import resolves relative to
the importing file, so `vendor/` works by construction; `tests/pkg/vendor_deps/`
gates both shapes. The convention is in
[packages](docs/reference/packages.md#vendoring). Cost: transitive dependencies
by hand, no mechanical security-fix notice.

### 3. The promise, written down

The deprecation machinery is exercised end to end. [SUPPORT.md](SUPPORT.md)
states what may break in which release, that a deprecation survives until the
next major, that Linux x86_64 is the one supported platform, and that there is
no support window — with the plain note that 1.0-onward promises cannot yet be
relied on at 0.x.

### 4. Trust that did not come from here

§1 and §7 above survive the reframe unchanged. Shipping to strangers is where
an unaudited FFI boundary is a liability; [audit-brief](docs/internals/audit-brief.md)
is the packet for the second.

## Non-goals

Several things are out of scope — traits, a package manager, a
C-style ternary, Hindley-Milner inference, refcounting/GC, and hosted CI among them. The
full list is in [docs/architecture.md](docs/architecture.md#decided-non-goals).
New directions are welcome; re-proposals of these need new evidence.

Traits were reopened and re-closed 2026-09-11; see
[architecture](docs/architecture.md#traits--typeclasses--reopened-2026-09-11-re-closed-the-same-day).
