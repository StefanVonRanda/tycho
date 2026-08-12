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
    (`src/tychoc.c:8466`, `:8473`, `:8495`), and while it runs, `g_srcname` /
    `g_src` still hold the CALLER's file. So the call site was already in hand at
    the one place the `GInst` is built; nothing needed threading through.
  - Cost, measured rather than estimated: **+7 net lines**
    (`git diff --numstat src/tychoc.c` → `45 38`), and four of the five edit
    sites are line-neutral in-place rewrites —
    `src/tychoc.c@GInst` (three fields), the `GInst gi;` construction,
    and gen_program's instance loop set/clear at `src/tychoc.c:12548` and
    `:12564`. Only the `die_at` region grew, by factoring the snippet printer out
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
    ranges are 25+ lines, 18 are 100+, and the widest (`src/tychoc.c:3244-3724`,
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
  pop-on-empty (line 12121 → `src/tychoc.c:13022@pop`), tuple arity (lines 2018
  and 2014 → `src/tychoc.c:5600@least` and `src/tychoc.c:5601@most`), `or_return`
  in a `parallel for` (line 6639 → `src/tychoc.c:7187@or_return`), the
  inout-aliasing rule (line 6150 → `src/tychoc.c:6703@alias`), the 16-parameter
  cap (line 8075 → `src/tychoc.c:8729@parameters`), `split_once` (line 193, an
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

- [x] **Phase 5 — `bench/prongB/RESULTS.md` writes output checksums that read
      as commit hashes** *(discovered by Phase 3, deliberately not guessed at)*

  The two 8-hex tokens (now `md5=411c91a9` and `md5=67f39fca`) appeared as bare
  parenthesised 8-hex tokens meaning "byte-identical output"; neither resolves to
  any object. A reader cannot tell them from a short commit hash, and the new
  gate cannot check them either way. Label them at the source (`crc=…` or
  `out-sha=…`) so the ambiguity is gone. Not done here: it edits a benchmark
  record whose provenance belongs to whoever measured it.

  **Done.** Fixed at the true source, which is not `RESULTS.md`: that file is
  hand-maintained prose, and the tokens are pasted from the runner's stdout.
  `bench/prongB/run.sh` computes them at two sites — `run_one` and the
  json-workload runner — both `md5sum … | cut -c1-8`. Both now emit
  `md5=<8hex>`, so the label is produced where the value is, and the next paste
  carries it automatically. `RESULTS.md`'s two occurrences were updated to match
  what the runner now prints.

  Label shape is the house one, not invented: `CONTRIBUTING.md:88-91` already
  says "do not backtick a bare digest… give it a label (`sha=cbf43926`)". `md5=`
  names the algorithm actually used.

  Scope checked rather than assumed. `RESULTS.md` holds two other checksum-ish
  tokens and BOTH correctly stay: `26214400` and `262547666730` are printed by
  the benchmark programs themselves, not by the runner —
  `bench/prongB/maptree.ty:34` computes `200 * 2 * 65536 = 26214400` — and being
  pure decimal they can never read as a commit hash, since `hashy()` in
  `scripts/check_citations.py@hashy` requires an `a-f` letter.

  **Widening: still NOT possible, and the blocker is no longer this file.**
  Measured, not guessed, by temporarily widening `HASH_TICK` to `{7,12}` and
  running the gate (then reverting — the gate is unchanged by this phase, as the
  brief required). The probe was run three times, and the count fell each time:

  1. **Before this phase's edits: 3 reddened.** Two were `plan.md:288` — this
     phase's own description, which quoted the raw prongB tokens in backticks —
     and the third was the gate's own comment. The tokens in
     `bench/prongB/RESULTS.md` were never among them: they are not backticked,
     so `HASH_TICK` never saw them.
  2. **After relabelling and rewriting this phase's own prose: 1 reddens.**
     `scripts/check_citations.py:95` — **the gate's own comment**, which cites
     crc=cbf43926 as the example digest that forced the width pin. A widened
     gate fails on its own docstring.

  So after this phase one blocker remains, and it is self-referential. Filed as
  Phase 17 rather than absorbed, because it edits the gate.

  **The premise this phase inherited was wrong, and it matters for Phase 17.**
  The claim was that width 7 was pinned to dodge these prongB tokens. It was not:
  they are not backticked at all, so `HASH_TICK` never saw them. The gate's own
  comment names the real forcing case — a backticked CRC32, crc=cbf43926, from
  `corelib`'s worked example. Fixing prongB was still right (a reader genuinely
  could not tell those tokens from a hash), but it was never what pinned the gate.

  Gates: `sh bench/guard.sh` ok (tycho beats C on tree workloads; elision live),
  `make check-links` ok. `sh -n bench/prongB/run.sh` clean, and the emitted form
  was executed rather than assumed: `md5=b1946ac9`. The full prongB sweep was NOT
  run — it needs Go, Rust and Koka toolchains and measures nothing this change
  touches.

- [x] **Phase 17 — the commit-hash gate cannot widen while its own comment
      quotes a bare digest** *(discovered by Phase 5, not absorbed: it edits the
      gate, which Phase 5 was told not to do)*

  With Phase 5 done, widening `HASH_TICK` from `{7}` to `{7,12}` reddens on
  exactly one remaining token, and it is inside the gate itself:
  `scripts/check_citations.py:95` quotes crc=cbf43926 in backticks while
  explaining why bare digests must not be backticked. Relabel it to the form
  `CONTRIBUTING.md:88-91` prescribes, then decide whether to widen — the payoff
  is that 8-to-12 char backticked hashes stop being unchecked forever.
  Verify by widening, running `python3 scripts/check_citations.py`, and
  confirming it is green before keeping the wider form.

  **Done.** The inherited measurement was re-run, not trusted: an untracked copy
  of the gate with `HASH_TICK` widened to `{7,12}` (untracked, so `git ls-files`
  never scans it) failed on **exactly one** token, and it was the gate's own
  comment — `scripts/check_citations.py:95` in the pre-fix file, quoting a CRC32
  digest in backticks. One blocker, self-referential, as filed.

  The comment now names the digest as `crc=...` prose with no hex run, and the
  module docstring's claim was wrong in the same place and is corrected: it said
  no "md5/sha/CRC-width run" reaches 7..12, but CRC32 is 8 and does. Both now
  say a CRC32 digest must be written unbackticked.
  `scripts/check_citations.py@HASH_TICK` is `{7,12}`.

  **Proof it can fail, both directions** (`ROADMAP.md` appended to, then
  restored; `git diff --stat ROADMAP.md` empty after):

  ```
  bad  -> STALE  ROADMAP.md:400  deadbeef1234 is not a commit in this
          repository ... citation check: FAILED
  good -> HEAD at 9 and 12 chars: ok, 62 commit hashes resolve (60 + 2)
  ```

  The bad token is written unbackticked *here* because the widened gate caught
  this very evidence block when it was not — `plan.md` is exempt from the path
  citation rules, not from the hash check. That is a third demonstration.

  The tree's own count is unchanged at 60: no backticked 8..12 hash exists here
  today, so the widening buys future coverage, not present coverage.

  Gates: `python3 scripts/check_citations.py` green, **0.535 s before / 0.542 s
  after** (`time`, warm) — the sub-second cost everyone pays is unmoved.
  `sh scripts/check_links.sh` ok (119 markdown files), 0.488 s.

- [x] **Phase 6 — rotted citations whose repair is a prose rewrite, not a
      re-point** *(discovered by Phase 4, out of its scope, not absorbed)*
      DONE for the seven enumerated bullets. **The systemic block at the end was
      NOT done** — it is a day's work by the phase's own estimate and is refiled
      as Phase 19 rather than ticked along with the rest.

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
  - `docs/internals/FRICTION.md:420` cites `src/tychoc.c:9456` as `ncpu()`'s
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

  **Evidence — each read against its source, then rewritten or deleted.**

  | citation | claimed | true now | done |
  |---|---|---|---|
  | `ROADMAP.md:231` | `is_ok` is `Ok(v): return true`, so `core:result`'s combinators "cannot be instantiated at" `Result(void, E)` | `corelib/result/result.ty@is_ok` is `return r is Ok`; built and ran a `touch(x) -> Result(void, string)` through `result.is_ok`/`is_err` — both compile and print `true`. The gap is **closed** | section retitled "**closed**", the false gap claim deleted and replaced with what the `is` rewrite actually did |
  | `ROADMAP.md:233` | `is_err` at `` `:76` `` | `:76` is inside `is_ok`; `is_err` is at `:78` | replaced with the rename-proof `corelib/result/result.ty@is_err` |
  | same paragraph, tail | `0faccaf` "did *not* add the instantiating call site, so the message still points at a file the caller cannot edit" | `be325b4` added it — re-probed, the refusal now prints a `note: ... this call instantiated the generic` naming the caller's own line | sentence rewritten to record both fixes |
  | `docs/bootstrap.md:106-107` | four runners "**still**" feed their entry point to a freshly built `tychoc0` | all four retired that leg 2026-07-29 and say so in their own headers (`examples/webserver/run.sh:30`, `examples/weblog/run.sh:19`, `examples/fetch/run.sh:10`, `examples/sqlite/run.sh:9`); `scripts/frontparity.sh`, credited in the same line, is retired too | rewritten to past tense, coordinates moved to the retirement headers, and the 13/24 split marked as a historical record nothing runs |
  | `docs/spec/appendix-e-conformance.md:362-363` | the same four runner coordinates | prose was already correctly past-tense; only the coordinates rotted | coordinates repointed to the retirement headers |
  | `server/README.md:276` | cites `docs/internals/FRICTION.md:601` for the `write-failed` log line | the string occurs nowhere in FRICTION.md; it is emitted at `server/main.ty:569` | repointed and anchored to the real source |
  | `docs/internals/FRICTION.md:420` | `ncpu()` registered at line 4682 of `src/tychoc.c`, lowering at line 9456 | line 9456 is `collect_append_ops`; registration is `src/tychoc.c:5112`, lowering `src/tychoc.c:10094` | both repointed and **anchored**, so a future shift reddens instead of drifting |
  | `docs/internals/FRICTION.md:1020` (×2) | quotes a comment at `corelib/test/io/main.ty:43` praising it for distinguishing the language from the frozen compiler | `:43` is `fn sl(...)`; the comment was rewritten and at `:51` now records the **opposite** — that nothing builds tychoc0 and nested patterns are writable | the dead coordinate dropped from both refs and the reversal recorded inline |

  Where a claim was simply obsolete it was deleted rather than guessed at: the
  `Result(void, E)` gap paragraph is gone, not reworded into a smaller gap.
  Gate: `sh scripts/check_links.sh` ok (119 markdown files, no dead relative
  links) and `python3 scripts/check_citations.py` ok. `make test` deliberately
  not run — prose only, and it cannot redden for it.

- [ ] **Phase 19 — the whole-document citation drift, still uncosted**
      *(split out of Phase 6, which repaired only its enumerated bullets)*

  Five documents have drifted as a block, every ref into `src/tychoc.c` or
  `runtime/tycho_rt.c`: `docs/spec/15-program.md` (15 refs),
  `docs/internals/design-scalar-match.md`, `docs/rfc/value-lifetime-regions.md`,
  `docs/internals/value-semantics-limits.md`, `docs/rfc/limited-references-spike.md`.
  Phase 6 verified the shape of the problem and left it: re-pointing is a day's
  work that lasts until the next compiler phase, so the repair is only worth
  doing **as a conversion to anchored `path:N@token` form**, after which
  `scripts/check_citations.py` polices them forever. Bare refs into a 12k-line
  file can never fail a bounds check, which is why this is invisible today.

- [x] **Phase 7 — adversarially exercise the surface that shipped 2026-08-11**

  A hunt, not a build: every feature that landed that day had roughly one
  happy-path fixture and one reject fixture, so the combinations no fixture
  covers were probed directly. ~45 `.ty` programs written in `/tmp`, compiled
  with `./tychoc` and run.

  **Probed, no defect found** (detail and outputs in
  `docs/internals/FRICTION.md`, "Adversarial pass over the surface that shipped
  2026-08-11"): `is` single-eval on a side-effecting call, `is` under
  `and`/`or`/`not` short-circuit, `is` in a generic body substituting to two
  different enums and to `Option(int)`/`Option(string)`, `is` precedence against
  `==`; the uppercase-binding rule at seven binding forms; `[string]` over the
  FFI at empty, empty-element, 2000-element, push-built, struct-field, literal,
  same-array-twice and `bounded[N]string`; `Result(void, E)` through an
  `or_return` chain and through `result.is_ok`/`is_err`/`err_or`/`is`;
  `try_map` at first/last/only/empty and nested in itself; f-string ordering
  with `inout` side effects, nested f-strings, `or_return` inside one;
  `io.set_mtime` on a directory, a symlink, a missing path, `""`, a mode-444
  file, and negative and year-2100 stamps.

  **One defect found and fixed here.** `bdf0a00` refused `Ok`/`Err`/`Some`/
  `None` as a `fn` or `const` name because the constructor wins at every use
  site and the declaration would be unreachable. The same argument holds for a
  `struct`, an `enum`, a `type`, a `handle` and an enum VARIANT, and none of the
  five was guarded — each declared cleanly and failed only at its first use,
  with a diagnostic about the builtin and no mention of the declaration:

  ```
  struct Ok:
      v: int
  fn main():
      o := Ok(1)
      println(str(o.v))     # error: '.v' on a non-struct value
  ```

  A variant was the worst of the five: `match e: Ok:` matched it while `e := Ok`
  reported "'Ok' ... as a value it is a constructor: Ok(x)". Half of it worked.

  Fix: `is_builtin_ctor(nameT->text)` added to the four type-declaration guards
  and the variant-registration guard — `src/tychoc.c@parse_handle`,
  `src/tychoc.c@parse_struct`, `src/tychoc.c@parse_enum`,
  `src/tychoc.c@parse_typedecl`, and the variant loop inside the last. Six
  lines changed, six removed: **line-neutral, so no citation re-anchoring**.

  Fixtures: `tests/reject/struct_named_ok.ty`,
  `tests/reject/enum_named_none.ty`, `tests/reject/variant_named_ok.ty`,
  `tests/reject/newtype_named_err.ty`, `tests/reject/handle_named_some.ty`.

  > Negative control, run before the fixtures were trusted: with
  > `git stash push src/tychoc.c` and a rebuild, all five compiled clean
  > (`built /tmp/hunt/nc` × 5). With the fix they are refused with
  > `'Ok' is already defined` and its three siblings. Nothing in the tree
  > declared any of the four names — grepped over every `*.ty` before editing.

  Spec: `docs/spec/12-aggregates.md` already said the variant namespace is
  shared by `fn`, `const`, `struct`, `enum`, `type` and `handle`, then spelled
  the builtin-constructor rule for `fn` and `const` only. Widened to name all
  six plus a variant.

  **The one that looked like a defect and is not.** `str(bump(&c)) + "|" +
  str(c)` prints `1|0` and `pair(bump(&e), e)` prints `1,0`, while the f-string
  spelling prints `1|1`. `docs/spec/09-expressions.md:168` makes argument and
  operand order *unspecified* deliberately and
  `docs/spec/appendix-f-impl-defined.md:14` names the f-string holes as the
  exception, so `680d30d` is exactly as narrow as it claims.

  Gates: `make test` (639 baseline, +5 reject fixtures), `check-links`,
  `check_citations.py`, `scripts/entrypoints.sh`. Not `make corelib` — nothing
  under `corelib/` changed. Not `make ci`.

- [ ] **Phase 8 — `core:iter` has no `try_each`, and the refusal is a message
      about a corelib local** *(discovered by Phase 7, filed not absorbed)*

  `iter.try_map` cannot take a callback returning `Result(void, E)` — `$U`
  becomes `void` and the accumulator has no element type. Minimal repro:

  ```
  fn check(x: int) -> Result(void, string):
      if x < 0:
          return Err("neg")
      return Ok()
  fn main():
      r := iter.try_map([1, 2], check)
  ```
  ```
  corelib/iter/iter.ty:30: error: cannot infer the type of 'out' from this use
  ./main.ty:8: note: required from here -- this call instantiated the generic
  ```

  The `note:` is `be325b4` working. The message is still about `out`, a local
  the caller has never seen, for a problem that is "there is no `try_each`".
  Not absorbed into Phase 7: adding a function is a decision about the
  package's shape, and the alternative (refuse `void` in `try_map` with a
  message naming `try_each`) is only worth writing once `try_each` exists.

- [x] **Phase 9 — `docs/spec/14-ffi.md` does not say a `string` is truncated at
      an embedded NUL** *(discovered by Phase 7)*

  The spec calls each element "an ordinary NUL-terminated C string" and stops
  there. A Tycho string carries a length and may hold a NUL, so `len(s)` and the
  callee's `strlen` disagree with no diagnostic — probed at `["a" + chr(0) +
  "c"]`, where Tycho says 3 and C says 1. The behaviour is right (the borrow
  copies nothing, which is the point) and `bytes` is the tool for a NUL-bearing
  payload; what is missing is the sentence saying so. Doc-only, so the two doc
  gates and nothing else.

  **The brief's scoping was wrong, and the probe is what caught it.** One program
  under `--shim`, a C file reporting `strlen`:

  ```
  A [string] C strlen sum = 4      A tycho len sum = 6
  A elem[0] byte 2 via C  = 99     ('c' is there; only the length is lost)
  B scalar C strlen       = 1      B scalar tycho len = 3
  C to_str C strlen       = 1      C to_str len tycho = 3
  D bytes len C sees      = 3      D bytes byte 1 = 0     (bytes: INTACT)
  ```

  Line B is the finding: a **scalar `string` parameter has the identical
  hazard**, with no array involved. So does the **return** direction — a C
  function returning a static `{'h',0,'i',0}` gives a Tycho string of `len` 1,
  because the arena copy is `strlen`-bounded
  (`runtime/tycho_rt.c@tycho_str_from_c`, whose own comment already said so).
  This is the whole `char*` boundary in both directions, not a `[string]` bug;
  the spec paragraph is written to that scope. A literal cannot express it —
  `"a\0c"` is rejected, "unsupported escape \0" — so the value always comes from
  `chr(0)`, from `to_str` over a constructed `bytes`, or from C.

  **Verdict: (a) alone, and (c) rejected on the measurement**, against the
  brief's expectation of "(a)+(c) if the scan proves cheap". It does not. The
  borrow measured **0.0 ns/call** — nothing is copied, which is its whole point —
  and a `memchr` guard over a 2000×64-byte array measured **5.5 µs/call**
  (a scratch `memchr` benchmark, `cc -O2`, 200k iterations). That is not a percentage
  overhead on the call, it is the difference between O(1) and O(total bytes) on
  the one property §24.1 sells. It also covers only the outbound half: a `char*`
  out of C carries no length, so there is nothing a return-side check could
  compare against. (b) is impossible as the brief predicted — the content is a
  runtime value, and refusing the *type* would ban the FFI's commonest parameter.
  (d) gives up the same no-copy property and breaks every existing callee for a
  hazard whose escape hatch already exists and works.

  **`bytes` is that escape hatch, and it was checked, not assumed**: it crosses
  as `(ptr, len)` in both directions and preserved the interior NUL exactly
  (C saw length 3, byte 1 = 0, byte 2 = 105).

  **Changed**: `docs/spec/14-ffi.md` §24.1 — the `string` bullet, the `[string]`
  bullet's misleading "a Tycho string is already a NUL-terminated C `char*`", and
  a new "Interior `NUL`s" subsection stating what happens, why, what it costs to
  prevent, and that `bytes` is the supported route. No fixture: documentation
  only, so there is nothing to redden.

  **Gates** — `python3 scripts/check_citations.py` ok; `make check-links` ok
  (119 markdown files, no dead relative links); `sh scripts/spec_check.sh`
  exit 0, 11 runnable examples all pass; `make ffi` green. `make test` and
  `scripts/entrypoints.sh` deliberately NOT run: nothing under `src/`,
  `runtime/` or `tests/` changed, so neither can redden for this.

- [x] **Phase 17 — `core:regex` silently fails to match past an interior NUL in
      the subject** *(discovered by Phase 9)*

  Phase 9 documented the FFI boundary as a stated limit, which is right for the
  boundary itself. But `corelib/regex/regex.ty:40` takes the **subject** as a
  `string` and hands it to `rx_find` as a `char*` (`corelib/regex/regex.ty:24`),
  and a regex subject is ordinary user data — unlike a hostname
  (`corelib/net/net.ty:94`) or a hex digest (`corelib/crypto/crypto.ty:35`),
  which are NUL-free by construction. So a subject with an embedded NUL is
  matched only up to it, and the caller gets "no match" rather than an error:

  ```
  len(subject)   = 10      # "abc" + chr(0) + "SECRET"
  find SECRET    = -1      # the bytes are plainly there
  is_match       = false
  control(no NUL)= 4       # "abcXSECRET" -- same shape, matches
  ```

  The control is in the same run and rules out the pattern being at fault. This
  is the `core:compress`/`core:image` class the owner has twice removed: plausible
  data instead of a failure, and here it is filter-evasion shaped — a NUL-prefixed
  payload passes a regex check that should have caught it. Not absorbed into
  Phase 9, which was scoped to the spec: this is a corelib behaviour decision
  (reject a NUL-bearing subject, or carry the length through to a length-aware
  matcher), and it needs `make corelib` plus a `corelib/test/regex` case, not the
  doc gates. Audit the other `string`-taking externs in the same pass —
  `corelib/strings/strings.ty:207` `strx_parse_double` is the same shape at lower
  severity.

  **DONE 2026-08-12. Fixed properly (option a), not documented away.** The
  subject now crosses as `(pointer, length)` and `REG_STARTEND` bounds `regexec`
  by that length, so a NUL is matched as an ordinary byte
  (`corelib/regex/regex_shim.c@rx_exec`). Every subject-taking wrapper passes
  `len(s)` (`corelib/regex/regex.ty@is_match` and its siblings).

  **Reproduced first, one program, one run, `SECRET` as the pattern:**

  ```
  BEFORE                              AFTER
  len(nul)      = 10                  len(nul)      = 10
  find nul      = -1                  find nul      = 4
  find_end nul  = -1                  find_end nul  = 10
  is_match nul  = false               is_match nul  = true
  matched nul   = ''                  matched nul   = 'SECRET'
  gstart nul    = -1                  gstart nul    = 4
  gend nul      = -1                  gend nul      = 10
  group nul     = ''                  group nul     = 'SECRET'
  groups nul n  = 0                   groups nul n  = 1
  find ctl      = 4                   find ctl      = 4     <- control, no NUL
  is_match ctl  = true                is_match ctl  = true
  pattern len   = 5                   pattern len   = 5
  pat ok        = true                pat ok        = false <- pattern refused
  pat match 'a' = true                pat match 'a' = false
  ```

  **Entry-point audit: all eight were affected, not one.** `is_match`, `find`,
  `find_end`, `matched`, `group`, `groups`, `group_start`, `group_end` all reach
  `regexec` through the subject, and the BEFORE column shows every one of them
  answering "no match" on bytes that are plainly present. The **pattern** has the
  same hazard in the other direction: `"a" + chr(0) + "zzz"` compiled to `a`, a
  *looser* regex than was written, and matched `"a"`. `regcomp` has no
  length-bearing form on any platform, so the pattern is **refused** —
  `ok()` is false, the existing bad-pattern channel
  (`corelib/regex/regex_shim.c@rx_compile`).

  **Portability, measured not assumed.** `REG_STARTEND` is a BSD/glibc extension,
  not base POSIX; musl lacks it. It needs no feature-test macro here —
  `gcc -std=c11 -Wall -Wextra -c corelib/regex/regex_shim.c` compiles clean.
  `make shim-check` **skips this shim** (`skip corelib/regex/regex_shim.c
  (missing dependency: _WIN32: -lpcre2-posix)`), so that standalone compile is
  the substitute, not a supplement. Where the macro is absent the fallback does
  **not** silently truncate: it `memchr`s the subject and dies loudly. Proved by
  compiling the shim with `#undef REG_STARTEND` in front of it:

  ```
  clean find = 4
  tycho: core:regex: subject holds an interior NUL and this platform's
  <regex.h> has no REG_STARTEND; refusing to match a truncated subject
  exit=1
  ```

  **Cost: none measurable.** 200,000 `is_match` calls over a 65-byte subject,
  three runs each, same box, same program: BEFORE 62 / 60 / 59 ms, AFTER 57 / 60
  / 58 ms — ~0.3 µs per call either way. The length is an O(1) header read
  (`runtime/tycho_rt.c@tycho_str_len`), not a scan, so Phase 9's 5.5 µs
  `memchr`-guard objection does not apply to this shape at all.

  **Fixture + negative control.** `corelib/test/regex/main.ty` gained a
  NUL-bearing subject, its NUL-free control and the pattern rejection; the golden
  `corelib/test/regex.out` gained exactly 3 lines and its 7 pre-existing lines are
  byte-identical, which is the "NUL-free behaviour did not move" control at golden
  level. Breaking the fix (`REG_STARTEND` → `0` in `rx_exec`) reddens it:

  ```
  8c8
  < nul len=10 find=-1 end=-1 hit=false mlen=0 g2= ng=0 gs=-1
  ---
  > nul len=10 find=4 end=10 hit=true mlen=6 g2=RET ng=3 gs=4
  ```

  **Exposed callers: none.** `grep -rn 'regex\.' --include='*.ty' .` names three
  files and no more — the package itself, `corelib/test/regex/main.ty` and
  `examples/corelib/regex/main.ty`. No tool, server or example validates untrusted
  input with `core:regex`, so nothing in this tree was evadable in practice; the
  defect was in the surface a user would reach for.

  **The audited sibling is already safe.** `strings.parse_float` scans in Tycho
  before it calls `strx_parse_double` (`corelib/strings/strings.ty@parse_float`),
  so a NUL-bearing input fails closed rather than parsing a prefix — measured,
  `parse_float("1" + chr(0) + "2")` returns `Err` while `"12"` gives `12.0`. No
  follow-up phase is owed for it.

  **Gates.** `make corelib` → `all green (46 ok, tychoc matches goldens)`, no
  skip. `make corelib-examples` → `all green (37 ok)`. `make shim-check` →
  `9 ok, 5 skipped, 0 failed` (regex among the skips, hence the standalone
  compile above). `make test` deliberately NOT run: nothing under `src/`,
  `runtime/` or `tests/` changed, so it cannot redden for this.

- [x] **Phase 10 — a user's enum variant name leaked into a corelib package and
      broke it** *(discovered by Phase 7)*

  A user declaring `enum Mine: Syntax(string)` in `main` and importing
  `core:strings` got an error pointing *inside corelib*, at a line they cannot
  edit. `corelib` declares `Syntax`, `Garbage`, `EmptyInput`, `OutOfBounds`,
  `Empty`, `Overflow` and similar — all names a user would plausibly pick.

  **Repro, before the fix.** `main.ty` alone in its own directory:

  ```
  package main
  import "core:strings"
  enum Mine:
      Syntax(string)
  fn main():
      println(str(strings.parse_int("42")))
  ```
  ```
  corelib/strings/strings.ty:227: error: Syntax carries a payload — write Syntax(...)
     227 |             return Err(Syntax)
  ```

  **Control**, the same program with the variant renamed `Other(string)`: builds,
  prints `42`. The trigger was purely the name collision.

  **Mechanism** — not the `raw` fallback the brief hypothesised. It was the
  lookup *order* in the `E_IDENT` arm of `resolve_expr`, which tried the bare
  name first and the package-prefixed name only as a fallback. `variant_find`
  (`src/tychoc@variant_find`) matches the mangled name only, and `main` mangles
  with an empty prefix — so `main`'s `Syntax` is stored unmangled and won the
  bare lookup for *every* package's unqualified `Syntax`, including corelib's own.
  The fix swaps the two, matching what the `E_CALL` arm already did:

  ```
  int evi, eid = -1;   /* a payload-less enum variant -- OWN package first, as the E_CALL arm below does */
  if (e->pkg && e->pkg[0]) eid = variant_find(sfmt("%s%s", e->pkg, e->sval), &evi);
  if (eid < 0) eid = variant_find(e->sval, &evi);   /* main's own, or a builtin ctor (never mangled) */
  ```

  Line-neutral (3 lines for 3), so no citation re-anchoring was needed;
  `make check-links` confirms.

  **Caller audit** — every site that resolves a variant by name:

  | site | verdict |
  |---|---|
  | `E_IDENT` bare variant, `resolve_expr` | **the bug; fixed** |
  | `E_CALL` package resolution (`src/tychoc@pkg_done`) | clear — already prefixed-first; the fix copies it |
  | `E_CALL` constructor lookup | clear — runs *after* the rewrite above, so `e->sval` is already mangled |
  | `pkg.NAME` field access | clear — looks up the prefixed name only, never bare |
  | `enum_variant_index` (`src/tychoc@enum_variant_index`) | clear — the `raw` fallback is scoped to an already-known enum type, so it cannot cross a package boundary. Hypothesis disproved |
  | `is` RHS, parsed at `pkg_mangle` with the four builtin-ctor exemptions | clear — resolved against the LHS's enum type |
  | match-arm patterns (`arm->variant`, `arm->pcname`, `arm->pch`) | clear — `pkg_mangle`d at parse |
  | declaration-time duplicate checks | clear — mangled |

  **Fixture** — `tests/pkg/variant_shadow/`: `main` declares payload-carrying
  `EmptyInput`/`Garbage`/`OutOfBounds`, two sibling packages `lib` and `lib2`
  each declare all three payload-less and refer to them bare, and main exercises
  qualified `is`, bare qualified variant values, a match on its own enum, and a
  bare own-variant value. It reddens without the fix:

  ```
  tests/pkg/variant_shadow/lib/fault.ty:11: error: EmptyInput carries a payload — write EmptyInput(...)
  ```

  verified by stashing `src/tychoc.c`, rebuilding, and re-running.

  **Why the fixture uses two local packages rather than `core:strings`:** it
  cannot import corelib. See the new Phase 11.

  **Gates.** `make check-links` ok. `sh scripts/entrypoints.sh` ok (75).
  `make goldens-check` ok (it correctly reddened first on the untracked golden;
  fixed by `git add`, not a re-record). `make corelib` all green (46 ok).
  `make vm-check` green. `make test` 645 passed, 0 failed — 644 at the previous
  phase, +1 for the new fixture.

- [x] **Phase 11 — a `tests/pkg/` fixture cannot import a shim-backed corelib
      package** *(discovered by Phase 10)*

  `tests/run.sh` compiles a package fixture with `--emit-c` and then its own
  `cc` line, and has no shim handling at all (`grep -n shim tests/run.sh` is
  empty). `tychoc` linking directly appends `corelib/<pkg>/<pkg>_shim.c`, so the
  same program builds by hand and fails in the lane:

  ```
  undefined reference to `strx_parse_double'
  ```

  It bites on the *import*, not the call — the whole package is emitted, so
  `import "core:strings"` alone is enough even if `parse_float` is never called.
  No existing fixture hits this (`grep -rln 'import "core:' tests/pkg/` named
  only the Phase 10 fixture before it was restructured), which is why it has
  stayed hidden. Phase 10 worked around it with two local packages; the fix is
  to teach `run_one` to append the shims for the corelib packages a fixture
  imports. Not absorbed into Phase 10: it is a change to the test harness, and
  it widens what every future `tests/pkg/` fixture may do.

  **Done.** The filed repro was reproduced first, by hand, before anything was
  changed — `import "core:strings"` plus `--emit-c` and a bare `cc` line gives
  exactly `undefined reference to strx_parse_double`.

  `tests/run.sh@run_one` now asks `--print-shims` and splices the answer onto
  both cc lines, the native one and the sanitizer one, the same way
  `scripts/release.sh:90` does. Cost: one extra tychoc invocation per fixture,
  measured at 0.001 s (it returns before type checking), against a lane that
  takes minutes.

  **`tools/prunner/main.ty@judge_pos` had the identical gap** and was fixed with
  it — not scope creep but the same defect: `make test-fast` reddened on the new
  fixture alone (652/1) while `make test` was 653/0, which is precisely the
  divergence prunner's header promises cannot happen.

  **The fixture Phase 10 could not write** is `tests/pkg/corelib_variant_shadow/`:
  main declares `Syntax` and `Empty` WITH payloads, `core:strings`' `FloatErr`
  declares both without, and `parse_float` is called across all three outcomes.
  This is the real-world shape, against corelib, that `tests/pkg/variant_shadow/`
  had to stand in for with two local packages.

  **Negative controls, both run:**

  ```
  harness: pre-fix tests/run.sh (git show HEAD:tests/run.sh, own dir + symlinks)
           FAIL pkg_corelib_variant_shadow (native cc)
             undefined reference to `strx_parse_double'
  compiler: f645304's src/tychoc.c hunk reverse-applied, rebuilt
           FAIL pkg_corelib_variant_shadow (transpile)
             corelib/strings/strings.ty:212: error: Empty carries a payload
  ```

  The second is the point of the fixture: without the compiler fix the error
  lands on a line inside corelib that the user cannot edit. Both were restored
  and re-run green (`git diff --stat src/tychoc.c` empty afterwards).

  Gates: `make test` **653 passed, 0 failed** (651 before, +1 here and +1 for
  Phase 12 — the two landed under one run, as the brief allowed).
  `make goldens-check` reddened first on the untracked golden and went green on
  `git add`, not on a re-record. `sh scripts/entrypoints.sh` and
  `make check-links` ok.

- [x] **Phase 12 — no lane can redden if corelib warnings leak back into a user
      build** *(discovered while fixing the `core:json` `keys` warning)*

  `2376066` mutes warnings raised while parsing a corelib package the user
  merely imported, and three probes proved it (clean user build; the user's own
  shadow still warns; `corelib/test/json/main.ty` still shows json.ty's). None
  of that is locked by a gate. The over-mute direction IS held —
  `tests/warn/shadow_builtin.ty` is a bare program with no `package` decl, so it
  is unaffected by the mute and still fires. The under-mute direction is not:
  if the mute regresses, every gate stays green.

  Neither existing lane can express it. `tests/run.sh:440` makes the warn lane
  *require* a `warning:` in stderr, so it cannot hold a "no warning" fixture;
  and the lane's fixtures share one directory, so adding `package main` +
  `import "core:json"` to one makes tychoc compile every sibling `.ty` with it
  and collide on `main`. The cheap fix is probably a third grading mode in the
  warn lane (golden stderr, empty allowed) rather than a new gate.

  **Done, and both constraints in the filing were confirmed by reading, not
  assumed.** The warn lane does require a `warning:` (`tests/run.sh:446`), and
  the flat fixtures do share one directory. So the new lane is
  `tests/warn/pkg/<name>/` — the shape `tests/reject/pkg/<name>/` already uses
  for exactly the same reason (a `package` header compiles the whole directory),
  not a sixth convention. It locks the WHOLE compiler stderr against
  `tests/warn/pkg/<name>.err` and has no "a warning must be present" leg, so an
  empty golden is legal here.

  **One fixture covers both halves**, which is what makes it hard to fool:
  `tests/warn/pkg/corelib_mute/main.ty` imports `core:json` (whose `keys`
  shadows a builtin, internally) AND shadows `len` itself. The golden is the
  user's own warning and nothing else — so half 1 is "json.ty's line is absent"
  and half 2 is "main.ty's line is present", in the same byte comparison.

  **Negative control, both directions** (the mute is `src/tychoc.c@g_mute_warn`;
  `2376066` no longer reverse-applies, later edits moved those lines, so the
  guard itself was stubbed and the compiler rebuilt each time):

  ```
  (a) mute off      -> FAIL: + corelib/json/json.ty:932: warning: `keys` shadows ...
  (b) mute silences -> FAIL: - tests/warn/pkg/corelib_mute/main.ty:13: warning: `len` ...
      everything
  restored          -> ok
  ```

  (a) is the regression this phase exists for; (b) is why half 2 had to be in
  the golden — a mute that silenced everything passes a half-1-only test.

  `tools/prunner/main.ty` learned the lane too (`judge_warnpkg`, and the job
  push), because its report is meant to be byte-identical to `tests/run.sh`'s
  over the whole corpus, and a lane it does not know is a silent count
  difference. Its seven `tests/run.sh:N-M` header citations were re-anchored:
  the run.sh edits moved every lane down six lines and the citation gate caught
  one of them.

  Gates: `make test` **653 passed, 0 failed**, `ok warnpkg_corelib_mute` in the
  report (651 before; +1 here, +1 for Phase 11 in the same run).
  `make goldens-check` ok once the golden was tracked.

- [x] **Phase 13 — the shadowed-builtin warning's central claim is false for at
      least one builtin** *(discovered by the same work)*
      VERDICT: **the claim is false for EVERY builtin, and it is a compiler bug
      against a normative spec line — not a wording bug. Not fixed here; filed
      as Phase 18 below.**

  The warning says "every unqualified `X(...)` here calls this procedure, not
  the builtin". For `len` that is true and `tests/warn/shadow_builtin.ty` proves
  it — the fixture prints 7, not 4. For `hash` it is false:

  ```
  fn hash(x: int) -> int:
      return x * 2

  fn main():
      println(str(hash(7)))
  ```

  warns, then prints `-1407484600305285887` — the builtin's answer, not `14`.
  Reproduced on a plain program with no imports. Pre-existing and untouched by
  `2376066`, which only gates whether `warn_at` prints. Either the resolver
  should prefer the user's procedure uniformly, or the warning's wording is
  wrong for the subset of builtins that win; deciding which is the point of the
  phase. `src/tychoc.c@shadows_builtin` lists both names, so the warning fires
  identically for the two.

  **Evidence — the brief's premise was wrong, and so was this phase's.** Both
  said `len` is the case where the warning is TRUE and
  `tests/warn/shadow_builtin.ty` "proves it — the fixture prints 7, not 4".
  Built and run: **it prints 4.** Nothing ever checked, because
  `tests/run.sh:442` captures the warn lane's *stderr* only and never executes
  the program — the fixture's own comment asserted the answer and no gate could
  contradict it.

  All 57 names on `src/tychoc.c@shadows_builtin` were probed (declare
  `fn <name>` at package level, call it unqualified, mark the local body so
  "which body ran" is observed rather than inferred). Three groups:

  | group | names | warning? | who won |
  |---|---|---|---|
  | rejected at declaration, flat program — `'X' is already defined` | ~40, incl. `str` `split` `find` `chr` `sqrt` `pow` `print` `println` `die` `now` | yes, then a hard error | neither — cannot be declared |
  | declares, then the **builtin's own arg-check** refuses the local's call | `len` `keys` `push` `pop` `reserve` `to_float` `to_int` `to_str` `to_bool` `to_bytes` `send` `recv` `close` | yes | **builtin** |
  | declares and runs — local body provably never executes | `hash` `to_u8`–`to_u64` `to_i8`–`to_i64` `to_f32` | yes | **builtin** |

  **There is no name for which the local wins.** `eprintln` and `zero` appeared
  to, but neither is a builtin at all (`eprintln` → `unknown procedure`, closed
  FRICTION #6; `zero` → `core:bignum provides zero`), so they were warning about
  a collision that cannot happen — a false positive on
  `corelib/bignum/bignum.ty:23`'s own legal declaration. Both removed from the
  list, which the list's own comment already demanded ("a name wrongly present
  would warn about a legal declaration, which is worse",
  `src/tychoc.c:4139-4140`). Package mode behaves identically to flat: a
  `package main` declaring `len`/`hash` still prints `4` and the builtin's hash,
  with neither local body running.

  **Why this is not a wording fix.** `docs/spec/01-lexical.md:201-209` is
  normative and says the local "**shadows** that builtin for every unqualified
  call inside its own package — including calls from the procedure itself, which
  recurse", and that "a conforming implementation must not change which
  procedure is *selected*". The compiler selects the builtin. Rewording the
  warning to match today's behaviour would bless a spec violation, so the
  warning text is left exactly as it stands and the divergence is recorded
  instead.

  **Landed here:** the two false-positive names removed; the false "prints 7"
  comment in `tests/warn/shadow_builtin.ty` corrected (its golden re-recorded —
  line number only, warning text byte-identical); and
  `tests/shadow_builtin_selection.ty` added, which **runs** the shadowed calls
  and locks the real answers (`4`, `false`, `7`), closing the gate hole that let
  the claim rot. NEGATIVE CONTROL in that fixture: `mylen`, a name no builtin
  owns, returns 7 through the same call shape — so the fixture distinguishes
  "builtin won" from "this test measures nothing". Verified: `make test`
  **654 passed, 0 failed** (baseline 653 + this fixture), `make corelib` all
  green (46 ok), `make vm-check` green, `sh scripts/entrypoints.sh` 75 entry
  points, `check_citations` + `check_links` ok.

- [ ] **Phase 18 — the compiler selects the builtin where the spec says the
      package-local procedure wins** *(sized by Phase 13, not absorbed)*

  `docs/spec/01-lexical.md:201-209`, normative: a procedure named after a
  builtin shadows it for every unqualified call in its package, self-calls
  recurse, and "a conforming implementation must not change which procedure is
  *selected*". Today the builtin is selected in every case — see Phase 13's
  table. `tests/shadow_builtin_selection.ty` locks the divergence, so the fix
  must redden that fixture deliberately.

  **Sizing.** The builtin dispatch runs *before* the user proc table: the
  give-away is that `fn len(x: int)` + `len(3)` dies with `len(...) takes an
  array, a string, bytes, a map, or a soa` — the builtin's arg-check refusing a
  call that matches the local's signature perfectly. So the fix is not a lookup
  tweak but a reordering of call resolution, and every builtin call in the tree
  goes through that path. Two decisions have to be made first, and they are
  separable:
  1. **Selection** — a package-local proc of a builtin's name must win. Affects
     `make test`, `make corelib` and every tool lane at once; `core:utf8`'s
     historical stack-guard recursion is what "correct" looks like here, so the
     spec is asking for the sharper edge on purpose.
  2. **Declarability** — ~40 builtin names are rejected outright in a flat
     program (`'X' is already defined`), which contradicts the same spec section
     ("none is reserved", "A procedure declared with a builtin's name is
     therefore legal"). Either the spec narrows to package scope, or the
     rejection goes. This one is a spec question, not a compiler question, and
     should be answered before the resolver is touched.

  Not attempted here: Phase 13's brief was a diagnostic, and this is a
  resolution change with a corpus-wide blast radius.

- [x] **Phase 14 — a package-qualified function cannot be a value, and the
  diagnostic says the wrong thing.** VERDICT: **(a) CONTAINED, implemented.**

  Both halves of the report were real, and the report's own guess about the
  cause was wrong in a way worth recording.

  **Probe, generic (the reported case).** `apply2(math.min, 2, 3)` →
  `error: package 'math' has no variant or const 'min'`. False: the name exists
  at `corelib/math/math.ty@min` and `math.min(2, 3)` compiles.

  **Control, local.** The identical shape with a local `addi` builds and prints
  `5`. `tests/pkg/fnval/main.ty` already pinned the *same-package* case, so the
  asymmetry was cross-package only.

  **Probe, NON-generic — the one that decided the verdict.**
  `apply1(strings.to_upper, "hi")` → `error: package 'strings' has no variant or
  const 'to_upper'`. `strings.to_upper(s: string) -> string` is fully concrete.
  So genericity was NOT the cause and the report was right that the capability
  itself was missing; had only generics failed, the diagnostic would have been
  the whole bug.

  **Mechanism.** `resolve_expr`'s `E_FIELD` arm, for an imported package, tried
  const then variant then died — it never asked whether the name was a function.
  The `E_IDENT` arm had done exactly that for a mangled `<pkg>name` since
  `tests/pkg/fnval` was written, including `note_fnval`'s `__clo` thunk, which is
  emitted per name (`src/tychoc.c:13449-13454`) and so needed nothing new. Fix
  reuses it verbatim: `sig_find(q)` → function value (node rewritten to `E_IDENT`
  with `op = TK_FN`, `lhs` cleared so a re-resolve is idempotent);
  `generic_find(q)` → refuse, explaining that no instantiation exists and naming
  the lambda workaround; otherwise the miss message, widened to "variant, const
  or function" and given the `suggest_pkg_symbol` did-you-mean the call path
  (`src/tychoc.c:6138`) already had. 23 insertions, 2 deletions, one arm.

  **Spec.** `docs/spec/09-expressions.md` §13.6 excluded `inout` and
  comparability and never excluded a package-qualified name — the gap was
  incidental, not designed. §13.6 now says so, and states the generic exclusion,
  which was real and unwritten.

  **Fixtures.** `tests/pkg/fnvalcross/` + `.out` (argument, bound local,
  indirect call, void return, string in/out, generic still callable);
  `tests/reject/pkg/fnval_pkg_generic/` and `.../fnval_pkg_missing/`, each
  pinning two `# expect:` substrings.

  **Negative control.** `git checkout src/tychoc.c`, rebuild: all four
  `# expect:` lines reported `correctly MISSING`, the old compiler emitting
  `package 'genx' has no variant or const 'pick'` and `package 'opsy' has no
  variant or const 'bumpp'` — the same message for a valid use and a typo, which
  is the defect. `tests/pkg/fnvalcross/main.ty` failed to build under it. Source
  restored from a verified backup and rebuilt; golden matches.

  **Gates.** `make check-links` ok · `make vm-check` green ·
  `sh scripts/entrypoints.sh` ok (75) · `make corelib` all green (46 ok, no
  skip) · `sh scripts/spec_check.sh` 11 runnable examples all pass ·
  `make goldens-check` ok (448 golden files, all tracked) · `make test`
  **passed: 648 failed: 0** (baseline 645 + the 3 fixtures above).
  Citation drift: +21 lines staled 49 refs; `scripts/reanchor_citations.py
  --apply` rewrote 119 files, 0 needing a human, and the checker is green.

- [x] **Phase 15 — a LOCAL generic function used as a value gives the same class
  of false message, in the other arm.** `apply2(mymin, 2, 3)` for a local
  `fn mymin(a: $T, b: $T) -> $T where comparable(T)` dies `unknown variable
  'mymin'; did you mean 'main'?`. The name is not unknown and `main` is not the
  suggestion a reader wants. Cause is the mirror of Phase 14: `resolve_expr`'s
  `E_IDENT` arm tries `sig_find` and, on a miss, goes straight to the
  unknown-variable path without consulting the generics registry, which is where
  a generic template lives (`src/tychoc.c:6135`). Phase 14 fixed this for the
  package-qualified spelling only, and deliberately did not widen scope. The fix
  is the same two lines — `generic_find` before the `suggest_var` fallback,
  reusing Phase 14's wording — plus a `tests/reject/` fixture with `# expect:`.
  Gate: `make test` (648 at Phase 14, so 649).

  **Done.** The brief's cause was CORRECT, and checked rather than inherited:
  `resolve_expr_inner`'s `E_IDENT` arm ran `sig_find` → `suggest_var` →
  `suggest_fn` → die, never touching `generic_find`
  (`src/tychoc.c:5690-5702@suggest_var`).

  Outcome chosen of the three: **refuse with the reason**, not implement. A
  generic has no single value form, and the deciding fact is structural rather
  than a matter of taste — `resolve_expr_inner(Expr *e)` takes no expected type
  (`src/tychoc.c@resolve_expr_inner`), so at a value site nothing fixes `$T`.
  Instantiating would mean threading a target type into ident resolution, which
  is far outside this phase. Same call Phase 14 made for `pkg.fn`, same wording.

  Narrower than the report implied, and worth recording: a local NON-generic
  function as a value was never broken. `apply2(addi, 2, 3)` printed `5` in the
  same file where `apply2(mymin, 2, 3)` died, so the defect was genericity
  alone — the `E_IDENT` arm's `sig_find` path (`tests/pkg/fnval`) already worked.

  Old: `unknown variable 'mymin'; did you mean 'main'?`
  New: `'mymin' is generic, so it has no single function value -- there is no
  instantiation to take. Wrap it in a lambda that fixes the types, e.g.
  fn(a: int, b: int) -> int: mymin(a, b)`

  A refusal that names a workaround is only honest if the workaround runs, so it
  is executed, not just asserted: `tests/fnval_generic_wrap.ty` compiles the
  suggested lambda at `int` and at `string` and prints
  `plain=5 / wrapped=2 / direct=2 / strwrap=fig`. `tests/reject/fnval_local_generic.ty`
  pins both halves of the new wording with `# expect:`.

  Negative control: `git stash push src/tychoc.c` + `make tychoc` → the old
  binary emits `tests/reject/fnval_local_generic.ty:21: error: unknown variable
  'mymin'; did you mean 'main'?`, so BOTH `# expect:` substrings are absent and
  the fixture fails. Restored (`diff -q` against the saved copy) and re-run: the
  new wording is back.

  Gates: `make check-links` ok (119 md, no dead links), `make vm-check` green,
  `sh scripts/entrypoints.sh` ok (75), `make goldens-check` ok (450 goldens, all
  tracked — both new `.out` files needed an explicit `git add`, exactly the trap
  `CLAUDE.md` documents). `make test` was run ONCE at the end of Phase 16,
  covering this phase and that one together. The +6 lines staled 49 citations;
  `scripts/reanchor_citations.py --apply` rewrote 119 files, 0 needing a human.

- [x] **Phase 16 — `(pkg.fn)(x)`, the parenthesised immediate call, dies
  `unknown variable 'pkg'`.** Found writing Phase 14's fixture; the line was
  removed from it rather than absorbed. `g := opsx.brack` then `g("yo")` works,
  and `opsx.brack("yo")` works, but the parenthesised form in between does not:
  a call whose callee is an expression resolves that callee down the
  call-on-expression path, which reaches `E_FIELD` with the package ident as an
  ordinary `lhs` and resolves it as a variable. Pre-existing and unrelated to
  Phase 14's change (it reproduces identically before it). Decide whether the
  form is worth supporting at all — it is redundant with both working spellings
  — or whether the message should simply name the package and say so. Gate:
  `make test`.

  **Done — supported, because the size judgement came out small.** The brief
  asked for a size call before any fix, and the answer was one condition on one
  `if`, inside the same package-qualified concern Phase 14 touched. Nothing in
  how calls resolve generally was reordered, so the structural risk the brief
  warned about never arose.

  **The brief's stated cause was WRONG, and this matters** — it was inherited
  from Phase 14's note, not re-derived. The claim was that the call-on-expression
  path "reaches `E_FIELD` with the package ident as an ordinary `lhs` and
  resolves it as a variable". That path is innocent: it resolves the `E_FIELD`
  correctly, which is why `f := (opsp.brack)` **already compiled**, and why
  `println((opsp.brack))` got as far as a type error naming `fn(string) -> string`.
  Those two probes are what killed the stated cause.

  The real culprit runs BEFORE it: the Stage-2 UFCS rewrite
  fires on any `E_CALL` whose callee is an `E_FIELD` and immediately probes the
  callee's base for a receiver type — `src/tychoc.c:6026@resolve_expr`. For
  `pkg.fn` that base is a package name, not a value, so the probe itself died
  before the call-on-expression path at `src/tychoc.c:6095` was ever reached.
  The new guard is `src/tychoc.c:6024@is_imported_pkg`.

  Fix: exclude an imported package qualifier from that probe, using the same
  `is_imported_pkg` test the `E_FIELD` arm gates its own package case on, so the
  two agree on precedence rather than inventing a second rule. The node then
  falls through to call-on-expression, where Phase 14's arm yields the function
  value and the call goes indirect. `(pkg.fn)(x)` now runs.

  Regression risk taken seriously, since the excluded probe is what UFCS
  chaining is FOR: `tests/pkg/fnvalparen/main.ty` exercises `b.twice().shown()`
  and `b.twice().twice().shown()` in the same file as the fixed spelling, so the
  exclusion cannot silently disable chaining. `make test` at 651 is the wider
  proof — the corpus is full of UFCS chains.

  Negative control: `git stash push src/tychoc.c` + `make tychoc` → the old
  binary dies `tests/pkg/fnvalparen/main.ty:26: error: unknown variable 'opsp'`,
  so the fixture cannot compile at all. Restored, rebuilt, output byte-identical
  to `tests/pkg/fnvalparen.out`.

  Gates: `make check-links` ok, `make vm-check` green, `sh scripts/entrypoints.sh`
  ok (75), `make goldens-check` ok (450). **`make test`: 651 passed, 0 failed**
  — run ONCE here, covering Phase 15 and this phase together (648 at Phase 14 +
  3 new fixtures). The +6 lines staled 49 citations;
  `scripts/reanchor_citations.py --apply` rewrote 119 files.

  One citation needed a human, and it was rotted BEFORE this phase rather than by
  it — `docs/rfc/ffi-threading-design-review.md:148-149` cited lines 5975-5979 of
  `src/tychoc.c` for `is_extern_str_call` and line 6017 for its `len`
  application, both roughly 4,000 lines off. It surfaced only because this hunk
  landed on that same line 6017. Repaired
  with coordinates read from source: the definition is now cited as
  `src/tychoc.c@is_extern_str_call` (the insertion-proof form CLAUDE.md prefers),
  applied at `src/tychoc.c:9947` for `len` and `:10074`/`:10081` for
  print/println. A coordinate repair, not the prose rewrite Phase 6 collects.

## Out of scope

`plan_windows.md` is a separate track and is not touched by this plan.

`make ci` and standalone `make test` are not run as ritual — each phase runs
only the lane that can redden for what it touched. Pushing is the user's call,
not a phase's; phases commit and stop.
