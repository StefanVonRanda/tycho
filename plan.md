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

## Out of scope

`plan_windows.md` is a separate track and is not touched by this plan.

`make ci` and standalone `make test` are not run as ritual — each phase runs
only the lane that can redden for what it touched. Pushing is the user's call,
not a phase's; phases commit and stop.
