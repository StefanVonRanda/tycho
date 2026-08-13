# FRICTION triage — re-probe the backlog before writing another program

## Goal
Every unresolved entry in `docs/internals/FRICTION.md` was written from reasoning
and never re-probed. #27 was withdrawn on 2026-08-13 after three probes showed
its central claim false, having cost a full phase first. This plan probes the
remaining eight, then corrects or fixes each.

## Pre-flight
- Worst case: a phase "fixes" something that was never broken, or narrows a
  deliberate design. Mitigation: every phase RE-PROBES before touching code and
  may close the entry as wrong instead of building anything.
- Reversibility: entries are corrected in place, never deleted — #27's wrong
  reasoning stays on record beside what actually holds.
- Verified 2026-08-13 by reading the index: 8 entries carry no resolution marker.
- The file's convention, confirmed at `docs/internals/FRICTION.md:1331` vs
  `docs/internals/FRICTION.md:1362`, and `docs/internals/FRICTION.md:1396` vs
  `docs/internals/FRICTION.md:1420`: a closed entry keeps a struck-through
  heading with the resolution, and the ORIGINAL entry with its measured evidence
  is preserved directly below it. Those pairs are not duplicates. Do not "clean
  them up" — the second copy is the data.
- Assuming: each entry's own repro still compiles. Several predate language
  changes, so a repro that no longer parses is itself the finding.

## Phases

- [ ] **Phase 1 — `bytes` slices clamp, so a slice is not a bounds check** (`:1435`)
  - Probe first: does an out-of-range `bytes` slice still clamp silently?
  - If it reproduces: decide against Go/Odin (both panic on out-of-range slice)
    before proposing a change; a clamp that matches neither is the finding.
  - Verify: `make test` if `src/tychoc.c` moves, else the two doc gates.

- [ ] **Phase 2 — `core:json` accepts input it cannot represent** (`:1738`)
  - Three claimed sightings. Probe each separately; they may not all still hold.
  - Verify: `make corelib`, plus `make q-check` if `core:json` behaviour moves.

- [ ] **Phase 3 — `core:decimal` has no `div`** (`:1849`)
  - Probe that it is still absent, then decide: add `div` with an explicit scale
    and rounding mode, or record why a decimal `div` is refused. Do not add a
    `div` that silently picks a scale.
  - Verify: `make corelib`, `make q-check`.

- [ ] **Phase 4 — `iter.try_map` has no `Result(void, E)` shape** (`:2306`)
  - Entry 4 in an earlier section claimed `Result(void, E)` was inexpressible and
    was CLOSED; check whether that closure already covers this one.
  - Verify: `make corelib`.

- [ ] **Phase 5 — a `string` across the FFI truncates at its first NUL** (`:2376`)
  - Silent truncation at a trust boundary. Probe it, then decide whether the FFI
    should refuse an embedded NUL rather than truncate. Fail loud beats fail short.
  - Verify: `make ffi`, `make test`.

- [ ] **Phase 6 — a two-key comparator cannot be written inline** (`:2486`)
  - `core:sort` gained a comparator-taking sort (`:1913`, CLOSED 2026-08-10).
    Probe whether that already answers this; the entry may predate it.
  - Verify: `make corelib`.

- [ ] **Phase 7 — a first `--shim` C file must hand-declare `tycho_int`** (`:2540`)
  - Probe whether a generated header now exists. If not, decide whether to emit
    one — this is ergonomics, so a correction saying "deliberate" is a fine
    outcome.
  - Verify: `make shim-check`, `make corelib`.

- [ ] **Phase 8 — a `for` binding does not destructure a tuple** (`:2603`)
  - Probe both halves: destructuring in a `for` binding, and whether a tuple is
    indexable. Go and Odin both destructure in range/multi-return position, so
    check that default before recording this as deliberate.
  - Verify: `make test`.
