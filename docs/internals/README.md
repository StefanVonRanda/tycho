# Internals

Maintainer notes. Not user documentation — these record decisions that the code
cannot state for itself, and they assume you already know the codebase.

- [`value-semantics-limits.md`](value-semantics-limits.md) — where the memory
  model costs, and why those costs were accepted.
- [`design-aggregate-ref.md`](design-aggregate-ref.md) — aggregate references.
- [`design-scalar-match.md`](design-scalar-match.md) — scalar patterns in
  `match`. Cited by name in a compiler error message, so it is load-bearing:
  see `src/tychoc.c@is_builtin_name`'s neighbourhood and the `match` refusal.
- [`windows-port.md`](windows-port.md) — the design record of the native
  Windows port. Cited by `tests/run.sh`, `scripts/ci.sh` and the wine lanes.

Dated probes, completed `plan-*-DONE.md` campaigns, point-in-time audits and the
8,382-line FRICTION log were deleted on 2026-09-20. They were snapshots of a tree
that has moved, and git history holds them. The 25 assertions FRICTION carried
that no suite covers now live in `scripts/check_pins.py`, where they run in
`make ci` instead of being narrated.
