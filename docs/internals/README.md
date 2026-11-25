# Internals

Maintainer notes. Not user documentation — these record decisions, dead ends and
measurements, and they assume you already know the codebase.

## Design notes

- [`value-semantics-limits.md`](value-semantics-limits.md) — where the memory
  model costs, and why those costs were accepted.
- [`design-aggregate-ref.md`](design-aggregate-ref.md) — aggregate references.
- [`design-aggregates.md`](design-aggregates.md) — how arrays and structs behave
  under value semantics with implicit arenas, and why no borrow checker is needed.
- [`design-maps.md`](design-maps.md) — map values and in-place mutation.
- [`design-generics.md`](design-generics.md) — generics monomorphization.
- [`design-concurrency.md`](design-concurrency.md) — concurrency runtime, channel core, and measurement.
- [`design-ffi.md`](design-ffi.md) — FFI ownership, prototypes, and coverage.
- [`tir.md`](tir.md) — the typed IR (`tychoc --emit-tir`) for back ends that are not C.
- [`design-corelib.md`](design-corelib.md) — corelib handle guards, path layering, and shim conventions.
- [`design-gfx.md`](design-gfx.md) — `core:gfx`'s values-and-handles-only boundary, ownership and thread rules.
- [`design-subscripts.md`](design-subscripts.md) — subscript lowering and rules.
- [`design-enums.md`](design-enums.md) — enum payloads and error operators.
- [`design-functions.md`](design-functions.md) — closure capture and `sink` adoption.
- [`design-scalar-match.md`](design-scalar-match.md) — scalar patterns in `match`.
- [`design-strings-v2.md`](design-strings-v2.md) — the `{ptr,len}` and
  small-string design and probe that the open Goose phase 4.4 executes.
- [`goose-review.md`](goose-review.md) — which Goose design ideas transfer to
  Tycho, with the incompatible ones named.

## Records

- [`corelib-parity.md`](corelib-parity.md) — Odin `core` capability map and the
  explicit non-goals.
- [`1.0-readiness.md`](1.0-readiness.md) — the 2026-11-12 readiness snapshot:
  internal gates, current performance, and the two external requirements.
- [`archived-c-compiler.md`](archived-c-compiler.md) — the deleted C compiler,
  the revision its citations resolve against, and the frozen reject-site index.
- [`FRICTION.md`](FRICTION.md) — the running log of what fought back while
  writing real programs against this language. The primary source for what is
  actually hard here.
- [`audit-brief.md`](audit-brief.md) — what a third-party reviewer would need.
- [`probe-procedure.md`](probe-procedure.md) — how to run an outside-program
  probe, and which surfaces earlier probes covered.
- [`probe-flat-state-2026-11-13.md`](probe-flat-state-2026-11-13.md) — flat state
  vs nested values; cited by `bench/flat-state/`, two follow-ups still open.
- [`probe-compact-array-2026-11-17.md`](probe-compact-array-2026-11-17.md) — the
  measurement half of the open compact-array phase in `plan-goose-transfers.md`.
- [`runtime-tuning.md`](runtime-tuning.md) — the per-scope arena tuning diary
  behind [`docs/performance.md`](../performance.md).
- [`windows-port.md`](windows-port.md) — the design record of the native
  Windows port. Cited by `tests/run.sh`, `scripts/ci.sh` and the wine lanes.

Finished plans, probes, audits and reviews are deleted once closed; git history
keeps them (for example `git show ba952872:docs/internals/audit-2026-09-25.md`,
or `git log --diff-filter=D --name-only -- docs/internals`).

## Active plans

- [`plan-value-storage-places.md`](plan-value-storage-places.md) — storage reuse,
  the `inout` append fix, and follow-up evidence/documentation corrections.
- [`plan-goose-transfers.md`](plan-goose-transfers.md) — the five Goose
  transfers; phases 3.1, 4.4, 5 and the wrap-up are open.
