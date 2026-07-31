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

- [x] **Phase 1 — repair the outgoing plan's already-false refs, then gate "`plan.md` phase N"**
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

- [x] **Phase 2 — decide the bare-ref policy, and implement the decision**
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

### Phase 1 evidence — 2026-07-31

#### Part 1 — the archived plan carried 38 false refs, not 3

The three named in its phase 27 entry were the visible end of the tail. Method,
because a line-delta bump is what produced several of the wrong repairs this
repo has already recorded: for every one of the **290 citations** in
`docs/internals/plan-signals-DONE.md`, `git blame -C -M` on the *citing* line
gives the commit that wrote it; `git show <commit>:<path>` gives the file **as
the author saw it**; the cited text is then located in today's file by **content
identity**, not by arithmetic. 125 of the 290 point into the three files batch E
moved (`server/main.ty` 70, `server/run.sh` 31, `corelib/signal/signal_shim.c`
24); of those, 36 still resolve correctly and the rest do not.

**38 were repaired** — every stale ref in the phase 1-3 and batch A/B evidence
prose, which is the population phase 27 described. The three it named came out
exactly as it predicted:

```
doc:296  `corelib/signal/signal_shim.c:81`      -> `corelib/signal/signal_shim.c:148@sigx_flag`
doc:290  `corelib/signal/signal_shim.c:77-85`   -> `corelib/signal/signal_shim.c:144-156`
doc:617… `server/main.ty:617` (x5 sites)        -> `server/main.ty:753@stopped`
```

**8 single-line repairs gained an anchor**, chosen as the rarest identifier on
the repaired line, so the next edit that moves them reddens the gate instead of
drifting: `@sigx_flag`, `@sigx_fd`, `@SHUT_RDWR`, `@saved`, `@clobber`,
`@sa_flags`, `@shutdown_requested`, `@MAX_ACCEPT_FAILS`. Ranges were left bare,
per the standing rule that a range has no single subject token.

Two range endpoints the automated relocation got wrong were caught by reading
the construct and fixed by hand: the handler span ends at the closing brace
`corelib/signal/signal_shim.c:156`, not at the inner `}` of the new slot loop,
and `serve_conn` is `server/main.ty:375-516`, not the span-preserving guess.
**More anchoring is not automatically more truth** — this is the same hazard the
pre-flight names, met in practice.

#### Part 2 — 18 live refs, not 5, and the reason the count was low

Re-derived as instructed. Batch C's 5 became **6**: `FRICTION.md:739`,
`server/main.ty:741`, `server/run.sh:328`, `:479`, `:602`, and `server/run.sh:650`,
which batch E wrote *after* batch C took its count. All six meant the plan
archived at `23adb1d` and now name `docs/internals/plan-signals-DONE.md`.

**The other 12 are a discovery, and they explain the "three spellings" warning.**
Batch C's sweep was case-sensitive: it matched `phase` and missed `Phase`. Left
behind, all capitalised — `Makefile:139`, `scripts/ci.sh:50`,
`scripts/asan_self.sh:17` and `:39`, five `tests/reject/*.ty` fixtures,
`docs/internals/frontend-restriction-audit-2026-07-25.md:358`,
`docs/internals/int64-migration-audit.md:280`, and
`examples/webserver/README.md:50-51`, which is *both* capitalised and wrapped
across two lines. Two more sat in `scripts/check_citations.py`'s own docstring.
Sweeping them was not in the phase's stated scope; it became unavoidable, because
a gate that cannot go green is not a gate. It is recorded here rather than
absorbed silently.

**Attribution was checked, not guessed**, by batch C's own method: the commit
that *adds* `plan-X-DONE.md` is the instant X stopped being live, so the windows
tile; `git blame` puts each citing line in exactly one; and the phase number
cited must be one the mapped document declares. **20 of 20 passed.** Eleven map
to `plan-front-door-DONE.md`, one each to `plan-int64-DONE.md` and
`plan-webserver-DONE.md`, six to `plan-signals-DONE.md`.

#### Part 3 — the gate

Predicate, in `scripts/check_citations.py` as a fourth direction: outside
`plan.md` and the frozen `docs/internals/plan-*-DONE.md` set, no tracked file may
carry a phase reference into the rotating plan. Exemptions follow the existing
`ARCHIVED` / `SRC_SKIP_CITER` shape rather than a second mechanism —
`PLANREF_SKIP` names the live plan and `compiler/tychoc0.ty`, the latter for the
reason it is already skipped as a citer: it is frozen and an unfixable red.

The pattern is deliberately loose because **four separate surveys of this class
each under-counted it**: optional backticks, `phase` or `phases`, `re.I` for the
case variation batch C missed, and a separator class permitting a newline plus a
comment leader. It matches whole-file text, not lines, because a line loop is
precisely what hid the wrapped ones.

**`check_citations.py` is NOT exempt from its own rule**, so neither the pattern
nor the failure message spells the form it forbids — the same discipline the
absolute-path rule follows. The message says what to do (name the archived plan,
with the boundary/blame/check recipe), not merely that something is wrong.

#### Planted-violation proof, both directions

Five spellings appended to `server/README.md`, a file outside the allowed set:

```
$ python3 scripts/check_citations.py
STALE  server/README.md:292  'plan.md phase 3' -> a phase reference into the rotating plan, ... Name the archived plan: `docs/internals/plan-<name>-DONE.md` phase 3. (`git log --diff-filter=A -- docs/internals/` gives the rotation boundaries; ...)
STALE  server/README.md:293  '`plan.md` phase 3' -> ...
STALE  server/README.md:294  '`plan.md` phases 1' -> ...
STALE  server/README.md:295  'plan.md Phase 3' -> ...
STALE  server/README.md:296  '`plan.md`\nphase 3' -> ...
citation check: FAILED (5 stale citation(s) above)
EXIT=1
```

All five caught, each naming the archived form to write instead. Plant removed
(`git diff --stat -- server/README.md` empty), and:

```
$ python3 scripts/check_citations.py
citation check: ok (199 anchored contain the token they name, 2787 bare in bounds, 265 source->doc citations resolve, 247 source->source in bounds, 16 source->source anchored)
EXIT=0

$ sh scripts/check_links.sh
link check: ok (137 markdown files, no dead relative links)
EXIT=0
```

Anchored 191 → **199** (the 8 new anchors); bare 2793 → 2787; source→doc 248 →
**265**, the rewrites having introduced 17 new `docs/internals/plan-*-DONE.md`
mentions from source files, each now existence-checked. The new pass reports its
own count under `--stats` only, so the `ok` line stays comparable with the
archived evidence that quotes it. Writing *this* evidence block then took the
totals to **200 anchored / 2802 bare**, which is the tree's final state — and
one of its own refs reddened the gate first, a bare `:1794-1811` inheriting a
`docs/` path from the line above. It was caught, not by review, but by phase
63's same-line rule. Fifth phase in this repo to redden on its own write-up.

#### One consequence worth recording

Inserting the docstring section moved `ARCHIVED` from `scripts/check_citations.py:316`
to `:362`. Six refs named the old line; **three were anchored and reddened the
gate immediately**, three were bare and would have rotted silently. All six were
re-pointed. That is the phase-2 asymmetry reproducing itself inside phase 1.

#### Gates run, and the one deliberately not run

`python3 scripts/check_citations.py` and `sh scripts/check_links.sh`, per the
brief and `CLAUDE.md`'s gate budget. **`make test` was not run**: every edit
outside the two docs is a comment — `#` lines in `Makefile`, `scripts/ci.sh`,
`scripts/asan_self.sh`, `server/main.ty`, `server/run.sh` and five
`tests/reject/*.ty` fixtures. No line was added or removed in any source file,
so no citation into one shifted, and the flat reject lane scores on exit status
plus a non-empty diagnostic rather than on golden text.

### Phase 2 evidence — 2026-07-31

#### The breakdown, which decided everything below

Counted by walking the gate's own parser (imported, not re-implemented, so the
totals reconcile with `--stats`): same `CITE` regex, same paragraph-scoped
inheritance, same skips. 2802 bare Markdown refs.

| bucket | bare | note |
|---|---|---|
| frozen `docs/internals/plan-*-DONE.md` | **1800** | every rule in the gate already refuses to demand an edit here |
| live plan evidence blocks | 17 | a record of what a ref said, not a claim about today |
| `> Provenance:` ranges | 196 | exempt by the settled rule, correctly |
| reachable narrative prose | **789** | the only population a policy could act on |

By shape: 1579 single-line, 1223 ranges. By path form: 1901 spell the path, 901
inherit it. By file class outside the frozen set: `docs/spec` 299,
`docs/internals` live 257, other Markdown 219, `docs/rfc` 196, other `docs/` 14.
Concentration is extreme — `docs/internals/plan-front-door-DONE.md` alone holds
593, and `FRICTION.md` 209. Source comments are a separate, already-separate
population: 247 bare source→source refs, half of them in `tools/` and `tests/`.

**The number that decided the policy is 0.** Inside `> Provenance:` blocks —
the one context in this tree where anchoring is mandatory — **zero** single-line
refs are bare. All 196 bare provenance refs are ranges, which the rule exempts on
purpose. The mandatory context is at 100%, so "require anchors in named
contexts" has no second context left to name: it would have to invent one and
then sweep the 789 into it. **"Convert all 2793"** was never the real number
either; two thirds of it is frozen record that no rule here may touch.

Commands: `/tmp/.../breakdown.py`, `ambig.py` and `prov.py` (scratchpad, not
committed) — each imports `scripts/check_citations.py` and re-walks with the
same guards, bucketing instead of failing.

#### The decision: strengthen the anchors that exist, do not spread them

Three candidate ambiguity predicates were measured before one was chosen, over
all 216 anchors (200 Markdown, 16 source→source):

| predicate | Markdown hits | source→source | verdict |
|---|---|---|---|
| token on >1 **line inside the range** | **1** of 200 | 0 of 16 | **ships as a hard failure** |
| token recurs within ±25 lines | 54 (31 non-frozen) | 8 | counted, never failed on |
| token recurs anywhere in the file | 122 (76 non-frozen) | 10 | counted, never failed on |

So: **an ambiguous anchor is now a failure** in both anchored directions. A
token on more than one line of the cited range names none of them — the region
can drift inside itself and the check still passes, which reads as verified
while verifying nothing. Population was 1, in `docs/spec/03-types.md`'s
`bounded`-capacity Provenance block: the ref read compiler/tychoc0.ty:11908-11912
anchored to "[b#" (spelled here **without backticks on purpose** — backticked it
is a live citation, and this record of a repaired ref would redden the very rule
it documents), and it opened three comment lines above the guard it meant.
Repaired to `compiler/tychoc0.ty:11912-11913@[b#`, the `if`/`die_at` pair — the anchor
became unique **and** the citation more precise, which is the repair this rule
is meant to produce.

**Anchor strength is measured, not enforced.** The other two predicates are
printed by `--stats` and were not made failures, because the counts say
enforcing manufactures exactly the hazard the pre-flight names: 17 of the 97
mandatory single-line `> Provenance:` anchors are weak in the ±25 window — four
separate refs anchor `@parse_value_ctrl` to four different lines of the same
function, each matching all four; `@elem` occurs 324 times in `src/tychoc.c`.
Clearing that red means inventing 17 replacement tokens chosen by whoever is
clearing it. **More anchoring is not automatically more truth** — now with a
number instead of a warning.

**The green line now splits the bare total** into the four buckets above. That
total is what this plan's own Goal read as a backlog, and it was read that way
off this line. Splitting it there rather than only under `--stats` is a
deliberate departure from phase 1's keep-the-`ok`-line-comparable discipline,
noted in the docstring: the undivided number is the defect.

#### The two hazards

**False anchors:** addressed head-on — the ambiguity rule is the strongest form
the counts supported, and the two weaker forms were rejected *with* their counts
rather than on taste. The gate now claims only what it checks: the `ok` line
reads "anchored contain the token they name **and each names one line**".

**Record blocks:** the policy ships **no ratchet, no budget, no shrink target**,
and says so in both `CLAUDE.md` and the docstring. Pressure on the bare count is
the mechanism that would eventually point someone at a before/after block whose
line numbers are data. Two of the four buckets on the green line — frozen record
and live-plan evidence, 1817 refs — are labelled as unreachable *on the line
itself*, so the number a reader is left with is 789 and no instruction to move
it. `CLAUDE.md` states the rule with no mechanism, deferring the mechanism to
the phase already filed for it.

#### Planted-violation proof, both directions

Planted in `server/README.md` (Markdown pass) and `scripts/docs_fences.sh`
(source→source pass): an ambiguous anchor and, over the **identical range**, a
control anchored to a token unique in it.

Transcript below with the planted refs' **backticks replaced by quotes**: spelled
verbatim they are live citations and this block would redden the rule it is
recording. Nothing else is altered.

```
$ python3 scripts/check_citations.py
STALE  server/README.md:292  'src/tychoc.c:1-40@source' -> AMBIGUOUS ANCHOR: 'source' is on 5 lines of src/tychoc.c (:3, :4, :12, :35, ...), so it names none of them and a drift inside the range still passes. Anchor a token that occurs once, tighten the range to its construct, or drop the anchor -- a range with no single subject token is honestly bare.
STALE  scripts/docs_fences.sh:162  'src/tychoc.c:1-40@source' -> AMBIGUOUS ANCHOR: 'source' is on 5 lines of src/tychoc.c (:3, :4, :12, :35, ...) [...]
citation check: FAILED (2 stale citation(s) above)
EXIT=1
```

Exactly two failures: one per direction. **Both `@Pipeline` controls passed
silently** over the same range — the rule rejects ambiguity, not anchoring. A
third arm planted the same ambiguous anchor in a frozen archive
(`docs/internals/plan-int64-DONE.md`): the failure count stayed at 2, confirming
the frozen exemption. All plants removed, `git diff --stat` clean, and:

```
$ python3 scripts/check_citations.py
citation check: ok (200 anchored contain the token they name and each names one line, 2802 bare in bounds (1800 frozen record, 17 live-plan evidence, 196 exempt `> Provenance:` range, 789 reachable prose), 266 source->doc citations resolve, 247 source->source in bounds, 16 source->source anchored)
EXIT=0

$ sh scripts/check_links.sh
link check: ok (137 markdown files, no dead relative links)
EXIT=0

$ sh scripts/spec_check.sh
spec-examples: 9 runnable example(s), all pass
EXIT=0
```

#### The gate reddened on this phase's own edits, three times

Inserting the docstring moved the `ARCHIVED` constant from `:362` to `:434`;
three refs named the old line and **all three were anchored, so all three
reddened immediately** — two in a frozen archive, one in `scripts/docs_fences.sh`.
Repointed. A later docstring edit moved it one line further and reddened the
same three again. That is phase 1's recorded consequence reproducing twice
inside one phase, and it is the asymmetry this plan is about, seen from the
inside: an anchored ref to a moving line is *noisy*; a bare one is *silent*. The
noise is the feature. Filed as phase 30, because the right fix is not more
repointing.

**And a third time, on this very evidence block** — three AMBIGUOUS ANCHOR
failures in `plan.md` itself the moment it was written: the record of the
repaired ref, and the two planted refs inside the quoted transcript. All three
are *records of citations*, not citations, and nothing in the tree distinguishes
those two things — which is precisely the hazard phase 29 is filed for, met
in practice within an hour of the policy that was designed around it. The fix
used here is the cheapest one available: **drop the backticks**, since the gate
only reads backticked spans in Markdown, and say in the prose that they were
dropped and why. That is a convention, not a mechanism. Sixth phase in this repo
to redden on its own write-up.

Also caught in passing and fixed before shipping: the new failure message
printed a count of 5 beside a list of 4 line numbers, silently truncated. A list
that reads as complete while being partial is the same overstatement this whole
phase is about, so it now elides explicitly (`where_at`).

#### Gates run, and the ones deliberately not run

`python3 scripts/check_citations.py`, `sh scripts/check_links.sh`,
`sh scripts/spec_check.sh` — all green, per the brief's budget. **`make test`
and `make ci` were not run.** Every edit is Markdown, a Python doc gate, or a
`#` comment line in `scripts/docs_fences.sh`; no compiled artifact and no
fixture is reachable from this diff.

- [ ] **Phase 30 — a citation to a definition should not be a line number**
  - Scope: `scripts/check_citations.py`, and the refs a symbol form would replace.
  - Three refs cite the `ARCHIVED` constant in `scripts/check_citations.py` by
    line. That line moved twice during phase 2 alone and once during phase 1 —
    not because the constant changed, but because prose above it grew. Every one
    of those repairs was mechanical and none carried information.
  - The refs want to name a **symbol**, not a line: a `path@SYMBOL` form with no
    number, checked by locating the token, would be stable under insertion above
    it and would still redden if the symbol were renamed or deleted.
  - **Count first**, as ever: how many anchored refs in the tree point at a
    definition line rather than into a region? A mechanism for three is not
    worth building — phase 29's own test, applied here.
  - Verify: `python3 scripts/check_citations.py`, the planted-violation proof,
    `sh scripts/check_links.sh`.

- [x] **Phase 31 — frozen archives are not as exempt as the docstring says**
  - Scope: `scripts/check_citations.py`'s docstring, or its `frozen` guard.
  - The file states repeatedly that a frozen `docs/internals/plan-*-DONE.md` is
    never asked for an edit. That is true of the mandatory-anchor rule, the
    absolute-path rule, the `docs/`-inheritance rule and the new ambiguity rule
    — but **not** of the anchor content check, which policed two frozen refs in
    `docs/internals/plan-loops-cleanup-DONE.md` in this phase and six in phase 1.
  - So the real rule is "a frozen record is never asked to change, unless it
    anchored a ref that has since drifted" — which is defensible, and is not
    what any of the four "ARCHIVED PLANS ARE EXEMPT" paragraphs say.
  - Decide which is right and make the file say it. Do not change behaviour and
    documentation in the same motion without saying which one moved.
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`.

- [ ] **Phase 28 — the 6 refs phase 1 refused to repair, and why refusing was right**
  - Scope: `docs/internals/plan-signals-DONE.md` only.
  - Six refs spell `server/main.ty:493-494` (plus a `:494` and a `:494-495` in
    one before/after line) and claim it "sets `running = false` in the `Err` arm
    of accept". The construct did not move — batch A **deleted** it, deliberately,
    and `server/main.ty:520` is a comment block headed "WHY THE ERR ARM IS NOT
    `running = false`" explaining the decision. So there is no line to relocate
    to: the nearest true statement is `server/main.ty:594`, and pointing there
    would make a frozen record assert something about today's code that batch A
    specifically disproved.
  - Phase 1 failed closed (RULE 7): a wrong write is worse than a skipped one.
  - The real question is which of two things these are — a citation to repair, or
    a claim about superseded behaviour that should be marked as such and left.
    **Decide that before touching a line number.**
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`.

- [ ] **Phase 29 — decide what a before/after record block is, so nobody "repairs" one**
  - Scope: `docs/internals/plan-signals-DONE.md`, `CLAUDE.md`'s Citations section.
  - Phase 1 deliberately left three regions alone: the phase-4 repair log
    (`docs/internals/plan-signals-DONE.md:751-754` and `:941-943`) and batch D's
    before/after table (`docs/internals/plan-signals-DONE.md:1794-1811`). Their
    line numbers are **data** — a record
    of what a ref said at a past moment — and repairing them would falsify the
    record, which is the exact thing the frozen-record rule exists to prevent.
  - But nothing in the tree marks them as data, and phase 1 told them apart only
    by reading each one. The next sweep will not be so careful, and a sweep that
    "fixes" a before/after column destroys evidence irreversibly.
  - Options: a convention the gate can see, a marker in the block, or a written
    rule with no mechanism. **Count how many such blocks exist across all twelve
    archived plans before choosing** — a mechanism for three blocks is not worth
    building.
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`.

## Status — PLAN COMPLETE

Both phases done, each verified and committed on its own.

| phase | commit | what shipped |
|---|---|---|
| 1 | `dd3c019` | the fourth direction: a rotating-plan phase reference outside the live plan and the frozen set is a hard failure. 38 already-false refs in the outgoing archive repaired first, 20 live refs repointed (18 found against a predicted 5 — the sweep that took the original count was case-sensitive). |
| 2 | this commit | the bare-ref policy, decided from a full breakdown: bare stays bare, an **ambiguous anchor** is a hard failure in both anchored directions, anchor **strength** is measured and published, and the green line splits the bare total into what a policy can and cannot reach. |

**The Goal's two blind spots are closed as stated.** The second is now an exact
predicate with a planted-violation proof. The first has a decided, documented
policy and 1 repair — not 2793.

**What the numbers say now**, and it is a different claim than before: 201
anchored refs each contain the token they name **and each names exactly one
line**; 2802 bare refs are in bounds, of which 789 are the only ones any policy
could act on. The gate's own honesty about the bare form is unchanged and
deliberate — a bare ref that drifts onto a different-but-existing line still
passes, and no line-checker can see it.

**Open, filed, not closed here:**

- **Phase 28** — the 6 refs phase 1 refused to repair on RULE 7 grounds. The
  construct was deleted, not moved, so there is no line to relocate to. Decide
  whether they are citations or claims about superseded behaviour.
- **Phase 29** — what a before/after record block *is*, so nobody "repairs" one.
  Phase 2 hit this from the inside (see its evidence) and worked around it with
  a convention; the mechanism question is still open.
- **Phase 30** — a citation to a definition should not be a line number. The
  `ARCHIVED` constant's refs were repointed three times across two phases, none
  of the repairs carrying information.
- **Phase 31** — frozen archives are not as exempt as the docstring claims; the
  anchor content check polices them and four "ARCHIVED PLANS ARE EXEMPT"
  paragraphs say otherwise.

Retired by decision rather than swept, with measurements in `FRICTION.md`:
sweeping the reachable bare refs, and the three drift phases 21, 23 and 25.

## Out of scope

- **Sweeping the 2793 bare refs.** Retired to `FRICTION.md` on 2026-07-31 with
  the measurements that justify it; this plan changes the *gate*, not the refs.
- **The three retired drift phases** (21, 23, 25). Same decision.

  ### Phase 31 evidence — 2026-07-31

  **The documentation moved; behaviour did not.** Stated first because the phase
  required saying which one moved. `git diff --numstat` is `10 10` — line-neutral
  on purpose, since `scripts/check_citations.py` is cited by line from four
  archived plans (`:109-122`, `:211-215`, `:247`, `:362`) and an insertion would
  have staled them while the gate stayed green.

  **The real rule, now written where the overclaim was:** a frozen record is
  never asked to renumber a BARE ref, because that would falsify a recorded
  observation. An ANCHORED ref is different and was never exempt — it promised a
  token sits on that line, and a promise that has stopped holding misinforms
  rather than merely dates. Three paragraphs corrected: the header's blanket
  "this gate must never demand an edit there", and two that said the exemption
  held "as everywhere else in this file".

  **Found while editing, and it is the more interesting result.** The header's
  own first line read "on the rule phase 4 of plan.md settled" — a rotating-plan
  reference of exactly the kind phase 1 gated an hour earlier, sitting inside the
  gate that forbids them, passing. The predicate matches `plan.md phase N`; this
  is `phase N of plan.md`, the same reference with the words the other way round.
  Corrected here as a side effect of rewriting the paragraph. Filed as phase 32
  with the enumeration below, because the population is not one line.

- [x] **Phase 32 — the plan-ref gate matches one word order, and 11 live refs use
      the other.** Phase 1 shipped a predicate for `plan.md phase N` in three
      spellings (plain, backticked, plural) and case-insensitively. It does not
      match the **reversed** form `phase N of plan.md`, which is equally stale
      and equally invisible. Measured outside the frozen archives:

      FRICTION.md:360, :554, :555, :556        (4)
      tests/range_negative_step.ty:6
      tests/bounds_elision.ty:11
      tests/reject/for3_empty_clause.ty:14
      bench/guard.sh:45
      server/README.md:45
      tools/prunner/main.ty:21
      docs/internals/int64-migration-audit.md:3
      src/tychoc.c:11379

  - Every one names a phase number against a `plan.md` that has since rotated, so
    every one resolves against the wrong document today — the exact defect phase 1
    measured at 172 refs and fixed for one word order.
  - **Not a five-minute fix.** Phase 1 established that attributing each ref to
    the plan it meant needs `git blame` on the citing line plus verification that
    the cited phase is one that document declares; it found 38 false refs where 3
    were predicted, and its own predecessor's sweep had missed 12 through
    case-sensitivity. Assume the same care is required.
  - Two of the listed sites are `tests/` fixtures and one is `src/tychoc.c`, so
    the repair touches source comments as well as prose.
  - Cost: an hour, most of it attribution rather than editing. Extend the
    predicate first, then let it name the population, then repair — the order
    phase 1 used.

### Phase 32 evidence — 2026-07-31

#### Part 1 — the re-derived population is 12, and the phase entry's own list was 12

The entry's title says 11; the refs it enumerates are 12. Re-derived
independently before the predicate was written, over `git ls-files` minus
`plan.md`, `compiler/tychoc0.ty` and the frozen `docs/internals/plan-*-DONE.md`
set, matching whole-file text so a wrapped ref could not hide:

```
$ python3 - <<'PY'   # SEP = r'[\s>#*/-]*'
  re.compile(r'\bphases?\s+\d+' + SEP + r'(?:of|in|from|within)' + SEP +
             r'`?plan\.md`?', re.I)
PY
FRICTION.md:360  'Phase 7 of `plan.md`'          tests/bounds_elision.ty:11
FRICTION.md:554  'Phase 6 of `plan.md`'          tests/range_negative_step.ty:6
FRICTION.md:555  'Phase 7 of `plan.md`'          tests/reject/for3_empty_clause.ty:14
FRICTION.md:556  'Phase 7 of `plan.md`'          bench/guard.sh:45
server/README.md:45  'phase 15 of `plan.md`'     tools/prunner/main.ty:21
src/tychoc.c:11379   'Phase 4 in plan.md'        docs/internals/int64-migration-audit.md:3
```

**Exactly the 12 the entry lists — no more, no fewer.** Four candidate patterns
were measured as deltas over that baseline so the choice of predicate was made
on counts rather than taste, and the three the entry asked about are all **zero**
in this tree: "of the plan" / "of the live plan" with no filename (0), a phase
named without the word by bare number, `#`, "step" or "item" (0), and a
punctuation-tolerant separator (0 new). Requiring a connecting word and making
it optional both found **the same 12**, which is why the shipped pattern takes
the looser form for free.

**One spelling the survey did find and this phase did NOT ship:** a possessive
joining the two ("<the live plan>'s phase N"). It is a third spelling, not the
second word order, and it is filed as phase 33 below with its measured
population rather than absorbed here.

#### Part 2 — attribution: 12/12, and two that `git blame` alone gets WRONG

Phase 1's method, reused: the commit that *adds* `plan-X-DONE.md` is the instant
X stopped being live, so the twelve windows tile with no gap; `git blame -C -M`
on the citing line lands in exactly one; the cited number must be a phase the
mapped document declares. **12/12 mapped to a window and 12/12 cited a declared
phase.**

Corroboration was then run against the declared phase's *title and evidence*,
not just its existence — and this is where two refs came apart:

| ref | blame window | corroborated attribution |
|---|---|---|
| `FRICTION.md:360` | loops-cleanup (phase 7 = "delete the counting form and the `range` builtin") | **friction phase 7** — the libpng/`corelib/test/image` item, verbatim in friction phase 7's own evidence |
| `FRICTION.md:555` | signals (line last touched by batch C, the sweep that rewrote *other* refs on it) | **friction phase 7** — the `frontparity` reach item, in friction phase 7's evidence |

`git blame` gives the last commit to touch a LINE, not the commit that wrote the
CITATION. For both of these a later, unrelated edit re-dated the line, and the
window it produced offers a phase 7 that is demonstrably about different work.
Content decided; the window was overruled with the reason written down. **This is
the "confidently wrong plan name is worse than an ambiguous one" hazard, met in
practice** — a blame-only pass would have shipped two wrong document names.

The other ten corroborated on the first pass, blame subject agreeing with the
cited phase's title:

```
tests/reject/for3_empty_clause.ty:14  -> plan-loops-cleanup phase 4  "three-clause `for` and bare `for:`"
tests/bounds_elision.ty:11            -> plan-loops-cleanup phase 6  "rewrite all 549 `range()` sites"
tests/range_negative_step.ty:6        -> plan-loops-cleanup phase 6   (same rewrite, descending sites)
bench/guard.sh:45                     -> plan-loops-cleanup phase 6   (the 223 sites that lost elision)
FRICTION.md:554                       -> plan-friction phase 6       "`core:cli` and `args()`"
FRICTION.md:556                       -> plan-friction phase 7       "`bytes` gets operators"
src/tychoc.c:11379                    -> plan-front-door phase 4     "emitted C is warning-clean"
docs/internals/int64-migration-audit.md:3 -> plan-int64 phase 1      "audit every `long` site"
tools/prunner/main.ty:21              -> plan-prunner phase 4        "does it replace `tests/run.sh`?"
server/README.md:45                   -> plan-signals phase 15        (filed by its phase 3, same subject)
```

**Nothing was left unattributed.** Had one resisted, it would have been left with
the reason stated rather than given a likely-looking plan name.

#### Part 3 — the predicate, and the line-count discipline the repair needed

`PLANREF_REV` in `scripts/check_citations.py` mirrors `PLANREF`: same optional
backticks, same `re.I`, same separator class (so it survives the same hard wrap
onto a comment-led continuation line), plus the two things the direction forces —
an OPTIONAL connecting word, and a number list, because in the plural spelling
the number adjacent to the filename is not the one the word introduces. Matches
from the two orders are merged by position and an overlapping one dropped, so a
sentence satisfying both is one failure, not two.

`scripts/check_citations.py` stays subject to its own rule: the new docstring
section describes the reversed shape without spelling it, and the failure message
was already order-neutral.

**The gate named the population before any ref was touched** — the ordering phase
1 proved: 12 STALE lines, byte-identical to the re-derivation above, plus **3
collateral failures the phase caused itself.** Inserting the docstring section
moved `ARCHIVED` from `scripts/check_citations.py:435` to `:474`, breaking three
anchored refs (`docs/internals/plan-loops-cleanup-DONE.md:2784` and `:3639`,
`scripts/docs_fences.sh:21`). Repointed 435 -> 474. The two in a frozen archive
were repaired deliberately: an ANCHORED ref is not exempt there, which is the
"mostly" phase 31 restored to the header.

**Every repair is a comment, and every file kept its exact line count.** The
second half is load-bearing and was enforced mechanically: `src/tychoc.c` and the
`tests/` fixtures are cited by line from elsewhere in the tree, so a +1 shift
would have traded this repair for a fresh crop of stale citations. Each block was
re-flowed to the same number of lines at the narrowest width that fits, with the
write asserting the file's total line count was unchanged (`src/tychoc.c` 12775,
`tests/bounds_elision.ty` 54, `tests/range_negative_step.ty` 23,
`tests/reject/for3_empty_clause.ty` 19, `tools/prunner/main.ty` 539,
`bench/guard.sh` 75, `FRICTION.md` 790,
`docs/internals/int64-migration-audit.md` 281).

**No line of code was touched**, checked rather than asserted:

```
$ git diff -U0 -- src/tychoc.c bench/guard.sh tests/ tools/prunner/main.ty \
  | grep -E '^[+-]' | grep -vE '^(\+\+\+|---)' | grep -vE '^[+-]\s*(#|\*|/\*)'
(no output -- every changed line is a comment)
```

`CLAUDE.md`'s description of the gate's coverage was one clause out of date the
moment the predicate widened, so it now says "both word orders", names the
possessive as the known gap, and corrects "four surveys" to five.

#### Planted-violation proof — both directions, BOTH ORDERS

12 spellings appended to `server/README.md`, a file outside the allowed set.
5 forward (phase 1's, proving no regression) and 7 reversed:

```
$ python3 scripts/check_citations.py
STALE  server/README.md:293  'plan.md phase 3'
STALE  server/README.md:294  '`plan.md` phase 3'
STALE  server/README.md:295  '`plan.md` phases 1'
STALE  server/README.md:296  'plan.md Phase 3'
STALE  server/README.md:297  '`plan.md`\n   phase 3'
STALE  server/README.md:301  'phase 3 of plan.md'
STALE  server/README.md:302  'phase 3 of `plan.md`'
STALE  server/README.md:303  'Phase 3 in plan.md'
STALE  server/README.md:304  'phases 1 and 2 of plan.md'
STALE  server/README.md:305  'phase 3 within `plan.md`'
STALE  server/README.md:306  'phase 3 of\n   plan.md'
STALE  server/README.md:308  'phase 3 plan.md'
citation check: FAILED (12 stale citation(s) above)
EXIT=1
```

All 12 caught, each naming the archived form to write instead; nothing else
reddened, so the merge does not double-report. Plant removed:

```
$ python3 scripts/check_citations.py
citation check: ok (201 anchored contain the token they name and each names one
line, 2802 bare in bounds (1800 frozen record, 17 live-plan evidence, 196 exempt
`> Provenance:` range, 789 reachable prose), 273 source->doc citations resolve,
247 source->source in bounds, 16 source->source anchored)
EXIT=0

$ sh scripts/check_links.sh
link check: ok (137 markdown files, no dead relative links)
EXIT=0
```

`source->doc` rose 248 -> 273: the repairs turned 12 unchecked plan references
into citations the second direction now checks for existence, which is the
secondary win — the repaired form is not merely correct, it is *gated*.

- [ ] **Phase 33 — the possessive spelling, the third one the gate cannot see.**
      Phase 32 closed the second word order and measured, but deliberately did
      not ship, a third spelling: a possessive joining the filename to the phase
      ("<the live plan>'s phase N"). It is not a word order, its refs need their
      own attribution, and one of its shapes names no file at all. Measured at
      phase 32's tree, outside the exempt files — **12 refs in four shapes**:

      possessive naming the file (7)
        corelib/signal/signal.ty:66, corelib/signal/signal_shim.c:34,
        corelib/test/signal/main.ty:23, tools/prunner/main.ty:108,
        tests/reject/dotlt_sequential.ty:5, FRICTION.md:334, :462
      possessive naming NO file — "the plan's phase N" (3)
        FRICTION.md:197, :675, :684
      reversed with words in between (1)
        tests/bounds_noelide.ty:9  ("phase 27's evidence in <the live plan>")
      the filename without its extension (1)
        docs/internals/frontend-restriction-audit-2026-07-25.md:265

  - **The 3 that name no file need a decision this phase had no measurement to
    make.** "The plan's phase N" may legitimately mean an archived plan the
    surrounding sentence already named, so a pattern for it keys on a common
    English word rather than on a path, and could redden prose that is correct.
    Count the false-positive rate before shipping that half; the other 9 are the
    same defect as phase 32's and are unambiguous.
  - Note `FRICTION.md:334` and `:462` put words between the two parts
    ("<the live plan>'s carried-forward phase 7"), so the separator class both
    current patterns use cannot reach them — this is a pattern *shape* change,
    not another alternation.
  - Same order as phases 1 and 32: extend the predicate, let the gate name the
    population, then attribute with `git blame -C -M` **corroborated against the
    mapped phase's own evidence** — phase 32 found 2 of 12 where blame alone
    named the wrong document, because a later commit had re-dated the line.
  - Verify: `python3 scripts/check_citations.py`, the planted-violation proof
    both directions, `sh scripts/check_links.sh`.
