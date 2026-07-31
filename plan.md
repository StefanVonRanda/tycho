# Make the citation gate see what it currently cannot

Previous plan complete and archived at
[docs/internals/plan-signals-DONE.md](docs/internals/plan-signals-DONE.md)
(four phases plus batches A–D and phases 19–26; `make ci` observed
`CI_EXIT=0`). Phases 21, 23 and 25 were retired to `FRICTION.md` by decision
rather than swept.

## Goal

`scripts/check_citations.py` has two blind spots, and this repo has now spent
several days paying for both.

1. **A bare `path:N` is bounds-checked only.** It passes as long as the file has
   that many lines. **2793 refs** are in that category against 191 anchored ones
   that are actually verified. The last plan's own edits moved lines and the gate
   caught **15 anchored** refs while catching **none** of the bare ones
   describing the same code.
2. **"`plan.md` phase N" is invisible entirely** — no line number, so the gate
   never looks. 172 such references rotted across 44 files before anyone measured
   it, and the only cure currently in place is a written rule in `CLAUDE.md` that
   nobody enforces.

Done looks like: the second is closed by a gate with an exact predicate, and the
first has a decided, documented policy — not 2793 hand repairs.

## Pre-flight

- **Worst case:** a gate that fires on so much that it gets switched off, or
  worse, that trains everyone to write around it. The anchored form is
  *better*, but it is not free — an anchor names a token, and a token that
  appears twice in range makes a false anchor that passes while pointing at the
  wrong thing. An earlier plan found anchored ranges 9 and 82 lines off doing
  exactly that. **More anchoring is not automatically more truth.**
- **Reversibility:** total. Both changes are to one Python script plus whatever
  refs they force; nothing here touches the compiler or the language.
- **Verified — the asymmetry is real and measured.** `python3 scripts/check_citations.py`
  reports `191 anchored contain the token they name, 2793 bare in bounds, 248
  source->doc citations resolve, 247 source->source in bounds, 16 source->source
  anchored`. The word "bounds" is doing all the work in two of those clauses.
- **Verified — the phase-24 predicate is exact and its population is known.**
  Outside `plan.md` itself and the frozen `docs/internals/plan-*-DONE.md`, no
  file should contain "`plan.md` phase N": it is either a live-plan reference the
  archiving commit must rewrite, or it is already stale. Batch C measured 5 such
  refs at the time (`FRICTION.md`, `server/main.ty`, `server/run.sh` ×3), all
  legitimately about the plan that has since been archived — **so they are stale
  now**, which is what makes this the right moment.
- **Verified — freezing did not make the stale refs right.**
  `docs/internals/plan-signals-DONE.md` was archived carrying refs that were
  already wrong when it froze: its `corelib/signal/signal_shim.c:81` names the
  middle of a comment (the statement is at `:148@sigx_flag`), and the `:77-85`
  handler span and a tail of `server/main.ty:N` refs moved the same way. The
  frozen-record rule protects citations that were **true when written**; these
  were false at the moment of freezing, which is a different thing and phase 1
  repairs them as a stated one-time exception.
- **Assuming — requiring anchors everywhere is the wrong answer and I have not
  proven it.** 2793 refs is too many to convert by hand, most are ranges with no
  single subject token, and the false-anchor hazard above is real. The likely
  right answer is to require anchoring only where a ref is *load-bearing* — a
  `> Provenance:` block already does this — and to leave narrative refs bare.
  **Risk if wrong:** a policy that sounds principled and changes nothing. Phase 2
  must justify its line with counts, not taste.

## Phases

- [ ] **Phase 1 — repair the outgoing plan's already-false refs, then gate "`plan.md` phase N"**
  - Scope: `docs/internals/plan-signals-DONE.md` (the one-time exception above),
    the 5 live "`plan.md` phase N" refs batch C left, and
    `scripts/check_citations.py`.
  - Repair first, gate second — the gate cannot go green while the refs it
    forbids are still present, and that ordering is the discipline it enforces.
  - The predicate: outside `plan.md` and `docs/internals/plan-*-DONE.md`, no file
    may contain "`plan.md` phase N". Note batch C found **three spellings** —
    plain, backticked, and the plural "phases 1 and 2" — and the original count
    of 110 missed two of them. Match all three.
  - Done when: the gate rejects a planted "`plan.md` phase 3" outside the allowed
    files, accepts the tree as it stands, and the previously-stale refs name the
    archived document they actually meant.
  - Verify: `python3 scripts/check_citations.py`, then the planted-violation
    proof both directions, then `sh scripts/check_links.sh`.

- [ ] **Phase 2 — decide the bare-ref policy, and implement the decision**
  - Scope: `scripts/check_citations.py`, `CLAUDE.md`'s Citations section, and
    whatever refs the chosen policy forces.
  - **Count before deciding.** Break the 2793 down by context — how many are in
    `> Provenance:` blocks (already anchored-by-rule), how many in source
    comments, how many in prose, how many are ranges versus single lines. A
    policy proposed without that breakdown is a guess.
  - Then choose, and justify with the counts: require anchors in named contexts,
    or add a warn-with-count lane that reports drift without failing, or
    something else the numbers suggest. **"Convert all 2793" is not a candidate**
    — this repo has twice decided against hand-sweeping this class and recorded
    why in `FRICTION.md`.
  - Whatever ships must not make the false-anchor problem worse: if a token
    appears more than once in the cited range, the anchor is not a check.
    Consider whether the gate should reject an ambiguous anchor outright.
  - Done when: the policy is implemented, documented in `CLAUDE.md`, and proven
    both directions on a planted violation.
  - Verify: `python3 scripts/check_citations.py`, the planted-violation proof,
    `sh scripts/check_links.sh`, `sh scripts/spec_check.sh`.

## Out of scope

- **Sweeping the 2793 bare refs.** Retired to `FRICTION.md` on 2026-07-31 with
  the measurements that justify it; this plan changes the *gate*, not the refs.
- **The three retired drift phases** (21, 23, 25). Same decision.
