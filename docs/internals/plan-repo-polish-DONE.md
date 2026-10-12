# Repo polish, after the testing campaign

**Status: DONE 2026-08-04. The eight-program testing campaign is complete: eight programs across six axes, zero compiler/runtime defects, six tool-gate lanes, and the byte-for-byte proof. Outcome: TODO sweep clean, LICENSE/SECURITY/CONTRIBUTING present, README evidence section added, CI stays local-only by owner decision.**

> 2026-08-04: the eight-program testing campaign is declared complete. Eight
> programs across six axes (data structures, bits, arithmetic, networking +
> shared-state concurrency, algorithmic depth, file I/O), zero compiler/runtime
> defects, six tool-gate lanes, and a differential fuzzer, and the
> byte-for-byte proof. The completed plans live at
> `docs/internals/plan-tycho-sat-DONE.md` and the six before it; the fresh
> clone that proposed a build tool (the last untested axis, systems I/O) is
> superseded by this polish turn -- that axis stays filed as the next program
> if testing ever resumes.
>
> The rule from the program plans carries over: a finding becomes a phase only
> when a second, independent caller exists.

## The work

The repo measures clean (1224 commits, 894 `.ty` files, ~9 grep hits that
turn out to be false positives -- see phase 1). The polish gaps, in priority
order: a LICENSE (the legal blocker), a GitHub Actions workflow running `make ci`, a README section documenting the seven tool
programs as the evidence of exercise, and a CONTRIBUTING.md.

## Phases

### Phase 1 -- sweep the TODO/FIXME markers  [DONE 2026-08-04]

The tree carries **no TODO/FIXME/HACK markers at all**. A whole-word scan of
every `.ty`, `.md`, `.c`, `.sh` and `.py` file (including `src/tychoc.c` and
`runtime/tycho_rt.c`) returns zero matches. The ~9 hits that motivated the
phase are all substring false positives: `\uXXXX` -- the JSON escape
notation, which contains `XXX` -- appears seven times in
`corelib/json/json.ty`, once in `corelib/test/json/main.ty` and once in a
`docs/internals/FRICTION.md` record line; `to_XXX` in `tests/sized_family.ty:2` is the
placeholder spelling for the conversion-function family. None is a marker.
Nothing to fix; the scan is the deliverable.

### Phase 2 -- LICENSE / SECURITY / CONTRIBUTING  [DONE 2026-08-04 -- already present]

### Phase 3 -- README: the seven tools as evidence of exercise  [DONE 2026-08-04]

Added the "Eight programs that tested the language" subsection under The
evidence: the seven tool programs with the axis each stressed and its
ground-truth gate, plus the one finding the campaign did produce (hex
literals, filed by the chess engine's castling masks).

### Phase 4 -- the CI decision  [DONE 2026-08-04 -- local-only]

The owner's call: no GitHub Actions. The local-only stance documented in the README and
CONTRIBUTING stands unchanged; no workflow is added and no claim is
rewritten. The repo is polished as-is.
