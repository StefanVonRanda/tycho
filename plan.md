# Two `core:result` leftovers

> Rotated 2026-08-11. The previous `plan.md` — 49 phases closing the open
> `docs/internals/FRICTION.md` entries — is finished and deleted per CLAUDE.md
> ("a completed plan is deleted, not archived"). `git show 2f0c770:plan.md`
> recovers it. Everything in it was ticked except the entry below, which was
> re-probed during the rotation and carried forward because both halves still
> reproduce.

## Goal

Close the two `core:result` leftovers that the `Result(void, E)` work
(`2f0c770`) found and deliberately did not absorb. Done when `is_some` asks the
same way `is_ok` does, and when the decision on naming the instantiating call
site is either implemented or written down as a refusal with its cost.

## Pre-flight

- **Worst case:** the Phase 2 diagnostics change alters the *file and line* of
  an existing error message, silently re-recording a `tests/reject/` golden so
  it asserts the new wording rather than the rule. That is a gate that stops
  being able to fail.
- **Reversibility:** each phase commits alone; `git revert <sha>` undoes one.
  No data is at risk — both phases touch source and goldens only.
- **Verified 2026-08-11, by probe, during the rotation:**
  - `is_some` still binds an unread `v` — `corelib/result/result.ty:151-154`,
    the same four-line `match` shape `some_or` uses at `:146-149`.
  - `Option(void)` is refused outright, so no instantiation can reach the wall
    that motivated the `Result` half: `./tychoc tests/reject/option_void.ty`
    exits 1 with `'void' is a type only as a Result's ok payload`.
  - `map_err` at a void ok payload reports against corelib's own source. An
    eleven-line caller calling `result.map_err(f(), 7)` on a
    `Result(void, string)` gets, in full:

        corelib/result/result.ty:125: error: Ok carries no value here -- write a bare `Ok:` arm
           125 |         Ok(v): return Ok(v)

    The caller's own line is nowhere in the message.
- **Verified: `0faccaf` did NOT close the second item.** It fixed a different
  mis-attribution — the message used to name the *caller's* file with corelib's
  line number (`./main.ty:71` for an eleven-line file, `ROADMAP.md`'s probe
  table). The path is now right; the missing piece is the call site, which
  `0faccaf` never claimed to add. Both facts are from the probe above, not from
  the commit message.
- **Assuming:** nothing outside `corelib/result/` reads `is_some`'s generated
  code shape. Phase 1 must grep the call sites rather than trust this line.

## Phases

- [x] **Phase 1 — `is_some` asks with `is`, like `is_ok` does**
  - Scope: `corelib/result/result.ty` only. Rewrite `is_some` to the one-line
    `return o is Some`. **Leave `some_or` alone** — it returns the payload, so
    its `match` is load-bearing, not ceremony. Grep every `is_some` call site
    first; this is a body change with no signature change, but the brief that
    says so is not evidence.
  - Not a bug fix. It is the consistency half of `2f0c770`, which left it
    alone for the right reason: a golden-affecting edit with no failing case
    behind it is how scope creeps. It is scheduled now only because the
    rotation re-confirmed the inconsistency is real and one line wide.
  - Done when: `is_some` is one line and the Option half asks the same way the
    Result half does.
  - Verify: `make corelib` (~49s), all green. **Not `make test`** — it globs
    `examples/*.ty tests/*.ty` at the top level and never descends into
    `corelib/`, so it cannot redden for this (`tests/run.sh:179`, `:208`).
  - **Done 2026-08-11.** `corelib/result/result.ty@is_some` is now
    `return o is Some`, one comment line above it pointing at `is_ok`'s reason.
    `is_none` does not exist in the package, and `some_or` was left alone as the
    brief required — it returns the payload, so its `match` is load-bearing.
  - Call sites, grepped rather than assumed: exactly one,
    `corelib/test/result/main.ty:208`, plus the golden line
    `corelib/test/result.out:16` (`is_some   = 1 0`). Neither moved.
  - Evidence: `make corelib` → `corelib: all green (46 ok, tychoc matches
    goldens)`, no skip. `make corelib-examples` → `corelib examples: all green
    (37 ok, tychoc matches goldens)`. `make check-links` → link check ok
    (119 markdown files) and citation check ok.
  - Negative control: with the body flipped to `return o is None`, `make corelib`
    printed `FAIL result (output != golden)` / `corelib: FAIL`, so the golden can
    genuinely redden for this function. Restored, green again.

- [x] **Phase 2 — size naming the instantiating call site, then decide**
  - Scope: read `src/tychoc.c`'s generic-instance resolve path — the one
    `0faccaf` touched — and answer one question with source, not estimate: can
    a generic instantiation failure carry the caller's file and line to the
    diagnostic, and what does threading it cost?
  - This is a compiler diagnostics change, much wider than `core:result`.
    `map_err` itself cannot be made to work at a void ok payload — its whole
    job is handing the ok payload back — so the fix is never "make it work".
  - Done when: either the call site is named and a `tests/reject/pkg/` fixture
    asserts it, **or** the refusal is written into this file with the measured
    cost that justifies it. A sizing that ends in "too expensive" is a finished
    phase, not a failed one.
  - Verify: if implemented, `make test` — which was **638 passed 0 failed** at
    `2f0c770`, so expect 639 with the one new reject fixture. If the phase ends
    in a refusal, it touches only Markdown: `python3 scripts/check_citations.py`
    and `sh scripts/check_links.sh`, nothing else.
  - **Sized, then IMPLEMENTED, 2026-08-11.** The sizing question — can the
    instantiation site reach the diagnostic — answered yes from source, and
    narrowly. `instantiate_generic` already receives the call `Expr *e` and
    already reports at `e->line` for three other refusals
    (`src/tychoc.c:8425`, `:8432`, `:8454`), and while it runs, `g_srcname` /
    `g_src` still hold the CALLER's file. So the call site was already in hand at
    the one place the `GInst` is built; nothing needed threading through.
  - Cost, measured rather than estimated: **+7 net lines**
    (`git diff --numstat src/tychoc.c` → `45 38`), and four of the five edit
    sites are line-neutral in-place rewrites —
    `src/tychoc.c@GInst` (three fields), the `GInst gi;` construction,
    and gen_program's instance loop set/clear at `src/tychoc.c:12507` and
    `:12523`. Only the `die_at` region grew, by factoring the snippet printer out
    as `src/tychoc.c@src_snippet` so the note can reuse it.
  - Extends `0faccaf` rather than undoing it: that commit's `diag_use_proc(p)`
    call is untouched and still decides the ERROR line's file. The note is a
    second location printed after it.
  - Result — the rotation's own probe, re-run:

        corelib/result/result.ty:125: error: Ok carries no value here -- write a bare `Ok:` arm
           125 |         Ok(v): return Ok(v)
        ./main.ty:9: note: required from here -- this call instantiated the generic
             9 |     r := result.map_err(work(), 7)

  - One level, not a chain. A nested instantiation records the outer template's
    file and line, which is the immediate call — Rust prints the whole stack, this
    prints the hop the user can act on. Not a limitation worth paying for yet.
  - Fixture: `tests/reject/pkg/generic_inst_callsite/`, a sibling of `0faccaf`'s
    `generic_inst_srcfile/`. It pins BOTH locations, which needed the reject
    lanes to assert EVERY `# expect:` line rather than only the first
    (`tests/run.sh`, both the flat and pkg lanes) — a strict generalisation, so a
    single-expect fixture scores exactly as before.
  - Negative control: with `src/tychoc.c` reverted to HEAD and rebuilt, the
    fixture's second expect line NOMATCHed and the first still matched — i.e. the
    old compiler prints the error location but no note, so the fixture genuinely
    fails without the change. It cannot match itself: the old output contains no
    `note:` at all, and the only line the new output quotes from `main.ty` is the
    call at `:6`, not the `# expect:` line at `:2`. Restored, both MATCH.
  - Gates: `make test` → **639 passed, 0 failed** (baseline 638 at `2f0c770`,
    +1 for the new fixture, and no existing golden moved). `make corelib` → all
    green (46 ok). `make vm-check` → green. `sh scripts/entrypoints.sh` → ok
    (75 entry points). `make check-links` → both gates ok.
  - Citation drift, as the rotation warned: the +7 lines staled 108 citations.
    `scripts/reanchor_citations.py` dry-run reported 0 needing a human, then
    `--apply` rewrote 118 files and the gate went green. The script's documented
    blind spot — bare `:N` continuations inside `src/tychoc.c` itself — was ten
    refs, each verified to shift by exactly +7 (the text at the old line in
    `HEAD` and the new line in the working copy is identical) and fixed by hand.

- [x] **Phase 3 — harden `scripts/check_citations.py`: commit hashes are
      citations too** *(requested directly during this plan, not discovered by
      Phase 1 or 2)*

  Two blind spots were named: unvalidated commit hashes in prose, and bare
  `path:N-M` ranges that stay in bounds while pointing at unrelated code.
  Sized both before building; built one, declined the other with numbers.

  - **Sizing (a) — commit hashes.** 63 distinct backticked hex runs of 7–40
    characters across the tracked tree, 113 occurrences. 14 of them are decimal
    measurements (`15777800`, `4294967295`, `9223372036854775807`) and fall out
    on the "needs both a digit and an `a-f` letter" rule. The remaining 49 are
    **all exactly 7 characters** and, at the time of writing, **49/49 resolve**
    via `git cat-file`. Unbackticked hex is a minefield by contrast: 52 hashy
    bare tokens, of which the CRC/FNV/sha vectors in `corelib/test/hash.out`,
    `corelib/test/md5.out` and `tools/tycho-ar/main.ty` pass every shape test a
    hash would. The one bare form with unambiguous intent is the word `commit`
    in front — 4 sites tree-wide, 0 false positives.
  - **Sizing (b) — single-line refs vs ranges.** 290 bare single-line refs and
    552 bare ranges. Range widths: min 2, median 8, p90 39, max 481. Requiring
    an anchor on every single-line ref tree-wide is therefore a **flag day, not
    a migration**: 290 hand edits, many in `docs/spec/` where the cited line has
    no token that occurs once.
  - **Sizing (c) — ranges.** "Points at unrelated code" is not mechanically
    decidable and is not pretended otherwise. What is detectable: width. 107
    ranges are 25+ lines, 18 are 100+, and the widest (`src/tychoc.c:3236-3716`,
    cited from three spec chapters) is an honest citation of a whole declaration
    parser. Width is a report, never a verdict.
  - **Built:** commit-hash validation as a hard failure. Backticked at exactly
    7, or any width after the word `commit`/`commits`; one batched
    `git cat-file --batch-check` for the whole tree, so the cost is one
    subprocess, not one per hash. `.out`/`.err` goldens and `compiler/tychoc0.ty`
    are excluded with the existing skip sets. Every occurrence is named, not
    just the first. Skips loudly and passes on a shallow clone.
  - **Built:** `--report`, advisory, attached to no verdict — it lists the 290
    un-anchored single-line refs and the 18 wide ranges. Not wired into
    `make check-links`.
  - **Declined, with reasons:** tree-wide mandatory anchoring (290-ref flag
    day); any range-drift heuristic beyond width (undecidable); scanning
    unmarked hex (the digest vectors above make it uncheckable).
  - **The width-7 pin is a measured decision, not a guess.** The first version
    accepted 7–12 backticked and the cry-wolf test immediately reddened on a
    backticked CRC32. Every fixed-width digest in this tree is even (CRC32 8,
    FNV 8/16, md5 32, sha256 64), so pinning the backticked form to git's
    default 7 excludes all of them by construction. The cost is that an 8–12
    character hash in bare backticks is unchecked; `commit <hash>` covers it.
  - **REAL DEFECT FOUND, and fixed.** `Makefile` and `bench/guard.sh` both
    attributed the 5x tree-alloc perf regression to a commit that resolves to
    nothing anywhere in this repository (6ff7aa1, written unmarked here so this
    file does not redden its own gate). Corrected to `4a5c64c` — "MM-9
    (hierc0): parity — arena-place string elements + recycle, bucketed
    freelist", on `main`, and the fix commit `665af34` is literally titled
    "recover MM-9's 5x tree-alloc regression". Identification by the named
    ticket and the named symptom, not by hash similarity.
  - **Proof it can fail.** Clean tree: `make check-links` exit 0. Injecting the
    brief's exact defect — 9bcc93b for `9cbbd3b`, a transposition, written once
    backticked in `ROADMAP.md` and once after the word "commit" in
    `CHANGELOG.md` (both spellings unmarked in this bullet, or this file would
    redden its own gate) — produced `STALE  ROADMAP.md:400 ... is not a commit
    in this repository (git cat-file says: missing)` and the matching
    `CHANGELOG.md:662` line, then `FAILED (2 stale citation(s) above)`, exit 1.
    Each site is named separately, so a hash repeated in two files reports
    twice. Reverted: green. **The gate then caught this very bullet** on its
    first draft, which is the check proving itself a third time.
  - **Proof it does not cry wolf.** A paragraph carrying a sha256, an md5, a
    CRC32, an FNV, a UUID, a `#rrggbb`, a `0xdeadbeef`, a `127.0.0.1:8080`, and
    two decimal measurements passed with **zero** failures while still picking up
    the one real hash beside them (54 → 55 checked). Breaking only that hash in
    the same paragraph reddened. The shallow-clone path was proved by cloning
    this repo `--depth 1` with the stale 6ff7aa1 still present: `commit-hash
    check: SKIPPED`, exit 0.
  - **Runtime.** `make check-links` 0.861 / 0.870 / 0.842 s before, 1.019 /
    1.017 / 1.038 s after — +0.16 s for reading every tracked file once.
    `check_citations.py` alone is 0.532 s. `CONTRIBUTING.md`'s "under a second"
    for the whole target was corrected to `~1s`; `CLAUDE.md`'s gate row now
    carries both figures.
  - **Docs.** `CLAUDE.md`'s Citations section (authoritative) and
    `CONTRIBUTING.md`'s tracked copy were updated together, both with the
    "never backtick a bare digest" rule and the 8–12 gap.
  - **Gates:** `make check-links` → both ok, 55 commit hashes resolve.
    `sh scripts/entrypoints.sh` → ok (75 entry points), run because `Makefile`
    was touched. `make test`, `make corelib` and `make ci` deliberately NOT run:
    none can redden for a doc gate, a comment and a Markdown edit.
  - **Comment budget:** +106 lines in `scripts/check_citations.py`, 11 of them
    comment lines.

- [x] **Phase 4 — decide what to do about the 290 un-anchored single-line
      refs** *(discovered by Phase 3, out of its scope, not absorbed)*

  `python3 scripts/check_citations.py --report` lists them. Each proves a line
  exists and nothing more, which is exactly how `ROADMAP.md`'s `` `:133` ``
  silently re-pointed from `map_err_with` to `map_err`. Anchoring all 290 is a
  flag day and was declined. The affordable question this phase should answer:
  is there a *subset* — refs into `src/tychoc.c` and `runtime/tycho_rt.c`, the
  two files that move most — worth anchoring by hand, and can
  `scripts/reanchor_citations.py` propose an anchor mechanically for the ones
  whose cited line already contains a token occurring once in the range? Size
  that subset before touching anything.

  **Done 2026-08-11. The subset is small, and the reason is the finding.**
  `make check-links` green after every file; the 290 refs the phase started from
  became **252**, 38 converted. The gate now reports 268, not 252: this evidence
  block and Phase 6 below cite the rot they describe, adding 16 bare refs of
  their own. They stay bare on purpose — a ref whose job is to name a line that
  says the *wrong* thing has no honest anchor. A mechanical proposer was written and **rejected**: requiring only
  "a token occurring once in the range" makes every line anchorable, and for a
  one-line ref the range *is* the line, so the gate constraint is free and
  carries no signal. Anchoring on a token the citing sentence does not name
  produces a false anchor — reddens on an unrelated edit, passes through a real
  one. The rule that held is **the anchor must be the subject of the citing
  sentence**, which no script can decide.

  | category | n | note |
  |---|---|---|
  | `path@SYMBOL` (definition) | 15 | the best form; survives insertions entirely |
  | `path:N@token` | 23 | non-definition line with a distinctive subject token |
  | left bare, deliberately | 252 | prose, comment continuations, refs into frozen `compiler/tychoc0.ty` (which cannot drift), and rotted refs that have no honest anchor |

  **23 of the 38 conversions were also rot repairs** — the citing sentence and
  the cited line disagreed, each confirmed by reading the source: `stats_dump`
  (`runtime/tycho_rt.c@stats_dump`, cited as `:416`), `arena_recycle` (`:548` →
  the body of `block_get`), `TYCHO_BLOCK` (`:466` → `:521`), `g_out`
  (`corelib/crypto/crypto_shim.c:43@g_out`, cited as `:35` and twice as `:36`),
  pop-on-empty (line 12121 → `src/tychoc.c:12981@pop`), tuple arity (lines 2018
  and 2014 → `src/tychoc.c:5592@least` and `src/tychoc.c:5593@most`), `or_return`
  in a `parallel for` (line 6639 → `src/tychoc.c:7146@or_return`), the
  inout-aliasing rule (line 6150 → `src/tychoc.c:6662@alias`), the 16-parameter
  cap (line 8075 → `src/tychoc.c:8688@parameters`), `split_once` (line 193, an
  unrelated `enum FloatErr`, → `corelib/strings/strings.ty@split_once`),
  `read_request_capped` (line 242 → `corelib/httpd/httpd.ty@read_request_capped`),
  `netx_peer_addr` (line 204 → `corelib/net/net_shim.c@netx_peer_addr`), the
  shutdown line (line 646 → `server/main.ty:1088@stopped`), CI step `[3c/13]`
  (line 111 → `scripts/ci.sh:233@server`), the `match` grammar production (line
  130, which is `CompoundAssign`, → `docs/spec/appendix-a-grammar.md:136@match`),
  and `TYCHO_BLOCK_DEFAULT` (line 51, an `#include`, →
  `runtime/tycho_rt.c:80@TYCHO_BLOCK_DEFAULT`).

  **Nine rotted refs were flagged, not fixed**, because repairing them means
  rewriting a prose claim rather than a coordinate — see Phase 6.

- [ ] **Phase 5 — `bench/prongB/RESULTS.md` writes output checksums that read
      as commit hashes** *(discovered by Phase 3, deliberately not guessed at)*

  `411c91a9` and `67f39fca` appear as bare parenthesised 8-hex tokens meaning
  "byte-identical output"; neither resolves to any object. A reader cannot tell
  them from a short commit hash, and the new gate cannot check them either way.
  Label them at the source (`crc=…` or `out-sha=…`) so the ambiguity is gone.
  Not done here: it edits a benchmark record whose provenance belongs to
  whoever measured it.

- [ ] **Phase 6 — rotted citations whose repair is a prose rewrite, not a
      re-point** *(discovered by Phase 4, out of its scope, not absorbed)*

  Each was confirmed by reading the cited source. None is a coordinate that can
  simply be moved; the surrounding sentence asserts something the tree no longer
  does, so fixing it is a content decision for whoever owns the claim.

  - `ROADMAP.md:231` says `corelib/result/result.ty:71` "is `Ok(v): return
    true`". Line 71 is a comment and `is_ok` has no `match` at all —
    `corelib/result/result.ty:75-76` is `return r is Ok`. The paragraph records a
    `Result(void, E)` gap that the `is` operator has since closed.
  - `ROADMAP.md:233` cites `` `:76` `` for `is_err`; `is_err` is at
    `corelib/result/result.ty:78`, and 76 is inside `is_ok`.
  - `docs/bootstrap.md:106-107` says four runners "**still** feed their entry
    point to a freshly built `tychoc0`". All four retired that leg on 2026-07-29
    and say so themselves (`examples/weblog/run.sh:49`,
    `examples/webserver/run.sh:59`, `examples/sqlite/run.sh:67`). The same line
    then credits `scripts/frontparity.sh`, which is retired.
    `docs/spec/appendix-e-conformance.md:362-363` repeats the citation cluster.
  - `server/README.md:276` cites `docs/internals/FRICTION.md:601` for a
    `write-failed` log line. The string `write-failed` does not occur anywhere in
    `docs/internals/FRICTION.md`.
  - `docs/internals/FRICTION.md:420` cites `src/tychoc.c:9415` as `ncpu()`'s
    lowering; that line is `collect_append_ops`.
  - `docs/internals/FRICTION.md:1020` quotes a comment it places at
    `corelib/test/io/main.ty:43`; that line is `fn sl(...)`.

  **And the systemic case, which is the one worth costing.** Whole documents
  have drifted as a block, every ref into `src/tychoc.c` or
  `runtime/tycho_rt.c`: `docs/spec/15-program.md` (15 refs — `main` at `:12565`
  is a map typedef; the `--cc` flag at `:12920` is an `fprintf`),
  `docs/internals/design-scalar-match.md` (the `S_MATCH` pass at `:7572` is a
  bare `}`; codegen at `:10309` is a blank line),
  `docs/rfc/value-lifetime-regions.md` (the `&_scope` default at `:7423` is
  `pr->name = sfmt("__par%d", id)`), plus
  `docs/internals/value-semantics-limits.md` and
  `docs/rfc/limited-references-spike.md`. This is exactly what
  `docs/internals/FRICTION.md`'s own last entry predicted: bounds-checking a
  bare ref into a 12k-line file can never fail. Re-pointing them is a day's work
  that lasts until the next compiler phase; anchoring them as they are
  re-pointed is what makes the repair hold.

## Out of scope

`plan_windows.md` is a separate track and is not touched by this plan.

`make ci` and standalone `make test` are not run as ritual — each phase runs
only the lane that can redden for what it touched. Pushing is the user's call,
not a phase's; phases commit and stop.
