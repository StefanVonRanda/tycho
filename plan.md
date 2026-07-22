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

- [x] **Phase 1 — Runtime drift gate**
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

  ### Result

  Built `tests/rtparity/run.py` + `tests/rtparity/surface.ty`, wired as
  `make rtparity` (Makefile:212-221) and CI step 4/19 (scripts/ci.sh), placed
  right after `fixpoint` because it closes fixpoint's blind spot. Nothing
  outside `tests/`, `scripts/`, `Makefile` was changed.

  The lane compiles ONE broad probe with both compilers and diffs three
  user-visible surfaces of the emitted C — env knobs (`getenv("TYCHO_*")`),
  `tycho:` trap texts, and `TYCHO_ARENA_STATS` row labels. Those keys were
  chosen because both runtimes must spell them identically (they are the
  contract), so the deliberate internal spelling differences — `HBlock`/`ABlk`,
  `block_get`/`blk_get`, `tycho_str_concat`/`sc`, `stats_dump`/`st_dump` —
  cannot reach the comparison at all and need no allowlist entry.

  #### 1. Green on HEAD

  ```
  $ make rtparity
  rtparity: env knobs         3 shared, 0 allowlisted difference(s) (ok)
  rtparity: diagnostics      18 shared, 9 allowlisted difference(s) (ok)
  rtparity: arena-stats rows  5 shared, 0 allowlisted difference(s) (ok)
  rtparity: the two runtimes agree on env knobs, diagnostics and arena stats
  exit=0
  ```

  Non-vacuity, measured: env = {TYCHO_ARENA_STATS, TYCHO_MAX_TASKS,
  TYCHO_THREADS} on both sides; diagnostics 27 (tychoc) vs 18 (tychoc0);
  rows = {OS reserved, arenas, block reuse, bump-alloc, peak live} on both.

  #### 2. Injected drift — FAILS, naming the missing symbol

  Two edits to `compiler/tychoc0.ty`, hitting two of the three keys: delete the
  `TYCHO_ARENA_STATS` constructor (a byte-for-byte re-creation of the 2b24ca6
  bug) and delete the empty-pop guard from BOTH of its emission sites (one
  surviving site would still supply the text — it is a set comparison).

  ```
  $ git diff --stat compiler/tychoc0.ty
   compiler/tychoc0.ty | 4 +---
   1 file changed, 1 insertion(+), 3 deletions(-)

  $ make rtparity
  rtparity: FAIL - env knob "TYCHO_ARENA_STATS" is emitted by tychoc but MISSING from tychoc0.
  rtparity: FAIL - diagnostic "tycho: pop from an empty array\n" is emitted by tychoc but MISSING from tychoc0.
  rtparity: arena-stats rows  5 shared, 0 allowlisted difference(s) (ok)

  rtparity: FAIL - 2 runtime difference(s). runtime/tycho_rt.c and the runtime
            compiler/tychoc0.ty emits have drifted; port the change to the other
            side, or -- if the difference is intended -- add it to the allowlist in
            tests/rtparity/run.py WITH a reason.
  make: *** [Makefile:221: rtparity] Error 1
  exit=2
  ```

  The untouched third key still reports `ok`, so the failure is per-surface,
  not a blanket red.

  #### 3. Green again after restoring

  ```
  $ git checkout -- compiler/tychoc0.ty
  $ sha256sum compiler/tychoc0.ty
  1ec51731d6f5e0bc98970ab54b5351bcf6c70854cba0eed079a52cd3aea17b5e  compiler/tychoc0.ty   # == pre-injection
  $ git status --porcelain
   M Makefile
   M scripts/ci.sh
  ?? tests/rtparity/

  $ make rtparity
  rtparity: env knobs         3 shared, 0 allowlisted difference(s) (ok)
  rtparity: diagnostics      18 shared, 9 allowlisted difference(s) (ok)
  rtparity: arena-stats rows  5 shared, 0 allowlisted difference(s) (ok)
  rtparity: the two runtimes agree on env knobs, diagnostics and arena stats
  exit=0
  ```

  #### 4. `make test` — nothing broke

  ```
  ok    warn_chan_no_sender
  ok    warn_fallthrough_return
  ok    warn_loop_no_progress
  ok    warn_result_discarded
  -----------------------------------------
  passed: 399   failed: 0
  all green
  MAKE_TEST_EXIT=0
  ```

  #### Allowlisted intentional differences (9, all one direction)

  Every entry is a real hardening gap — tychoc traps, tychoc0 does not — and is
  listed individually, never as a pattern, so a NEW divergence cannot hide under
  a category. Each was re-verified at source:

  | trap text (tychoc only) | tychoc's check | tychoc0's unchecked path |
  |---|---|---|
  | `reserve capacity %ld out of range` | `tycho_cap_check`, runtime/tycho_rt.c:190-192 | `Arr_*_reserve` allocates the request |
  | `string length %ld out of range` | `tycho_str_alloc`, runtime/tycho_rt.c:858-860 | `hs` allocates unchecked |
  | `out of memory` | `tycho_oom`, runtime/tycho_rt.c:93 | `blk_get` mallocs unchecked, compiler/tychoc0.ty:9669 |
  | `slice [%ld:%ld] out of bounds (len %ld)` | inline in codegen, src/tychoc.c:8475,8494 | `Arr_*_slice` forwards lo/hi to `_from`, compiler/tychoc0.ty:9892 |
  | `split with an empty separator` | runtime/tycho_rt.c:1582 | `hi_split` returns the whole string (a real behaviour difference, not a missing trap) |
  | `[string:int] map exceeds 2^31 entries` | runtime/tycho_rt.c:1794 | per-type maps, no index guard |
  | `[string:float] map exceeds 2^31 entries` | runtime/tycho_rt.c:1937 | " |
  | `[int:int] map exceeds 2^31 entries` | runtime/tycho_rt.c:2124 | " |
  | `[int:float] map exceeds 2^31 entries` | runtime/tycho_rt.c:2263 | " |

  The other five allowlists (env both directions, rows both directions, traps
  tychoc0-only) are deliberately EMPTY: those surfaces match today and must
  stay matched.

  #### Two guards against the gate decaying into one that cannot fail

  - A floor count on the reference side (`FLOOR`, 3 env / 25 diagnostics /
    5 rows). If extraction silently stops working, both sets go empty and
    empty==empty would pass; below the floor the lane fails instead. The floor
    is on tychoc ONLY — holding tychoc0 to one would make every genuine
    one-symbol gap also print "extraction is broken", the wrong diagnosis.
  - Stale allowlist entries fail the lane. Close a gap in tychoc0 and the lane
    demands its entry be deleted, so the allowlist can only shrink by hand.

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
