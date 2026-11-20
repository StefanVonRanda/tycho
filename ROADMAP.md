# Roadmap

Tycho is **0.9.2 — pre-1.0**, and this file is a direction, not a promise of
dates. Day-to-day work is tracked in [GitHub
issues](https://github.com/StefanVonRanda/tycho/issues); this file is the
high-level shape. What 1.0 now requires is [below](#what-10-requires).
Terms (`surface.lock`, `tychoc`, `bootstrap/tycho_compiler.c`, lanes, FRICTION)
are defined in [STATUS](STATUS.md).

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

`compiler/` is Tycho written in Tycho — 26,036 lines in six packages (`lex`,
`parse`, `ast`, `types`, `emit`, `driver`), built as `tychoc1` and installed as
`tychoc`. The test, corelib, example and server runners default to it, so the
gates run on the self-hosted compiler. `bootstrap/tycho_compiler.c` is the
generated cold-start artifact, not a second implementation.

- **The bootstrap is two-stage and the fixpoint holds**, gated as
  `make bootstrap-check` and `make fixpoint-check`.
- **Decided 2026-11-09: one maintained compiler.** The self-hosted compiler is
  the reference implementation; the spec is normative. `src/tychoc.c` is
  deleted; its source remains in git history at commit
  `c7d46528ac6efcf9dc93359d14d98bedcb7ddbad`, and historical records cite it
  through
  [`docs/internals/archived-c-compiler.md`](docs/internals/archived-c-compiler.md).

## The language surface moved once, deliberately

Since 2026-08-22 the keyword, builtin and corelib surface is locked by
`surface.lock` and gated by `make surface-check`, with no new language
features before 1.0. The lock broke once, on purpose in 0.8.5, for layout and
SIMD (`packed`, `align(N)`, `vector[N]T`, simultaneous assignment, swizzling);
it records additions rather than forbidding them.

Corelib may still gain functions; it may not lose one or change a signature.
Since 2026-09-03 a gained function must be recorded in the same commit.
To record deliberately: `python3 scripts/surface_lock.py --record`.

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

## Benchmark follow-ups (2026-10-22)

The generated suite in [`bench/RESULTS.md`](bench/RESULTS.md) makes the
remaining performance work measurable. These are engineering targets, not
promises of dates.

**Update 2026-11-10:** `json_tree` is 51 ms against C's 49 ms, `lru` is 226 ms
and 14.7 MB against C's 233 ms and 12.0 MB, and `trie` is 89 ms GCC / 90 ms
clang against C's 79 / 74 ms. The remaining gaps are structural (generic-map
duplication and the last few percent of trie/dijkstra), not missing language
work. See [`docs/internals/1.0-readiness.md`](docs/internals/1.0-readiness.md).

Follow-up work after the benchmark refresh:

- **`json_tree` fast path:** PARTIAL — a single-scope flat-record parser
  removes most per-call arena overhead, and destination-arena construction now
  moves each object's key/value arrays into the returned array instead of
  copying them, key/value strings are sliced once instead of built byte by
  byte, and repeated keys are interned (GCC 84 ms / 81 MB vs C 59 / 103; clang
  61 / 81 vs C 63 / 102). The move is gated on the constructor push being the
  array's last use and on the destination escaping the function;
  `tests/dest_move_last_use.ty` pins the move and the copy-after-constructor
  case. It remains a native-time diagnostic, not a managed-language loss.

## Known remaining performance gaps

- **CLI hash process memory:** `tycho-hash` now matches Go's stdlib on time
  through SHA-NI or optional OpenSSL, but is still ~1.5x heavier on a 128 MB
  tree. Channel memory is fixed; the remaining cost is the per-worker file
  buffer and runtime working set. See [`bench/REALWORLD.md`](bench/REALWORLD.md).
- **Minimal single-loop server:** does not scale with connections; use the
  production multi-worker `server/main.ty`, which is competitive with Go.
- **Native C/Rust:** still faster on some recursive/pointer-heavy rows,
  especially `json_tree`, `trie` and `dijkstra`. This is diagnostic, not a
  managed-language blocker.

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
(record: `git show ba952872:docs/internals/probe-ffi-2026-08-19.md`) — each with its
`FRICTION-OUTSIDE.md`. Left open deliberately: the probes keep finding what
the author could not, and generics, `subscript`, `bounded[N]`, `select` and
error paths still have no non-author program
([procedure](docs/internals/probe-procedure.md)).

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
- With `zig cc` as cross driver, the compiler and the emitted C build for
  `aarch64-macos`, `x86_64-macos` and `aarch64-linux` with no source change.

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
Runtime-internal double-headed / single-block packing is being evaluated now,
without a surface change; scenario evidence and targeted opportunities are in
[plan-value-storage-places](docs/internals/plan-value-storage-places.md).
A measured local optimization need not wait for 1.0; no default allocator rewrite is approved.
The compiler follow-up removes a duplicate AST adapter and reuses an existing
append builder: about 27% lower capacity and 25% lower RSS on a frozen compiler
input, with unchanged emitted C; no demonstrated latency gain. Controls and
remaining limits are recorded in that same plan.

Traits were reopened and re-closed 2026-09-11; see
[architecture](docs/architecture.md#traits--typeclasses--reopened-2026-09-11-re-closed-the-same-day).
