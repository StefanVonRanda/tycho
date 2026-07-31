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

- [x] **Phase 30 — a citation to a definition should not be a line number**
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

- [x] **Phase 28 — the 6 refs phase 1 refused to repair, and why refusing was right**
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

- [x] **Phase 29 — decide what a before/after record block is, so nobody "repairs" one**
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

- [x] **Phase 33 — the possessive spelling, the third one the gate cannot see.**
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

### Phases 28–30 evidence — 2026-07-31

Three decisions, one shipped mechanism. Every count below was produced with the
gate's own grammar (`scripts/check_citations.py@CITE` / `@SRCCITE` imported, not
re-implemented), so the populations are the ones the gate actually sees.

#### Phase 28 — the annotation form, and why annotation was the right verb

**Decided: these are claims about superseded behaviour, not citations to repair.**
Verified before deciding — `server/main.ty:490-496` today is inside an
`io.read_bytes` / status-200 comment with no `running` anywhere near it, and
`server/main.ty:520` opens the comment block headed "WHY THE ERR ARM IS NOT
`running = false`". Batch A deleted the construct; there is no line to relocate
to, and the nearest true statement asserts the opposite rule.

**The form, applied to all six** (`docs/internals/plan-signals-DONE.md` lines 34,
144, 166, 379, 437, 880):

```
`server/main.ty:493-494` [SUPERSEDED: construct deleted in batch A — do not repoint]
```

plus one appended note at the foot of that file defining the tag, naming what
deleted the construct, and pointing at the replacement reasoning by its **grep-
able heading** rather than by a number. Chosen so that:

- **It is line-neutral.** Six same-line insertions and one appended section;
  nothing above moves. Confirmed no ref anywhere in the tree cites
  `docs/internals/plan-signals-DONE.md` by line, so nothing could have broken.
- **A sweep cannot miss it.** The tag sits adjacent to the number a sweeper
  greps for, not in a header they would never reach.
- **The gate does not parse it as a citation.** `[SUPERSEDED: …]` is not a
  backticked `path:N` span and not a `](…)` link. Proof: the bare/anchored
  counts are unchanged by the six tags, and `check_links.sh` stays at 137 files.

**Correction made mid-phase, recorded because the first draft was wrong.** The
note originally claimed the six refs were "bounds-checked". They are not checked
**at all** — `server/` is absent from `SRC_PREFIX`, so a Markdown citation into
the server is skipped before existence, bounds or anchors are considered. The
note was rewritten to say so, and the finding is filed as phase 34 below.

#### Phase 29 — no marker, and the number is the reason

**Counted first, as the entry demanded — and the entry's own estimate was off by
two orders of magnitude.** It assumed "three blocks". Over the twelve archived
plans plus the live one, matching the two structural shapes that carry
record-meaning (two refs joined by an arrow; a table row with ≥2 ref-bearing
cells):

```
$ python3 - <<'PY'   # REF = the gate's CITE span; ARROW = -> | → | => | ➔
  for each `git ls-files 'docs/internals/plan-*-DONE.md' plan.md`:
      lines with >= 2 REF matches, that are a table row with >= 2 ref cells,
      or that carry an ARROW
PY
SHAPE 1 -- repair-log arrow lines:            some
SHAPE 2 -- before/after table rows:           the rest
TOTAL record lines: 271, across 9 of 13 archived-plus-live plan files
  120  docs/internals/plan-postfreeze-rawstring-DONE.md
   95  docs/internals/plan-front-door-DONE.md
   33  docs/internals/plan-signals-DONE.md
   10  docs/internals/plan-loops-cleanup-DONE.md
    5  docs/internals/plan-friction-DONE.md
    3  plan.md
    2  docs/internals/plan-int64-DONE.md
    2  docs/internals/plan-webserver-gate-DONE.md
    1  docs/internals/plan-array-arith-DONE.md
```

**Decision: no marker is inserted.** 271 lines, **268 of them frozen**. Tagging
them is precisely the hand sweep this repo has declined three times, and it would
edit 8 archives in order to say "do not edit these archives". The live exposure
is **3 lines**, and those rotate into an archive at the next plan boundary.

**What ships instead** (`CLAUDE.md`, "A record line is not a citation —
recognise it by shape"): the recognition phase 1 performed by reading each region
is written down as the two shapes above, so it is a stated predicate rather than
one careful agent's judgement. Phase 2's no-incentive defence stands as the
load-bearing guarantee — nothing counts, budgets or ratchets these lines, so no
pressure to sweep is ever created. The written rule is for the human who goes
looking anyway.

**Why this does not contradict phase 28.** The two shapes above are
**self-identifying**: a reader seeing `before | after | delta` knows they hold
data. Phase 28's six refs are the opposite — a number that is a *pointer*,
embedded in ordinary running prose ("`…:493-494` sets `running = false`"), where
nothing about the shape warns anyone. **A tag is worth it exactly when the shape
does not already say "record".** That is the rule both decisions come from.

#### Phase 30 — counted at 37, so it shipped

**The count, with the command that produced it:**

```
$ python3 <scratch>/count30.py     # imports check_citations, reuses CITE/SRCCITE
                                   # and its path-resolution; "definition" = a
                                   # single-line ref whose cited line BINDS the
                                   # token (fn/static/#define/assignment head)
ANCHORED REFS NAMING A DEFINITION (candidates for a `path@SYMBOL` form)
markdown -> src : 36 of 202 anchored
source -> source: 1 of 16 anchored
TOTAL definition-shaped anchored refs: 37 of 218
```

Split by whether they can be moved: **22 live, 15 frozen.** So the answer to
"a mechanism for three refs?" is no — it is 17% of every anchored citation in the
tree, and the test the entry set is passed.

**Shipped:** `` `path@SYMBOL` `` with no number, in both Markdown (`SYMCITE`,
backticked) and source (`SYMCITE_SRC`, filtered against the tracked set). The
absence of the colon is what keeps it disjoint from `CITE`. Checked by locating
the token in the named file. Uniqueness is **not** required and the docstring
says why at length: a symbol occurs at its definition and every call site, so the
ambiguous-anchor rule does not transpose. The weakness is stated in the docstring
rather than hidden — it proves the symbol is still spelled that way, not that the
definition survives.

**Three refs migrated, nineteen deliberately not.** The three are this rule's own
motivating case:

```
docs/internals/plan-loops-cleanup-DONE.md:2784, :3639   (frozen)
scripts/docs_fences.sh:21                                (live)
```

They had to move: this phase's own docstring section pushed `ARCHIVED` from line
474 to 539, which would have staled all three a **fourth** time in the very
commit explaining why they should not need repairing. Editing the two frozen ones
is settled ground — phase 31 established that an ANCHORED ref in an archive was
never exempt — with the extra fact that `git log` shows both were already
mechanically renumbered by three post-freeze phases (`dd3c019`, `de1fcc1`,
`2ed2cbf`), so what was repaired was residue, not the observation. Removing the
number ends the cycle instead of extending it. The other 19 live refs are **not**
converted: a correct citation rewritten by hand is the drift-inducing pass this
repo declines.

**Line-neutrality result, stated because the brief warned about it:** `ARCHIVED`
moved 65 lines and **zero repairs were needed**, because the only three refs that
named it no longer carry a number. That is the mechanism demonstrating itself.

**Planted-violation proof, both directions.** Renaming the symbol in its own
file, then restoring:

```
$ python3 -c "...replace('shutdown_requested','shutdown_asked')..."  # corelib/signal/signal.ty
$ python3 scripts/check_citations.py
STALE  scripts/check_citations.py:436  `corelib/signal/signal.ty@shutdown_requested`
       -> 'shutdown_requested' does not appear anywhere in corelib/signal/signal.ty.
       A symbol citation survives insertions but not a RENAME or a DELETION,
       which is the whole of what it promises.
citation check: FAILED (3 stale citation(s) above)

$ git checkout -- corelib/signal/signal.ty
$ python3 scripts/check_citations.py
citation check: ok (...)
```

The other two failures in that run are the **old** line-anchored refs to the same
symbol (`FRICTION.md:738`, `server/README.md:43`) — which is the contrast the
form is for: both catch a rename, only the symbol form survives an insertion.

#### One ref lost false coverage, and that is an improvement

Measured, not assumed: dumping the bare population before and after showed the
bare total move 2807 → 2806, a single line —

```
- docs/internals/plan-loops-cleanup-DONE.md:2825  `:43` -> scripts/check_citations.py
```

That `:43` reads "(`docs/spec/12-aggregates.md` vs `docs/spec/15-program.md`,
both at `:43`)". It always meant **line 43 of those two spec documents**; it was
inheriting `scripts/check_citations.py` from the anchored ref 41 lines above,
and passed only because that file is longer than 43 lines. Removing the number
from the ref above stopped the mis-inheritance, so the ref is now correctly
skipped instead of wrongly checked. Left as-is: it is bare, in a frozen record,
and writing a real path there would make it a live claim about today's spec.

#### Gates run, and the ones deliberately not run

```
$ python3 scripts/check_citations.py
citation check: ok (199 anchored contain the token they name and each names one line,
2806 bare in bounds (1799 frozen record, 22 live-plan evidence, 196 exempt
`> Provenance:` range, 789 reachable prose), 273 source->doc citations resolve,
247 source->source in bounds, 15 source->source anchored, 5 `path@SYMBOL`
definition refs name a symbol still in their file)

$ sh scripts/check_links.sh
link check: ok (137 markdown files, no dead relative links)
```

Baseline at `2ed2cbf` for comparison: 201 anchored, 2807 bare (1800 frozen), 16
source→source anchored, no symbol clause. The deltas are exactly the three
migrated refs (−2 markdown anchored, −1 source anchored, +5 symbol including the
two new documentation examples) and the one false-coverage bare ref above.

**`make test`, `make test-fast`, `make ci` and `sh scripts/spec_check.sh` were
not run.** Every edit is Markdown, a Python doc gate, or a `#` comment line in
`scripts/docs_fences.sh`. No compiled artifact and no fixture is reachable from
this diff; `corelib/signal/signal.ty` was modified only inside the planted-
violation proof and restored with `git checkout` before anything else ran.

- [x] **Phase 34 — 751 Markdown citations are outside `SRC_PREFIX` and checked by
      nothing.** Found while writing phase 28's note, which first claimed the six
      superseded refs were bounds-checked. They are not checked at all.
      `SRC_PREFIX` is `("docs/", "src/", "compiler/", "runtime/", "corelib/",
      "tests/", "scripts/", "tools/", "examples/")`. A Markdown `path:N` naming
      anything else hits the fail-open skip: no existence, no bounds, no anchor.
      Measured over `git ls-files '*.md'` with the gate's own `CITE`:

      server/      179        fuzz/         31        bench/     24
      FRICTION.md  178        plan.md       20        (others)
      total markdown refs skipped for being outside SRC_PREFIX: 751

  - **This is the same bug as phase 44's**, which added `docs/` to `SRC_PREFIX`
    and immediately reddened 77 refs — 25 naming a file that no longer existed.
    `server/` is already in `DOC_SCAN_PREFIX`, so the gate scans the server for
    citations *out* while ignoring every citation *in*.
  - **Do not widen the prefix without counting the blast radius first**, in the
    order phase 44 used: add one tree, measure the reds, attribute them, then
    decide. `FRICTION.md` and `plan.md` are single files rather than trees and
    may want the `Makefile` treatment in `DOC_SCAN_PREFIX` instead.
  - Expect real failures, not noise: `server/main.ty` alone has been rewritten
    twice since most of those 179 refs were written, and phase 28 has just shown
    that six of them describe a construct that no longer exists.
  - Verify: `python3 scripts/check_citations.py`, the planted-violation proof,
    `sh scripts/check_links.sh`.

  - **EVIDENCE (2026-07-31).** Gates green: `python3 scripts/check_citations.py`
    and `sh scripts/check_links.sh` (137 markdown files, no dead relative links).
    `sh scripts/spec_check.sh` not run — nothing under `docs/spec/` was touched.

  - **1. THE ENUMERATION.** Re-derived with the gate's own `CITE` and its own
    paragraph / absolute-path / inheritance handling over `git ls-files '*.md'`.
    The phase entry's 751 was measured earlier and counted only part of it: the
    true figure at this commit is **1023** refs resolving to a path outside
    `SRC_PREFIX`. Every tracked top-level entry, with the refs each attracted:

    | top-level | tracked files | in `SRC_PREFIX`? | in `DOC_SCAN_PREFIX`? | md refs skipped |
    |---|---|---|---|---|
    | `docs/` | 94 | yes | no | 0 |
    | `tests/` | 932 | yes | yes | 0 |
    | `examples/` | 156 | yes | yes | 0 |
    | `corelib/` | 134 | yes | yes | 0 |
    | `compiler/` | 56 | yes | yes | 0 |
    | `scripts/` | 14 | yes | yes | 0 |
    | `tools/` | 9 | yes | yes | 0 |
    | `src/` | 1 | yes | yes | 0 |
    | `runtime/` | 1 | yes | yes | 0 |
    | `server/` | 14 | **no** | yes | **231** |
    | `bench/` | 165 | **no** | yes | **25** |
    | `fuzz/` | 14 | **no** | yes | **32** |
    | `editors/` | 20 | **no** | yes | **12** |
    | `.githooks/` | 1 | **no** | yes | 0 |
    | `.github/` | 4 | **no** | no | 0 |
    | `branding/` | 4 | **no** | no | 0 |
    | `Makefile` | 1 | **no** | yes | 0 |
    | `FRICTION.md` | 1 | **no** | no | **204** |
    | `plan.md` | 1 | **no** | no | **25** |
    | `README.md` | 1 | **no** | no | **12** |
    | `ROADMAP.md` | 1 | **no** | no | **5** |
    | `CLAUDE.md` | 1 | **no** | no | 1 |
    | `CONTRIBUTING.md` `LICENSE` `RELEASE_NOTES.md` `SECURITY.md` `.gitignore` | 5 | **no** | no | 0 |

    The phase entry named four trees; **nine** places have a non-zero population,
    and the remaining **475** skipped refs name no top-level entry at all (class
    D below). Two of the entry's four counts were low (`server/` 179 → 231,
    `FRICTION.md` 178 → 204) and two were near (`fuzz/` 31 → 32, `bench/`
    24 → 25).

  - **2. WHAT WENT IN, AND WHAT DID NOT.** Two groups in, three out, each with
    its reason; the full argument is in the `SRC_PREFIX WAS THE HOLE` section of
    `scripts/check_citations.py`.
    - **In, source trees:** `server/`, `bench/`, `fuzz/`, `editors/`,
      `.githooks/` — every tree `DOC_SCAN_PREFIX` already trusts as source. The
      asymmetry *was* the bug: the gate read `server/` for citations **out**
      while ignoring all 231 citations **in**. `.githooks/` has 0 refs and is in
      anyway, because the set is the argument, not the count.
    - **In, top-level documents:** `FRICTION.md` (204), `README.md` (12),
      `ROADMAP.md` (5), `CLAUDE.md` (1), plus `CONTRIBUTING.md`,
      `RELEASE_NOTES.md` and `SECURITY.md` for the same reason. These take the
      `Makefile` treatment the phase entry suggested — a prefix entry that is a
      whole filename. `docs/` was already in, so doc→doc was already policy;
      these are the documents that policy had missed for living at the root.
    - **Out — `plan.md` (25 refs, 11 of them already out of bounds).** It
      rotates: when a plan is archived the next starts at line 1, so a line ref
      into it from an archived record names a document that no longer exists in
      any form. The number is not repairable — what it pointed at is gone — and
      it would go stale again every phase, because the live plan grows every
      phase. That is a permanent red nobody can clear. Filed as phase 37.
    - **Out — `.github/`, `branding/`.** 0 refs, and absent from
      `DOC_SCAN_PREFIX` too, so adding them would make this gate treat as source
      something the other direction does not: an inconsistency with nothing
      behind it.
    - **Out — `Makefile`, `LICENSE`, every extension-less file.** *Unreachable,
      not excluded*: `CITE`'s path group requires a dot and an extension, so the
      Markdown pass cannot produce such a path whatever the list says. Adding
      one would have been dead code. (The source→source pass names `Makefile`
      explicitly for exactly this reason.)
    - **Carried with it, and required by it:** the frozen doc→doc skip and the
      same-line inheritance rule both keyed on the target starting with `docs/`.
      Both arguments were always about the target being a *small document*, not
      about where it lives, and `FRICTION.md` at 790 lines has that property.
      Widening `SRC_PREFIX` without widening them would have demanded edits to
      three frozen records for the bare-ref class every rule in the file refuses
      to touch. **No coverage is lost:** 751 frozen refs now land in that skip,
      524 were in it already, and all 227 of the rest name paths that were
      outside `SRC_PREFIX` until the same change.

  - **3. THE BLAST RADIUS, MEASURED ONE TREE AT A TIME** (the order phase 44 of
    `docs/internals/plan-loops-cleanup-DONE.md` used), baseline 0 failures:

        server/ +7   FRICTION.md +2   plan.md +11   README.md +1
        fuzz/ +0     bench/ +0        editors/ +0   .githooks/ +0
        .github/ +0  branding/ +0     ROADMAP.md +0 CLAUDE.md +0
        Makefile +0  CONTRIBUTING.md / RELEASE_NOTES.md / SECURITY.md +0
        all candidates together: +21

    With `plan.md` dropped and the two rules generalised to "any document", the
    +2, +11 and +1 vanish — every one of them was a bare continuation ref in a
    frozen archive, the class the frozen skip exists for. **Final: 7 failures.**

  - **4. THE CLASSIFICATION.** Four classes, not three. Counts per class:
    - **A — a ref that DRIFTED (6).** All six anchored into `server/main.ty`,
      which has been rewritten twice under them. Repaired by re-deriving the
      construct, not by shifting the number:
      - `server/README.md:153` — `server/main.ty:302` →
        `server/main.ty:313@is_dir` (the `match io.is_dir(fsp)` in `resolve()`)
      - `server/README.md:170` — `server/main.ty:369` →
        `server/main.ty:380@peer_addr` (the once-per-connection read in `serve_conn`)
      - `server/README.md:188` — `server/main.ty:635` →
        `server/main.ty:742@on_shutdown` (the `signal.on_shutdown(srv)` call)
      - `FRICTION.md:706` — `server/main.ty:635` →
        `server/main.ty:742@on_shutdown` (same call, same repair)
      - `docs/internals/plan-signals-DONE.md:1795` and
        `docs/internals/plan-signals-DONE.md:1796` — **anchor dropped, number
        kept.** These two are cells of a `| comment | as-found | re-derived |
        drift |` table: four ref-bearing cells and a delta column, which is
        `CLAUDE.md`'s **before/after table row** shape exactly, so every number
        in them is *data*. Repointing 588 would have falsified the `+75` beside
        it. The four sibling rows in that table are already bare, so dropping
        the anchors makes it consistent and is the docstring's own third option
        ("drop the anchor and leave the range bare"). Phase 31 settled that an
        anchored ref in an archive is not exempt; it did not settle what to do
        when the archive is a record table. Tension filed as phase 39.
    - **B — a ref that was ALWAYS WRONG (0).** None found.
    - **C — a bare continuation ref that INHERITED the wrong path (0
      surviving).** All three the naive widening produced were in frozen
      archives and are covered by the generalised skip. **This is the phase's
      most useful negative result**: the `docs/` widening found 52 of this class
      and 25 dead paths; this one found *one* mis-inheritance and *zero* dead
      paths across 522 newly-reachable refs. The difference is that `docs/` was
      widened over documents that had been **renamed** under their citations,
      and these trees have not been. So the widening buys almost no repair work
      today and the whole population a gate tomorrow.
    - **D — NOT A CITATION AT ALL (1 exposed, ~487 latent).** The one surviving
      red, in `docs/internals/plan-friction-DONE.md`, was a **TCP port number**
      written in backticks after a colon, in a sentence about that port being
      occupied; the paragraph had named `server/main.ty` earlier, so the grammar
      bound it there and read 8080 as a line. Repaired by moving the word "port"
      outside the backticks — this changes no recorded number and falsifies no
      observation, which is what makes it a legitimate edit to a frozen record.
      17 more of the same shape sit behind a dotted host that no prefix covers.
      Filed as phase 35. The larger latent set is the 470 refs over 40 names
      carrying **no directory at all** — filed as phase 36.

  - **5. BEFORE / AFTER, same tree, gate at `46cbb35` vs. now.**

        BEFORE  199 anchored, 2806 bare (1799 frozen, 22 plan, 196 prov, 789 prose)
                524 doc->doc skipped as frozen archive
        AFTER   223 anchored, 3076 bare (2019 frozen, 44 plan, 196 prov, 817 prose)
                751 doc->doc skipped as frozen archive

    **+294 refs are now actually checked** (24 anchored, 270 bare), and 228 more
    frozen doc→doc refs are declared-and-skipped rather than silently skipped.
    Refs skipped for being outside `SRC_PREFIX` fell **1023 → 501**. The
    anchor-strength figures moved with the new anchors (33/77 → 36/82) and the
    docstring's copy was updated; `CLAUDE.md`'s copy was not — filed as phase 38.

  - **6. THE PLANTED-VIOLATION PROOF.** Four refs appended to `FRICTION.md`, one
    per newly-covered kind — a new tree past EOF, a new tree with a wrong
    anchor, a new tree naming a dead file, and a top-level document past EOF.
    **Transcribed with the backticks removed**, because reproducing them intact
    would make this evidence block four live failing citations:

        STALE  FRICTION.md:791  server/main.ty:99999 -> server/main.ty has 754 lines: OUT OF BOUNDS
        STALE  FRICTION.md:793  fuzz/run.py:1@no_such_token_here -> lines 1-1 of fuzz/run.py do NOT contain 'no_such_token_here' (token absent from the whole file)
        STALE  FRICTION.md:795  bench/no_such_file.sh:3 -> bench/no_such_file.sh: NO SUCH FILE
        STALE  FRICTION.md:797  FRICTION.md:99999 -> FRICTION.md has 798 lines: OUT OF BOUNDS
        citation check: FAILED (4 stale citation(s) above)
        --- exit=1

    **The control that makes it a proof:** the gate as it stood at `46cbb35`,
    run against that same planted tree, printed its ordinary green line and
    `exit 0`. All four plants were invisible to it. Plants reverted; green again.

- [ ] **Phase 35 — a backticked port number is read as a citation.** Found by
      phase 34: widening `SRC_PREFIX` to `server/` turned one such span in
      `docs/internals/plan-friction-DONE.md` into a hard failure, because the
      paragraph had named a source file and the grammar bound the port to it as
      a line number. It was repaired by rewording, but the class is general and
      rewording does not scale to a frozen record that must not be reworded.
  - The shape is narrow and countable: a colon, then four or five digits, inside
    a backtick span, in prose about a port or an address. 17 more sit in the
    tree behind a dotted host, invisible today only because no prefix covers a
    hostname — and a hostname that is not a path is the same false positive
    wearing a different hat.
  - The danger is the silent one, not the loud one: a port bound to a paragraph
    naming a 13k-line compiler is **in bounds**, so it passes, and it is then
    counted as a checked citation on the green line. Count that population
    before choosing between "digits after a colon in port-shaped prose are not a
    citation" and "a bare continuation ref must not follow the word port".
  - Verify: `python3 scripts/check_citations.py`, the planted-violation proof,
    `sh scripts/check_links.sh`.

- [x] **Phase 36 — 470 refs name a bare basename with no directory.** After
      phase 34 widened `SRC_PREFIX`, 501 Markdown refs are still skipped, and
      **470 of them over 40 distinct names** carry no directory at all:
      `05-generics.md` (67), `03-types.md` (65), `tychoc0.ty` (51),
      `02-grammar.md` (33), `frontparity.sh` (31), `fixpoint.sh` (24) and 34
      more names behind them. No prefix list can ever resolve these, because the
      author never said which directory they meant.
  - Most are guessable — `05-generics.md` is almost certainly the file of that
    name under `docs/spec/` — and *guessing is exactly what RULE 7 forbids*. So
    the choice looks like one between resolving 470 refs by hand (the sweep this
    repo has declined three times in other forms) and failing them closed, which
    would redden hundreds of frozen archives at once.
  - A third option worth costing first: resolve only where the basename matches
    **exactly one** tracked file, fail where it matches several, skip where it
    matches none. Count all three buckets before deciding — a basename that
    resolves uniquely is not a guess.
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`.

- [x] **Phase 37 — 25 line references into the rotating live plan, 11 already
      out of bounds.** Phase 34 measured these and deliberately left the live
      plan out of `SRC_PREFIX`: it is renumbered from line 1 every time a plan
      is archived, so a line reference into it from an archived record names a
      document that no longer exists in any form. The number cannot be repaired,
      and it would go stale again every phase, because the live plan grows every
      phase. Bounds-checking it would be a permanent unclearable red.
  - But leaving it out means 25 refs are checked by nothing, which is the defect
    phase 34 existed to close, declined in one place for a stated reason. That
    is a decision to make on purpose rather than a hole to leave open.
  - The fourth direction already forbids the *pointer* form outright — a phase
    reference into the rotating plan is a hard failure naming the archived
    document to write instead. The obvious shape is to treat the line form the
    same way: such a reference from any file other than the live plan is a
    failure telling the author to name the archived document. Count by citer
    first — 24 of the 25 are in frozen archives, where a failure cannot be
    cleared, so the rule likely needs the same `ARCHIVED` exemption and would
    then police exactly one live ref. Decide whether that is worth a rule.
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`.

- [ ] **Phase 38 — the anchor-strength figures in `CLAUDE.md` are stale, and one
      pair in the gate's docstring was stale before phase 34 touched it.**
      `CLAUDE.md` states 32 anchors weak within ±25 lines and 76 recurring
      anywhere in the file; the tree now says 36 and 82, because phase 34
      brought new anchors into scope. Phase 34 updated the docstring's copy and
      left `CLAUDE.md`'s, being out of its scope.
  - Separately, the docstring's source→source pair "8 and 10 of 16" was
    **already wrong at `46cbb35`** — the gate printed 7 and 9 of 15 there, so it
    did not drift under phase 34 and was not phase 34's to repair. Two files now
    disagree with the gate and with each other about the same measurement.
  - The real question is not the four numbers: it is that a measured figure is
    duplicated into prose in two files with nothing checking either, while
    `--stats` prints all of them on demand. Consider whether the prose should
    carry the numbers at all, or should name the command that produces them.
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`.

- [ ] **Phase 39 — the anchor check and the record-line rule disagree about a
      frozen before/after table.** Phase 34 hit this directly: two cells of a
      six-row table in `docs/internals/plan-signals-DONE.md` carried anchored
      refs whose tokens had drifted. `CLAUDE.md` says a table row with two or
      more ref-bearing cells and a delta column is a **record**, and that every
      number in it must be left alone — including numbers that are provably
      wrong, because being wrong is what they record. Phase 31 says an anchored
      ref in a frozen archive is **not** exempt from the content check. Both
      rules are settled, and on that table they say opposite things.
  - Phase 34 disposed of it by dropping the two anchors and keeping the numbers,
    which satisfies both readings and matches the four sibling rows. That is a
    sound repair for two cells and not a policy.
  - `CLAUDE.md` also claims "**what the gate does about it: nothing,
    deliberately** — no rule counts, budgets or ratchets record lines, so
    nothing ever creates pressure to sweep one". Phase 34 disproves that as
    written: widening a prefix put gate pressure on a record table without any
    rule being aimed at record lines. Either the claim narrows to "no rule
    *targets* them", or the anchor check learns the shape.
  - Count first, as ever: how many anchored refs across the 271 known record
    lines would the content check fire on today? If the answer is two this is a
    documentation fix; if it is forty it is a rule.
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`.
