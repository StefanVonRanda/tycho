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
  read in the whole body, so pools use `inout`, not `sink`. Phase 3 is now the
  diagnostic, not the transfer.

## Phases

- [ ] **Phase 2 — sys/ systems and the tick loop**
  - Scope: movement, combat, decay, spawn/despawn over the pools.
  - Done when: transcript byte-identical over two runs and at two `TYCHO_THREADS`
    values; entity-count and energy conservation asserted against literals in the
    runner, not against a slice of the golden.
  - Verify: `make sim-check`.

- [ ] **Phase 3 — the `sink` diagnostic describes a rule it does not implement**
  - Scope: `src/tychoc.c@sink_arg_into`'s message, and `docs/internals/FRICTION.md`.
  - Done when: a variable whose sink pass IS its last use but which was read
    earlier gets a message naming the real rule (one read in the whole body),
    not "make this its last use", which it already is.
  - Verify: a reject fixture pinning the new text; `make test`, expect 662.
  - Probed: `len(w)`, `w[0]`, `bump(&w)`, `w[0] = 9` and `push(w, x)` each
    disqualify. Only `w := [5,7]` immediately consumed is accepted.
