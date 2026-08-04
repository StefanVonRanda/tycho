# Repo polish, after the testing campaign

> 2026-08-04: the eight-program testing campaign is declared complete. Eight
> programs across six axes (data structures, bits, arithmetic, networking +
> shared-state concurrency, algorithmic depth, file I/O), zero compiler/runtime
> defects, six tool-gate lanes, a differential fuzzer, and the self-hosting
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
order: a LICENSE (the legal blocker -- the repo is all-rights-reserved
without one), a GitHub Actions workflow running `make ci` (the rigor becomes
visible instead of asserted), a README section documenting the seven tool
programs as the evidence of exercise, and a CONTRIBUTING.md (the PR template
exists; the gate/citation/demand-rule conventions are not written down).

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
Nothing to fix; the scan is the phase's deliverable and its own finding
(a naive `grep XXX` over this tree is noise, not debt).

### Phase 2 -- LICENSE / SECURITY / CONTRIBUTING  [DONE 2026-08-04 -- already present]

The phase's premise was wrong: the survey that motivated it missed that
`LICENSE` (MIT, "The Tycho Authors", 2026), `SECURITY.md` and
`CONTRIBUTING.md` all already exist and are complete -- the README's License
section links all three. CONTRIBUTING covers the local gate, the frozen
tychoc0 rule, the arena-model rule, feature-work scope and code style.
Nothing to add.

### Phase 3 -- README: the seven tools as evidence of exercise  [DONE 2026-08-04]

Added the "Eight programs that tested the language" subsection under The
evidence: the seven tool programs with the axis each stressed and its
 ground-truth gate, plus the one finding the campaign did produce (hex
literals, filed by the chess engine's castling masks).

### Phase 4 -- the CI decision  [DONE 2026-08-04 -- local-only]

The owner's call, recorded verbatim: "I will not and will never use github
actions for anything." The local-only stance documented in the README and
CONTRIBUTING stands unchanged; no workflow is added and no claim is
rewritten. The repo is polished as-is.
