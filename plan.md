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

- [x] **Phase 2 — Value-lifetime region design study (RFC, no implementation)**
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

  ### Result

  `docs/rfc/value-lifetime-regions.md` (one new file; nothing else in the tree
  was touched — no compiler, runtime, or codegen change).

  #### The three designs, and the verdict

  | | surface | sideways pointer? | `spawn`/channels | fate |
  |---|---|---|---|---|
  | **A** region variables (`Node@r`) | `r := region()`, `x := T(..) in r` | **yes — that IS the feature** | must be BANNED from spawn args + channel payloads (a `Sendable` split) | reject: a lifetime-annotation system, contradicts `docs/thesis.md:10` |
  | **B** value-homed arena | `owned [Conn]` / `Region(T)` — the arena is a hidden field, never a type variable | none: reads copy out, writes copy in | **survives intact** — `copy_into(.., "(&_tk->root)", ..)` generalises with no new rule | sound, buildable, narrow payoff |
  | **C** `drop(x)` | one statement; use-after-drop is a compile error | none | n/a (intra-scope, value is consumed) | reject: it is the only one that breaks §10.4's asymmetry |

  Recommendation: **don't build.** B is the only sound one and should still wait
  for a stated condition — a `bench/` workload whose peak is dead-but-unreclaimable
  storage that the three shipped mechanisms cannot recover, with per-value payloads
  at or above one 64 KiB block. No such benchmark exists (checked the whole
  `bench/README.md:19-45` table plus `gcscan`/`latency`/`invindex`: all three are
  *live* retention).

  #### Verification

  ```
  $ make check-links
  link check: ok (116 markdown files, no dead relative links)
  exit=0
  ```

  Every `path:line` citation was checked, not sampled: a script extracted all
  **110** unique citations from the RFC, resolved each path, asserted each line
  range is within the file, and dumped the cited lines; the dump (1090 lines) was
  read in full and each region confirmed against the claim it backs. Two defects
  were found this way and fixed before commit:

  - `bench/README.md:26` was cited for the invindex hold-cost figure — line 26 is
    the *iter-transform* row. Corrected to `:27`.
  - `docs/spec/00-conventions.md:106-113` cut off mid-sentence before the
    load-bearing "never silently"; extended to `:106-114`.

  Post-fix re-run: 110 citations, 0 missing files, 0 out-of-range ranges.

  #### Three findings that qualify the phase's premise

  1. **"Copy-up" overstates the tax.** Up-escape is destination-passing *first*:
     the value is produced directly in the destination's storage
     (`docs/spec/07-memory-model.md:109-112`,
     `docs/guides/memory-model.md:43-45`), so a copy is paid only for a value that
     already exists elsewhere. The standing tax on long-lived structures is
     reclamation timing, not repeated copying. §2.3 of the RFC states this.
  2. **The "server holding N connections" shape does not exist in this repo.** The
     accept loop holds no per-connection state and a connection is an `int` fd
     (`examples/webserver/main.ty:205-215`, `corelib/httpd/httpd.ty:5-14`). The gap
     is real but no call site currently pays for it — §3.4.
  3. **The reference spike's optional half already shipped.** User-defined yielding
     subscripts (`limited-references-spike.md:104-131`) are now documented
     (`docs/reference/subscripts.md:1-8`), normative (§15.6/§18.7,
     `docs/spec/appendix-e-conformance.md:132`,`:146`) and enforced
     (`compiler/tychoc0.ty:2050`), so a region proposal must justify itself on
     reclamation timing alone.

  #### Drift found, deliberately NOT fixed (out of scope: one document, no code)

  `examples/triepool.ty:3` still says the by-value trie "costs ~2.7x C on memory".
  The current, consistently-stated figure is ~1.55× C
  (`docs/internals/value-semantics-limits.md:45`, `docs/guides/memory-model.md:118`,
  `docs/architecture.md:87`, `README.md:139`, `bench/README.md:33`,
  `bench/trie/RESULTS.md:28` — all six re-opened rather than trusted). Recorded in
  RFC §3.1 so it is not lost.
