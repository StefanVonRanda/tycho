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

None open. Item 8 stays open in the open list by design — the phase's
deliverable was the costing, and it is `docs/rfc/parallel-for-width.md`.
This file is finished and should be deleted rather than archived.
