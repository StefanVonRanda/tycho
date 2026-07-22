# Runtime drift guard, then region-lifetime research

## Goal

Two independent pieces of work, in order. (1) A gate that fails when
`runtime/tycho_rt.c` and the runtime `compiler/tychoc0.ty` emits as string
literals diverge in feature surface — the drift that let `TYCHO_ARENA_STATS`
exist in one and be a silent no-op in the other. (2) A written, source-grounded
design study of arenas whose lifetime is tied to a value rather than a lexical
scope — the gap that forces copy-up or a flat index pool for long-lived
structures. Done = (1) is a lane in `make ci` proven to fail on injected drift;
(2) is an RFC under `docs/rfc/` with a concrete design and an honest feasibility
verdict, no implementation.

## Pre-flight

- Worst case: Phase 1 adds a gate that fails on legitimate, intentional
  differences between the two runtimes (they are NOT meant to be textually
  identical — different struct names, `HBlock` vs `ABlk`, different helper
  spellings), turning `make ci` red for everyone until it is tuned or reverted.
  Phase 2 is a document; its worst case is wasted effort.
- Reversibility: both are additive and git-tracked. Phase 1 touches test
  infrastructure only — no compiler, no runtime, no generated code.
- Verified: the two runtimes are separate sources — `runtime/tycho_rt.c:86`
  (`HBlock`/`Arena`) vs `compiler/tychoc0.ty:9539+` (emitted `ABlk`/`Arena` as
  string literals); `tychoc` embeds its runtime verbatim via `build/tycho_rt_embed.h`
  (`Makefile:23-26`, `src/tychoc.c:26`); the drift was real and shipped
  (`TYCHO_ARENA_STATS` absent from tychoc0's runtime until commit `2b24ca6`);
  `make fixpoint` compares tychoc0 against ITSELF byte-for-byte and only compares
  tychoc0 to tychoc BEHAVIOURALLY (`compiler/fixpoint.sh:16-30`), so it cannot
  see this class of drift.
- Assuming: a feature-surface comparison (exported runtime function names, env
  vars, abort messages) is a good enough proxy for "these two runtimes offer the
  same behaviour". It is not a semantic equivalence proof and will not catch two
  same-named functions that behave differently.

## Phases

- [ ] **Phase 1 — Runtime drift gate**
  - Scope: test/build infrastructure only (`tests/`, `scripts/`, `Makefile`).
    Must not modify `runtime/tycho_rt.c`, `compiler/tychoc0.ty`, or `src/tychoc.c`.
  - Design freedom: the comparison key is the agent's call — candidates are the
    set of runtime function names, the set of `getenv` names, and the set of
    `tycho:` abort message texts, extracted from `runtime/tycho_rt.c` and from
    the C that `tychoc0` emits for a trivial program. Intentional spelling
    differences (`HBlock`/`ABlk`, `blk_get`/`block_get`) must be handled by an
    explicit, commented allowlist — never by weakening the comparison to nothing.
  - Done when: a new gate reports parity today, is wired into `make ci`, and
    FAILS when drift is injected.
  - Verify: run it green on HEAD; then inject drift (e.g. delete one
    `getenv("TYCHO_...")` handler from tychoc0's emitted runtime in a scratch
    copy, or add a runtime function to one side only) and show the gate failing
    with a message naming the missing symbol; restore and show green again.
    Paste both outputs under this phase.

- [ ] **Phase 2 — Value-lifetime region design study (RFC, no implementation)**
  - Scope: one new document under `docs/rfc/`. No compiler or runtime changes.
  - Content: state the gap with evidence from this repo (where scope-bound
    arenas force copy-up or the flat index-pool idiom, citing
    `docs/internals/value-semantics-limits.md` and real call sites); survey how
    the current model handles long-lived dynamic structures; propose at least
    two concrete designs for an arena whose lifetime follows a value, each with
    its effect on the "no pointer crosses sideways" invariant, on `spawn`/
    channel copy-in, and on the C lowering; give an honest verdict on whether
    it is compatible with the 1.0 spec freeze (`docs/internals/spec-plan.md`)
    or would require breaking it.
  - Done when: the RFC exists, every claim about current behaviour cites
    `path:line`, and it ends with a recommendation (build / don't build / build
    behind what condition).
  - Verify: `make check-links` green; each `path:line` citation in the document
    resolves to the claimed construct — spot-check every one and record the
    check under this phase.
