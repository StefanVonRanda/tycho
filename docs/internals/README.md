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
- [`design-corelib.md`](design-corelib.md) — corelib handle guards, path layering, and shim conventions.
- [`design-subscripts.md`](design-subscripts.md) — subscript lowering and rules.
- [`design-enums.md`](design-enums.md) — enum payloads and error operators.
- [`design-functions.md`](design-functions.md) — closure capture and `sink` adoption.
- [`design-scalar-match.md`](design-scalar-match.md) — scalar patterns in `match`.

## Records

- [`FRICTION.md`](FRICTION.md) — the running log of what fought back while
  writing real programs against this language. The primary source for what is
  actually hard here.
- [`audit-brief.md`](audit-brief.md) — what a third-party reviewer would need.
- [`ffi-review-2026-08-14.md`](ffi-review-2026-08-14.md) — an FFI boundary review.
- [`audit-2026-08-16.md`](audit-2026-08-16.md) — security audit across the whole
  threat model: two vulnerabilities found and fixed, and what it did not cover.
- [`audit-2026-09-25.md`](audit-2026-09-25.md) — a defect hunt across the compilers,
  runtime, shims, network stack, tycho-db and parsers: 90 baseline defects recorded,
  all 90 fixed, none open.
- [`roadmap-closed-2026-08.md`](roadmap-closed-2026-08.md) — the 1.0 conditions
  closed in August 2026, moved out of `ROADMAP.md`.
- [`windows-port.md`](windows-port.md) — the design record of the native
  Windows port. Cited by `tests/run.sh`, `scripts/ci.sh` and the wine lanes.

## Completed plans

Each `plan-*-DONE.md` is the finished record of one campaign, kept for its
evidence rather than its instructions:
[repo-polish](plan-repo-polish-DONE.md),
[chess](plan-tycho-chess-DONE.md),
[kv](plan-tycho-kv-DONE.md),
[kvsrv](plan-tycho-kvsrv-DONE.md),
[rsa](plan-tycho-rsa-DONE.md),
[sat](plan-tycho-sat-DONE.md),
[scheme](plan-tycho-scheme-DONE.md),
[scheme-compiler](plan-tycho-scheme-compiler-DONE.md),
[vm](plan-tycho-vm-DONE.md).
