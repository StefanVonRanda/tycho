# Internals

Maintainer notes. Not user documentation — these record decisions, dead ends and
measurements, and they assume you already know the codebase.

## Design notes

- [`value-semantics-limits.md`](value-semantics-limits.md) — where the memory
  model costs, and why those costs were accepted.
- [`design-aggregate-ref.md`](design-aggregate-ref.md) — aggregate references.
- [`design-scalar-match.md`](design-scalar-match.md) — scalar patterns in `match`.

## Records

- [`FRICTION.md`](FRICTION.md) — the running log of what fought back while
  writing real programs against this language. The primary source for what is
  actually hard here.
- [`audit-brief.md`](audit-brief.md) — what a third-party reviewer would need.
- [`ffi-review-2026-08-14.md`](ffi-review-2026-08-14.md) — an FFI boundary review.

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
