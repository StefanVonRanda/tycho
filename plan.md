# FRICTION triage — re-probe the backlog before writing another program

## Goal
Every unresolved entry in `docs/internals/FRICTION.md` was written from reasoning
and never re-probed. #27 was withdrawn on 2026-08-13 after three probes showed
its central claim false, having cost a full phase first. This plan probes the
remaining seven, then corrects or fixes each.

## Pre-flight
- Worst case: a phase "fixes" something that was never broken, or narrows a
  deliberate design. Mitigation: every phase RE-PROBES before touching code and
  may close the entry as wrong instead of building anything.
- Reversibility: entries are corrected in place, never deleted — #27's wrong
  reasoning stays on record beside what actually holds.
- Verified 2026-08-13 by reading the index: 7 entries carry no resolution marker
  (was 8; entry 5, the `bytes` slice clamp, was closed as deliberate).
- **That count was read from the index and the index is not the entry.** The
  `core:json` phase found its entry already carrying a `[FIXED, 2026-08-01]`
  banner *and* a 2026-08-11 re-probe, neither of which the index shows. Check the
  entry itself before assuming a phase has work in it.
- **The banner is not spelled one way.** `core:decimal`'s said `[CLOSED …]`, and
  a detector grepping `[FIXED]` reported it open on 2026-08-13. Grep for the
  bracketed word, not the word you expect. Six entries remain unprobed, not seven.
- The file's convention, confirmed at `docs/internals/FRICTION.md:1331` vs
  `docs/internals/FRICTION.md:1362`, and `docs/internals/FRICTION.md:1396` vs
  `docs/internals/FRICTION.md:1420`: a closed entry keeps a struck-through
  heading with the resolution, and the ORIGINAL entry with its measured evidence
  is preserved directly below it. Those pairs are not duplicates. Do not "clean
  them up" — the second copy is the data.
- Assuming: each entry's own repro still compiles. Several predate language
  changes, so a repro that no longer parses is itself the finding.

## Phases

- [ ] **Phase 7 — a `for` binding does not destructure a tuple** (`docs/internals/FRICTION.md:2872`)
  - Probe both halves: destructuring in a `for` binding, and whether a tuple is
    indexable. Go and Odin both destructure in range/multi-return position, so
    check that default before recording this as deliberate.
  - Verify: `make test`.

- [ ] **Phase 8 — `make docs-fences` is RED on main** (found 2026-08-13, out of
  phase 6's scope)
  - `docs-fences: FAIL docs/internals/FRICTION.md:2650 -- does not compile`,
    `<fence>:4: error: expected an expression | 4 | ...`. The ```` ```tycho ````
    fence in entry #10's withdrawal elides a branch with a literal `...`, and
    docs-fences compiles it. Introduced by `fe150d4`, the commit before this one;
    confirmed pre-existing by `git stash` + re-run, same 54/76/1 both ways.
  - Fix is one of the runner's own skip markers (see the `[MARKED]` / `[FRAGMENT]`
    reasons it already prints) or spelling the elided branch out.
  - Verify: `make docs-fences` only. Do not run `make test` or `make ci`.
