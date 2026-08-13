# tycho-sim — a deterministic entity simulation

## Goal
A tick-driven simulation over `soa` component storage, exercising the three
features no real program uses: `soa` (1 program today), `subscript` (0), `sink`
(0). Integer/logic workload only — deliberately away from float text, where the
last two fixes came from. Output is a tick transcript that goldens byte-exactly.

## Pre-flight
- Worst case: the program compiles and runs, finds nothing, and costs a week.
  Mitigation: every slice reports which compiler/runtime defects it surfaced;
  a slice that surfaces none says so rather than padding.
- Reversibility: new files under `tools/tycho-sim/` only. Nothing existing moves.
- Verified: `soa [Ent]` indexes natively (`s[i].hp`), monomorphic `subscript`
  works, `subscript` over `soa` works, `subscript` across a package boundary
  works — all four probed 2026-08-13.
- Verified 2026-08-13: `sink` cannot take a collection built with `push`.
  `can_move_into_sink` (`src/tychoc.c@can_move_into_sink`) requires exactly one
  read in the whole body, so pools use `inout`, not `sink`. The diagnostic that
  misdescribed that rule was fixed on 2026-08-13 (FRICTION #24).

## Phases

- [ ] **Phase 2 — sys/ systems and the tick loop**
  - Scope: movement, combat, decay, spawn/despawn over the pools.
  - Done when: transcript byte-identical over two runs and at two `TYCHO_THREADS`
    values; entity-count and energy conservation asserted against literals in the
    runner, not against a slice of the golden.
  - Verify: `make sim-check`.
