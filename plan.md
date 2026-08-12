# Open work

> **This file holds only what is NOT done.** A phase is deleted when it is
> ticked, not archived here — its evidence lives in the commit that closed it,
> where it is attached to the diff it describes and `git log -S` can find it.
>
> Trimmed 2026-08-12. It had reached 1,396 lines, of which 1,299 were evidence
> under 19 finished phases against 2 open ones: 93% archive. That is the second
> time in two days the file needed rotating, so the convention changed rather
> than the file — see CLAUDE.md, "Plans". `git show 3bb16fd:plan.md` recovers
> the long version; every phase's evidence is also in its own commit.

## Phase 1 — the whole-document citation drift

*Split out of the citation repair that fixed only its enumerated bullets
(`e6014db`, then the prose repairs in `2206b1c`).*

Five documents have drifted as a block, every ref pointing into `src/tychoc.c`
or `runtime/tycho_rt.c`:

- `docs/spec/15-program.md` (15 refs; `main` cited at a map typedef)
- `docs/internals/design-scalar-match.md` (the `S_MATCH` pass resolves to a bare `}`)
- `docs/rfc/value-lifetime-regions.md`
- `docs/internals/value-semantics-limits.md`
- `docs/rfc/limited-references-spike.md`

**Do it as a conversion, not a repair.** Re-pointing bare `path:N` refs lasts
until the next compiler phase moves the lines again. Converting them to anchored
`path:N@token` or `path@SYMBOL` form makes `scripts/check_citations.py` police
them permanently. A bare ref into a 12k-line file can never fail a bounds check,
which is exactly why this rot was invisible.

**Verify:** `make check-links` — and it must redden if an anchor is wrong, so
prove that on one ref before converting the rest.
