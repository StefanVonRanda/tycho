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

None open. Every entry this plan set out to re-probe has been closed in
`docs/internals/FRICTION.md`; the evidence is in the commits that closed each
one. This file is finished and should be deleted rather than archived.
