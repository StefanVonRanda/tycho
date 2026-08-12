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

## Phase 2 — reject the remaining 28 builtin names at their declaration

*Filed by the spec rewrite (`c81e896`), deliberately not landed with it.*

`docs/spec/01-lexical.md` §3.7 now says declaring a procedure with a builtin's
name **is an error**, and carries a `> **gap:**` block admitting the reference
implementation enforces that for only 26 of the 54 names on
`src/tychoc.c@shadows_builtin`. The other 28 declare with a warning and are then
overtaken by the builtin at the call site — sometimes silently (`to_u8(5)`
returns the builtin's `5`; the local body is never entered). Closing this
deletes the `gap:` block.

**Why it was not landed with the spec.** It newly rejects
`corelib/json/json.ty@keys` — shipped public API, called as `json.keys(obj)` —
plus any user code of the same shape. A breaking change to a published package
is a separate decision from "what does the spec say".

**Sizing.** The definition-time duplicate check is one condition at
`src/tychoc.c:8153` (`sig_find(pr->name) || consts_find(pr->name)`), and the 26
already-rejected names are exactly those with real `g_sigs` entries from
`src/tychoc.c@register_builtins`. `send`/`recv`/`close` have no entry at all —
they are recognised ad hoc during resolution — so this is not a table missing
rows. The generic path at `src/tychoc.c:8147` consults the same tables plus
`generic_find`, so whatever lands has to be written twice.

**Prerequisite, not optional:** rename `core:json`'s `keys` (to `object_keys` or
similar) keeping the old name as a deprecated forwarder, or accept the break and
bump. Sequence the corelib rename **before** the compiler change, or
`make corelib` reddens by construction.

**Verify:** `make corelib`, `make corelib-examples`, `make test`,
`sh scripts/entrypoints.sh`, plus the doc gates for removing the `gap:` block.
