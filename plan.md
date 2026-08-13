# The open list — re-scored 2026-08-13, then worked

## Goal
`docs/internals/FRICTION.md:206` ("The real remaining debt") was last scored
2026-07-31 against `9e8f8f2`. Seven items stood unstruck. All seven were
re-probed 2026-08-13 at `c4e4c784` before this plan was written: **three are
closed** and four survive. This plan strikes the three, then works the four in
cheapest-first order.

## Pre-flight
- Worst case: a phase "fixes" an item that closed weeks ago, or strikes one that
  still reproduces. Mitigation: the re-probe is recorded per item below and every
  phase re-runs its own probe before touching anything.
- Reversibility: items are struck in place with their evidence, never deleted —
  the numbering is frozen on purpose (`:218`).
- Verified 2026-08-13 by running each probe; items 6, 8, 10, 13 reproduce.
- Assuming: the closed three need only a strike-through, no code.

## Phases

- [ ] **Phase 4 — item 6: may a leaf package import `core:strings`?**
  - A layering decision, not lines: `core:httpd` hand-rolls `has_ext` rather than
    import `core:strings` for `ends_with`. Precedent cuts both ways — `core:io`
    dropped a dependency, `core:sort` gained one.
  - Decide and write the rule down where a future package author will read it;
    "deliberate, leaf packages stay dependency-free" is a fine outcome.
  - Verify: `make corelib` only if an import changes; otherwise the doc gates.

- [ ] **Phase 5 — item 8: no direct spelling for N workers**
  - `[spawn work(1), spawn work(2)]` is refused by design (a handle cannot escape
    to be waited twice or never). The ask is a width slot on `parallel for`, or
    task handles in a container with the affine rule preserved.
  - This is a type-system change and the item says so. Scope it as a written
    design before any code; a phase that only produces the design is complete.
  - Verify: `make test` and `make conc` if anything lands in `src/tychoc.c`.
