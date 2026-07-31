#!/usr/bin/env python3
"""Provenance-citation checker: does `path:N` still point at what it claims?

    python3 scripts/check_citations.py            # check (exit 1 on failure)
    python3 scripts/check_citations.py --stats    # also print the anchored/bare split

WHY THIS EXISTS.  The docs cite implementation lines as `src/tychoc.c:7206-7207`.
Every edit to a cited file shifts a share of those numbers, and the failure is
silent: the cited line still exists and still contains plausible-looking code, so
nothing looks wrong until a human reads it.  docs/internals/plan-front-door-DONE.md
phases 34, 35, 40 and 43 each
repaired a batch, and three of the four found their *replacement* lines were wrong
too.  This gate turns that class from "audit again next time" into green/red.

CITATION SHAPES RECOGNISED (all inside a backtick span; the colon is mandatory,
so a plain `0` or `9223372036854775807` in prose is NOT a citation):

    `src/example.c:7181`               path + single line                -> BARE
    `src/example.c:7206-7207`          path + range                      -> BARE
    `:7206-7207`                       continuation: inherits the last path named
                                       IN THE SAME PARAGRAPH              -> BARE
    `src/example.c:7206-7207@main`     path + range + anchor token    -> ANCHORED
    `:3251@has_prefix`                 anchored continuation          -> ANCHORED

The table names `src/example.c`, which is NOT a file in this tree, ON PURPOSE --
do not "repair" it to a real path.  This file is itself scanned by the
source->source pass below, so a shape example spelled with a real path is a live
citation: this table used to name lines 7206-7207 of `src/tychoc.c` anchored to
the token `main`, and those two lines say `var->raw);` and `for (int b = 0; ...`
-- no `main` anywhere near them.  A grammar of shapes is not a
claim about the compiler, and binding it to real line numbers puts this docstring
into the population every renumbering sweep has to repair.  Genuine citations in
the prose below (`docs/bootstrap.md:106`, `src/tychoc.c:402`) are real and stay
checked; only the shape table is a placeholder.

WHAT IS CHECKED
    BARE      the file exists, and 1 <= N <= M <= (lines in file).
    ANCHORED  the above, PLUS the cited line range must literally contain the
              anchor token.  The token is chosen when the citation is verified,
              so it is the expected content a bare `path:N` cannot carry.

THE ONE PLACE ANCHORING IS MANDATORY: `> Provenance:` BLOCKS (added 2026-07-29)
------------------------------------------------------------------------------
Anchoring is opt-in everywhere except one construct, where it is REQUIRED:

    inside a `> Provenance:` block, a SINGLE-LINE ref MUST be `path:N@token`.

A `> Provenance:` block is the blockquote that opens with `> Provenance:` plus
every `>` line that continues it -- the whole block, not only the first physical
line, because these blocks are hard-wrapped and most of their citations sit on a
continuation line.  Policing only the opening line would make the rule evadable
by pressing Enter.

WHY HERE AND NOWHERE ELSE.  A Provenance line is the spec's claim that a rule is
implemented AT A NAMED SITE.  When it rots it does not merely mislead, it asserts
something false about a mechanism: `01-lexical.md` §3.8 said "`::` is lexed at
`:402`" while `src/tychoc.c:402` had become the opening line of the raw-string
scanner -- in bounds, plausible, and about an unrelated feature.  Bare `:N` is
checked for bounds only, so nothing saw it.  Narrative refs elsewhere in the
prose do not carry that weight and stay opt-in.

WHY RANGES ARE EXEMPT -- DO NOT "FIX" THIS BY REQUIRING THEM TOO.  A range
(`path:N-M`) cites a *region*: a loop, a function body, a table.  It has no
single subject token, so an anchor for it must be invented from one arbitrary
line inside it.  That produces a FALSE anchor -- a token the gate will happily
keep matching while the region around it drifts -- which is strictly worse than
a bare range, because it reads as verified when it is not.  A Provenance block
is therefore normally a MIX: anchored single-line refs plus bare ranges, and the
mix is correct, not an oversight.  The converse is a useful signal: if a
single-line ref has no distinctive token to anchor to, the citation probably
wanted to be a range.

ARCHIVED PLANS ARE MOSTLY EXCLUDED -- read the "mostly", it is load-bearing and
was overstated here until 2026-07-31.  The `docs/internals/plan-*-DONE.md` set is
frozen verification evidence; renumbering a BARE ref there would falsify a
recorded observation, so no rule below asks for that.  An ANCHORED ref is not
exempt and never was: it promised a token sits on that line, and a promise that
has stopped holding misinforms rather than merely dates.  Behaviour unchanged.

WHAT THIS DOES **NOT** CATCH -- stated plainly so the coverage is not read wider
than it is:
  * A BARE citation that drifts onto a different-but-existing line.  That is the
    exact `15-program.md` failure mode (docs/internals/plan-front-door-DONE.md
    phase 43): the lines still held
    plausible C, just for a different rule.  A bare citation carries no expected
    content, so nothing can check it -- only the ANCHORED form catches this.
    Anchoring is OPT-IN and adoption is partial; `--stats` prints the split, and
    the honest reading of a green run is "no anchored citation has drifted, and
    no citation of either kind points outside its file".
  * A WIDE anchored RANGE that drifts while still containing its token; being
    distinctive does not save it (phase 11: an `arrc_sized_b` range nine lines
    off, an `int main(` one eighty-two). Keep a range as tight as its construct.
  * A citation that was wrong the day it was written and anchored to its own
    wrong line (garbage in).
  * A docs claim that is wrong about *behaviour* rather than about a line number.
    Different class entirely; no line-checker can see it.

FAIL-OPEN CASES (deliberate, RULE 7): a bare `:N` whose paragraph names no path
is skipped rather than guessed at -- EXCEPT inside a `> Provenance:` block, see
below -- and a RELATIVE path outside the trees listed in
SRC_PREFIX is ignored (a bare Markdown *link* is check_links.sh's job; `docs/`
joined SRC_PREFIX on 2026-07-30, see the doc->doc section below, and the rest of
the tree joined it on 2026-07-31 -- read "SRC_PREFIX WAS THE HOLE" before
treating this skip as a small one).  It is STILL a large skip -- 501 refs, of
which 470 over 40 distinct names carry NO DIRECTORY at all (`05-generics.md`,
`tychoc0.ty`, `frontparity.sh`), which no prefix list can resolve because the
author never said which directory they meant.  Two of those 40 names are not
files: a dotted host before a port reads as a path with an extension.

AN ABSOLUTE PATH IS A FAILURE, NOT A SKIP (added 2026-07-30, docs/internals/plan-loops-cleanup-DONE.md phase 23)
-----------------------------------------------------------------------------
`/home/igzo/github/tycho/src/tychoc.c:402` does not start with any SRC_PREFIX
entry, so until this rule it fell into the skip above and was checked by
NOTHING -- not existence, not bounds, not its anchor.  That is the worst
possible outcome, because CLAUDE.md tells writers to "write full paths in
evidence blocks" (so a bare `:N` cannot bind to whatever file was named last),
and an absolute path satisfies that instruction to the letter while buying
LESS checking than the relative form it replaced.  Deleting `tests/postfreeze/`
left ~40 such refs in the archived plans pointing at files that no longer
existed, and the gate stayed green throughout.

The fix is deliberately NOT to resolve the absolute path against ROOT.  Inside
this repo an absolute path is never the right spelling -- it is not portable to
another checkout and it is not what any other citation in the tree looks like --
so it is reported with the repo-relative form the author should have written.
A following bare `:N` does NOT inherit it (`cur` is cleared), because inheriting
an unspellable path would propagate the same blindness down the paragraph.

ARCHIVED PLANS ARE EXEMPT FROM THIS RULE TOO, and the reason is the one already
settled below: they are frozen records, ~161 of the 187 absolute refs in the
tree live in them, and roughly forty of those name the deleted
`tests/postfreeze/`.  Enforcing here would demand an edit to a record that must
not be edited, and would redden the gate permanently.  They were unchecked
before this rule and they are unchecked after it; nothing regressed.

A PATHLESS `> Provenance:` REF IS A FAILURE (added 2026-07-30, docs/internals/plan-loops-cleanup-DONE.md phase 34)
-------------------------------------------------------------------------------
The mandatory-anchor rule below keys off a resolved path: a ref whose paragraph
names none is `continue`d before that rule can run.  So a `> Provenance:` block
that opens a paragraph without naming a path got ZERO checking -- no file, no
bounds, no anchor -- and did so by accident, not by anyone's decision.  Twelve
refs were in that state; ten were repaired by hand and two ranges were left
path-less ON PURPOSE, to avoid reddening a gate that could not see them.  A
workaround that exists only because the gate is blind is the signal to fix the
gate.  Such a ref is now a hard failure telling the author to write the path.
The fix is NOT to carry `cur` across paragraph breaks -- see the comment at the
top of the Markdown loop for why that was deliberately removed.

DOC -> DOC IS CHECKED TOO (added 2026-07-30, docs/internals/plan-loops-cleanup-DONE.md phase 44)
--------------------------------------------------------------
`docs/` was absent from SRC_PREFIX until this change, so a citation from one
document INTO another -- `docs/spec/18-library.md` naming lines 204-210 of the
pre-rename path of `docs/guides/corelib.md` (spelled out here it would be a live
SOURCE -> DOC citation into a file that no longer exists, and this docstring is
scanned by that pass, so it is deliberately described rather than quoted)
-- hit the fail-open skip below and was checked by NOTHING: not existence, not
bounds, not its anchor.  That is how 31 refs to a document renamed eight months
earlier stayed green, and phase 15 had to repair them by hand because no gate
could see them.  Adding `docs/` here makes the doc->doc direction ordinary: same
bounds, same anchors, same mandatory `> Provenance:` rule as a doc->source ref.

WHAT IT REDDENED, AND WHAT THAT SAYS ABOUT THE BARE FORM.  77 refs: 25 naming a
document that no longer exists at that path, and 52 OUT OF BOUNDS -- of which
EVERY ONE was a bare `:N` inheriting a `docs/` path from its sentence while
meaning `src/tychoc.c`.  Not one was a genuine doc->doc citation that had merely
drifted.  So the widened bounds check is doing double duty: it is the only thing
that can see a mis-inherited continuation ref, and it sees it as an
out-of-bounds read into a file two orders of magnitude smaller than the compiler.
The residual blind spot is the bare form's usual one -- a mis-inherited `:N` that
happens to land inside the document -- and it is not fixed here.  It is fixed by
the next rule.

A `docs/` PATH IS INHERITED ONLY ALONG ITS OWN LINE (added 2026-07-30, phase 63)
-------------------------------------------------------------------------------
The bounds check above catches a mis-inherited `:N` only when the number is far
outside the inheriting document -- which it is when the author meant
`src/tychoc.c` (13k lines) and the paragraph last named a 386-line chapter.  When
the number happens to land INSIDE the chapter, nothing sees it: a
`docs/spec/16-builtins.md` paragraph writing `:20` and meaning `src/tychoc.c:20`
passes every check in this file.  That is not theoretical -- 52 of phase 44's 77
failures were this same mis-inheritance, visible only because the numbers were
large, and one surviving pair had to be repaired by hand in `plan.md` during
batch 10 because the gate could not.

So: a BARE `:N` whose inherited path is a DOCUMENT is accepted only when that
path was named ON THE SAME LINE.  Cross-line inheritance of a document path is a
hard failure telling the author to write the path.  (Keyed on `docs/` when it
shipped; widened on 2026-07-31 to any `.md` target, because the argument is
about the target being small enough for a stray number to land inside it, and
`FRICTION.md` at 790 lines has that property just as much as a spec chapter.)

WHY THAT SHAPE, AND NOT "REQUIRE A PATH ON EVERY DOC REF".  Both were counted
before either was shipped, on the tree at docs/internals/plan-loops-cleanup-DONE.md phase 63.  Forbidding inherited
`docs/` paths outright reddens 45 refs; restricting inheritance to the same line
reddens 16.  The 29 refs in the difference are the continuation form the tables
in `docs/rfc/` and `docs/spec/appendix-h-differences.md` are written in --
`docs/thesis.md:14-17`, `:39-53` inside one cell -- where the path is two
characters away and cannot be mistaken for anything else.  Those are the cases
the broad rule would have cost for no gain, because the failure mode needs
DISTANCE: an author does not lose track of the subject halfway along a line.

Sources other than `docs/` keep unrestricted inheritance.  For them the bounds
check already does this job, since every one of them is far larger than any line
number a doc sentence would produce.

ARCHIVED PLANS ARE EXEMPT FROM THIS RULE: the fix would be an edit to a frozen
record.  They are NOT exempt from the anchor content check -- header, "mostly".

SRC_PREFIX WAS THE HOLE, NOT A SETTING (widened 2026-07-31)
-----------------------------------------------------------
Every rule above runs only if the cited path starts with an entry of SRC_PREFIX.
That list had nine entries and the tree has more than nine places worth citing,
so the FAIL-OPEN skip was silently doing most of the work: 1023 Markdown refs
resolved to a path the gate then declined to look at -- no existence, no bounds,
no anchor -- and 522 of them named something this list could simply have held.
`server/` was the worst of it at 231, and `server/` has been in
DOC_SCAN_PREFIX the whole time, so the gate was reading the web server for
citations OUT while ignoring every citation IN.

WHAT WENT IN, AND THE PRINCIPLE.  A tree of real source that citations point at
belongs; a prefix list that matches everything is not a decision.  Two groups:

  * the source trees DOC_SCAN_PREFIX already trusts as source and this list did
    not -- `server/` (231 refs), `fuzz/` (32), `bench/` (25), `editors/` (12),
    `.githooks/` (0).  Asymmetry between the two directions was the bug; the
    zero-ref trees are in because the set is the argument, not the count.
  * the top-level DOCUMENTS -- `FRICTION.md` (204 refs), `README.md` (12),
    `ROADMAP.md` (5), `CLAUDE.md` (1), and `CONTRIBUTING.md`, `RELEASE_NOTES.md`
    and `SECURITY.md` for the same set-not-count reason.  These get the
    `Makefile` treatment: a prefix entry that is a whole filename.  `docs/` was
    already in, so doc->doc was already policy; these are the documents that
    policy had missed for living at the root.

WHAT STAYED OUT, WITH THE REASON -- these are decisions, not oversights:

  * `plan.md` -- 25 refs, 11 of them ALREADY out of bounds.  It is the rotating
    plan: when one completes it is archived and the next starts at line 1, so a
    line reference into it from an archived record names a document that no
    longer exists in any form.  The number cannot be repaired, because what it
    pointed at is gone -- and it would need repairing again every time the live
    plan grows, which is every phase.  This is the same rot the fourth direction
    below forbids outright; a bounds check on it would be a permanent red that
    nobody is able to clear.  Filed with its count rather than folded in.
  * `.github/`, `branding/` -- 0 refs, and not in DOC_SCAN_PREFIX either, so
    adding them would make this gate treat as source something the other
    direction does not.  An inconsistency with nothing behind it.
  * `Makefile`, `LICENSE` and every other extension-less file -- UNREACHABLE,
    not excluded.  CITE's path group requires a dot and an extension, so the
    Markdown pass cannot produce such a path whatever this list says.  (The
    SOURCE -> SOURCE pass names `Makefile` explicitly for exactly this reason.)

WHAT IT REDDENED: 7, and the shape matters more than the number.  Six were
ANCHORED refs into `server/main.ty`, which has been rewritten twice under them
-- three in `server/README.md`, one in `FRICTION.md`, two inside a frozen
archive.  The seventh was the artefact this widening was expected to produce and
did: a bare continuation ref that had inherited a `server/` path from its
paragraph while meaning nothing of the kind.  It was a TCP PORT NUMBER written
in backticks after a colon, in prose about a port already being occupied.  That
is a fourth class beside "always wrong", "drifted" and "inherited the wrong
path": a span that was never a citation and that this grammar cannot tell from
one.  Ports are the specific shape (a colon, then four or five digits, in
backticks); it is filed rather than fixed here, because the grammar change is
not this widening's to make.

WHAT DID **NOT** APPEAR, AND IT IS THE INTERESTING PART.  The `docs/` widening
found 52 mis-inherited continuation refs and 25 dead paths.  This one found one
mis-inheritance and zero dead paths across 522 newly-reachable refs.  The
difference is that `docs/` was widened over a set of documents that had been
RENAMED under their citations, and these trees have not been.  So the widening
buys almost no repair work today and buys the whole population a gate tomorrow,
which is the honest reading of a 7-red blast radius over 522 refs.

THE FROZEN DOC->DOC SKIP AND THE SAME-LINE INHERITANCE RULE MOVED WITH IT, from
"the target is under `docs/`" to "the target is a document".  Both arguments were
always about the target being a small document rather than about where it lives,
and the prefix was standing in for that.  Not widening them alongside SRC_PREFIX
would have demanded edits to three frozen records for the bare-`:N` class every
rule in this file refuses to touch.  It loses no coverage: 751 frozen refs now
land in that skip, 524 of which were in it already, and all 227 of the rest name
paths that were outside SRC_PREFIX until this same change.

THE SECOND DIRECTION: SOURCE -> DOC (added 2026-07-26)
-----------------------------------------------------
Everything above walks Markdown and checks what it cites in the source.  The
mirror-image citation -- a SOURCE file naming a document -- was checked by
nothing: `check_links.sh` reads Markdown only, and the pass above ignores any
path that is not under SRC_PREFIX.  So `compiler/tychoc0.ty`, `compiler/run.sh`
("Stage 1 of docs/bootstrap.md") and `compiler/fixpoint.sh` ("Stage 4 self-host
fixpoint (docs/bootstrap.md)") all cited a file that DID NOT EXIST, for as long
as anyone had been reading them, and both gates were green the whole time.  That
is the general form of the bug, so it gets the general fix: every tracked
non-Markdown file under DOC_SCAN_PREFIX is scanned for `docs/<...>.md` mentions
and the named document must exist (with the line bounds checked too, when the
mention carries a `:N`).

WHAT THE SECOND DIRECTION DOES **NOT** CATCH:
  * A source comment pointing at a document that exists but does not say what
    the comment claims.  Same class as the behaviour caveat above.
  * A document named without the `.md` suffix, or a directory (`docs/spec/`):
    not a citation of a file, so not checked.
  * A doc mentioned from a Markdown file in prose rather than as a link.  Links
    are check_links.sh's; a bare backticked mention inside a document is
    deliberately left alone, because the archived internals docs quote paths
    that were true when they were written and are a record, not a claim.

THE THIRD DIRECTION: SOURCE -> SOURCE (added 2026-07-29, docs/internals/plan-postfreeze-rawstring-DONE.md phase 8)
-------------------------------------------------------------------------
The runners cite each other as heavily as the docs cite the compiler:
`scripts/frontparity.sh` names lines of `tests/run.sh`, `tests/rtparity/run.py`
names lines of `compiler/fixpoint.sh`, `scripts/asan_self.sh` names lines of the
`Makefile`.  Phase 8 counted 131 such references and found 17 of them pointing
at the wrong line.  Neither pass above could see any of them: the first walks
only `*.md`, and the second matches only paths under `docs/` ending in `.md`.
So this pass scans the same tracked non-Markdown set as the second direction and
checks every `path:N` / `path:N-M` naming another TRACKED NON-MARKDOWN file for
existence and bounds.

COVERAGE OF THE BARE FORM, STATED NARROWLY SO IT IS NOT READ WIDER.  Bounds and
existence only -- exactly the bare-citation semantics of the first pass, with the
same blind spot: a reference that drifts onto a different-but-existing line still
passes.  All 17 that phase 8 repaired were IN BOUNDS and wrong, so the bare check
would have caught none of them.  Its value is the other half: a citation that
points past EOF, or at a file that has been renamed or deleted, reddens instead
of rotting.

ANCHORED SOURCE -> SOURCE (added 2026-07-30, docs/internals/plan-loops-cleanup-DONE.md phase 13)
-------------------------------------------------------------
The wrong-line class needs an expected token, so SRCCITE now accepts an optional
`@token` suffix and content-checks the cited lines when one is present:

    Makefile:304@SKIPPED           as cited from scripts/asan_self.sh
    src/tychoc.c:3366@i_dotlt      as cited from fuzz/run_parforparity.py

TWO DIFFERENCES FROM THE MARKDOWN ANCHOR, both forced by the medium.  (1) The
token is `[A-Za-z0-9_]+` -- no spaces.  A Markdown anchor sits inside a backtick
span that delimits it, so it may contain spaces; a source citation is bare
comment prose with NO closing delimiter, so a space-permitting token would
swallow the rest of the sentence.  (2) It is OPT-IN, with no construct where it
is mandatory.  There is no source-side equivalent of a `> Provenance:` block --
nothing marks a comment as making a load-bearing claim -- and the mandatory rule
earns its keep by applying to a construct an author chose deliberately.

ADOPTION IS PARTIAL AND THE COUNT IS HONEST ABOUT IT: `--stats` prints the
anchored and bare source->source totals separately.  Anchoring an EXISTING bare
ref means first verifying it, which is a citation sweep and belongs to a sweep
phase, not to the phase that added the grammar.  A green run means "no anchored
source citation has drifted", never "every source citation is right".

THE FOURTH DIRECTION: A ROTATING-PLAN REFERENCE (added 2026-07-31, docs/internals/plan-signals-DONE.md phase 24)
---------------------------------------------------------------
Every pass above needs a line number to have something to check.  A reference to
a phase of the repo's live plan document -- the file named at the top of this
paragraph's sibling, `<the live plan>` followed by the word "phase" and a number
-- carries none, so all three passes are blind to it BY CONSTRUCTION.  It is
still a citation, and it rots harder than a line number does: when a plan
completes it is archived to `docs/internals/plan-<name>-DONE.md` and the next
one starts numbering again at 1, so the reference does not merely drift a few
lines, it silently re-binds to a DIFFERENT DOCUMENT at a phase number belonging
to unrelated work.  167 of them over 43 files accumulated before anyone counted.

THE PREDICATE, EXACTLY.  Outside the live plan itself and the frozen
`docs/internals/plan-*-DONE.md` set, no tracked file may carry one.  There is no
third case: such a reference is either about the plan that is live right now --
in which case the commit that archives that plan must rewrite it, and this gate
is what makes that step non-optional -- or it is already stale.

THREE SPELLINGS, AND WHY THE COUNT KEEPS BEING WRONG.  The first survey of this
class matched only the unbackticked singular and reported 110 refs.  The real
figure was 172: it had missed the BACKTICKED form (66 of them) and the PLURAL
one, "phases 1 and 2" (5 more).  A second sweep then missed a fourth variation,
capitalisation, and left 16 refs behind in `Makefile`, `scripts/ci.sh`,
`scripts/asan_self.sh`, five `tests/reject/` fixtures and three documents.  So
the pattern here is deliberately loose: optional backticks on either side,
`phase` or `phases`, case-insensitive, and a separator class that permits a line
break plus a comment leader, because four of the 172 wrapped across two lines and
were invisible to every line-based sweep that had been run until then.

BOTH WORD ORDERS (added 2026-07-31)
-----------------------------------
The pattern above reads in ONE DIRECTION ONLY: the filename, then the word
"phase", then a number.  The MIRROR IMAGE -- the word and its number first and
the filename after it, with or without a connecting preposition -- is the same
citation making the same claim about the same rotating document, and it was
matched by NOTHING.  It was found the way this class always is: THIS DOCSTRING
CONTAINED ONE, and the gate that forbids the class read straight over its own
text and passed.  12 live refs were in that blind spot -- `FRICTION.md` (4),
three `tests/` fixtures, `src/tychoc.c`, `bench/guard.sh`, `server/README.md`,
`tools/prunner/main.ty` and `docs/internals/int64-migration-audit.md` -- and
every one of them was already stale.

THE REVERSED PATTERN IS A MIRROR, NOT A SECOND POLICY: same optional backticks,
same case-insensitivity, same separator class, so it survives the same hard
wrap onto a comment-led continuation line.  Two things the direction forces.
(1) A CONNECTING WORD (`of`, `in`, `from`, `within`) is PERMITTED BUT NOT
REQUIRED -- both variants were measured against the whole tree before either was
shipped and both found the same 12, so the looser one costs nothing today and is
the one that mirrors the forward pattern's shape.  (2) A NUMBER LIST is
tolerated, because in the plural spelling the number the word introduces is not
the number adjacent to the filename, and the forward pattern's way of coping
(match the first number and stop) does not transpose.

Matches from the two orders are merged by position and an overlapping one is
dropped, so a sentence that satisfies both is reported once rather than twice.

MEASURED AND DELIBERATELY NOT ADDED, recorded so the next survey of this class
does not have to re-derive them:
  * a phase named with NO filename at all ("of the plan", "of the live plan",
    "the current plan") -- 0 in the tree, and a pattern for it would key on a
    common English word rather than on a path;
  * a phase named WITHOUT THE WORD, by bare number, `#`, "step" or "item" -- 0;
  * a POSSESSIVE joining the two ("<the live plan>'s phase N", and the variant
    with words in between) -- PRESENT, 8 refs, and filed as its own phase rather
    than folded in here.  It is a third SPELLING, not the second word order, its
    refs need their own attribution, and one of its shapes ("the plan's phase N",
    no filename) needs a decision this phase has no measurement to make.

EXEMPT, WITH THE REASON -- the same shape as ARCHIVED and SRC_SKIP_CITER below:
  * the live plan -- inside it, a reference to its own phase is what the file is
    numbered by, and it is correct by definition.
  * `docs/internals/plan-*-DONE.md` -- frozen records, and inside one of them the
    reference self-refers unambiguously.  Editing them is what the ARCHIVED rule
    forbids everywhere else in this file.
  * `compiler/tychoc0.ty` -- the FROZEN bootstrap compiler, exactly the reason it
    is in SRC_SKIP_CITER: the file cannot be edited, so policing it would produce
    a red nobody is allowed to clear.

THIS FILE IS NOT EXEMPT, and that is on purpose.  It is scanned like any other,
so neither the pattern above nor the failure message below may spell the form
they forbid -- the same discipline the absolute-path rule already follows, and
the reason its docstring describes that form instead of quoting it.

THE BARE-REFERENCE POLICY, AND WHY IT IS NOT "ANCHOR EVERYTHING"
----------------------------------------------------------------
(added 2026-07-31.)  The `ok` line reports thousands of bare refs against a few
hundred anchored ones, and that ratio has twice been read as a backlog.  It is
not one, and the split below is printed on every green run so it stops being
read as one.  Counted on the tree at dd3c019: 2802 bare Markdown refs, of which

    1800  are in the frozen `docs/internals/plan-*-DONE.md` set, where EVERY
          rule in this file already refuses to demand an edit;
      17  are in the live plan's own evidence blocks, which are a record of
          what a ref said at a past moment, not a claim about today;
     196  are `> Provenance:` RANGES, exempt by the settled rule above -- and
          the count that matters about that context is the other one: ZERO of
          its single-line refs are bare, so the one construct where anchoring
          is mandatory is already at 100%.  There is no second construct in
          the tree that marks a ref as load-bearing, so "require anchors in
          named contexts" has nothing left to name;
     789  are reachable narrative prose.

Requiring anchors on those 789 is the hand sweep this repo has now declined
three times, with measurements each time (FRICTION.md: 11 of 15 spot-checked
refs drifting again four days after a repair pass, one reference repointed
four times).  So: BARE REFS STAY BARE.  What ships instead is a rule that makes
each EXISTING anchor worth more, plus a published number for how much an anchor
is actually worth.

NO RATCHET, NO BUDGET, NO SHRINK TARGET -- DELIBERATELY.  A gate that pushed
the bare count downwards would eventually push someone at a before/after
record block, whose line numbers are DATA: "was 846, now 848" is correct
precisely because it is stale, and "repairing" it destroys the evidence it
records.  Nothing in the tree marks those blocks yet (that is a filed, separate
question), so the safe policy is one that asks nobody to sweep.  The only
number here that must go down is the ambiguous-anchor count.

AN AMBIGUOUS ANCHOR IS A FAILURE (added 2026-07-31)
---------------------------------------------------
An anchor's whole value is that it names expected CONTENT.  If the token sits
on more than one LINE inside the cited range, it names no line: the region can
drift internally, or the citation can be repointed at the wrong half of its own
range, and the check still passes.  Such an anchor reads as verified while
verifying nothing, which is strictly worse than the honest bare range -- the
same argument that keeps ranges exempt from the mandatory rule, applied to the
anchors that do exist.  It is now a hard failure telling the author to anchor a
token that occurs once, or to tighten the range to its construct, or to drop
the anchor and leave the range bare.

THE POPULATION WAS COUNTED BEFORE THE RULE WAS WRITTEN: 1 of 216 anchors, in
`docs/spec/03-types.md`'s `bounded`-capacity Provenance block, whose range
opened three comment lines above the guard it meant.  Tightening it to the
`if`/`die_at` pair made the anchor unique AND the citation more precise, which
is the repair this rule is meant to produce.  Frozen archives are exempt from
THIS rule, but not from the anchor content check -- see the header's "mostly".

ANCHOR STRENGTH IS MEASURED, NOT ENFORCED (added 2026-07-31)
------------------------------------------------------------
A token unique in its range can still be common in the FILE, and then a drift
just re-matches somewhere else -- the failure `--stats` now quantifies instead
of assuming away.  Of the non-frozen Markdown anchors, 36 name a token that
recurs within +-25 lines of the cited range (the observed drift scale: an
earlier plan found anchored ranges 9 and 82 lines off), and 82 name one that
recurs somewhere in the file; source->source is 8 and 10 of 16.  Both numbers
are printed, because the window is a choice and hiding the wider figure behind
it would be the same overstatement this section exists to fix.

WHY NOT ENFORCE IT.  17 of the 97 mandatory single-line `> Provenance:` anchors
are weak in that window (20 counting the anchored ranges beside them) -- four
separate refs anchor `@parse_value_ctrl` to four different lines of the same
function, each matching all four.  Enforcing would demand 17 invented
replacement tokens chosen by whoever cleared the red, which is how false anchors
get manufactured.  A sixth of the strongest citations in this tree being weaker
than they look is a fact worth knowing and a bad thing to fix under gate
pressure.

A CITATION TO A DEFINITION IS A SYMBOL, NOT A LINE (added 2026-07-31)
---------------------------------------------------------------------
Every form above needs a line number, and for a citation that names a REGION
that is right: a loop, a table, a function body has no name of its own, so an
address is the only way to point at it.  A citation that names a DEFINITION is
different.  It means "the place where this symbol is bound", and the line number
is an accident of how much prose sits above it.  The constant named at the foot
of this docstring was repointed THREE TIMES across two phases -- twice in one of
them -- and not one of those repairs carried information: the constant never
changed, paragraphs above it grew.  Each repair also had to reach into two frozen
archives, because the number they recorded had gone stale for a reason that had
nothing to do with what they observed.

    `corelib/signal/signal.ty@shutdown_requested`     no number, no drift

THE CHECK IS EXISTENCE OF THE TOKEN IN THE NAMED FILE.  That is weaker than an
anchored line ref and is stated plainly rather than dressed up: a symbol
mentioned at forty call sites is confirmed by any one of them, so this form
proves the symbol is still SPELLED that way in that file, not that its
definition is still there.  What it buys is that the only two events which can
falsify it -- a rename and a deletion -- are exactly the two events that make
the citation wrong, while the event that used to falsify the line form -- an
insertion anywhere above -- can no longer touch it.

WHY UNIQUENESS IS NOT REQUIRED, unlike the ambiguous-anchor rule above.  There
the token's job was to identify ONE LINE inside a cited range, so a token on
three lines identified none.  Here the token IS the subject: a symbol occurring
at its definition and at every use is normal, and demanding uniqueness would
reject every symbol that is actually called anywhere.  Different job, different
rule.

THE POPULATION WAS COUNTED BEFORE THE FORM WAS WRITTEN, over the whole tree,
using this file's own grammar: 37 of 218 anchored refs cite a single line that
BINDS their token (a `fn`/`static`/`#define`/assignment head), of which 22 are
in live files and 15 in frozen archives.  So it is not a mechanism for three
refs.  It is also NOT a sweep: the form ships, three refs move to it, and the
remaining 19 live ones are filed rather than converted, because converting a
correct citation by hand is the drift-inducing pass this repo has declined three
times (see THE BARE-REFERENCE POLICY above).  A definition ref is right to write
in the new form and not wrong in the old one.

THE THREE THAT MOVED are this rule's own motivating case, and they moved for a
reason beyond tidiness: this section's own text pushed the constant down the
file, which would have staled all three a FOURTH time in the commit that
explains why they should not need repairing.  Two of them are in a frozen
`plan-*-DONE.md`, edited here on the settled ground in this file's header -- an
ANCHORED ref in an archive was never exempt -- and with the additional fact that
their numbers were not the observation those archives recorded: `git log` shows
both had already been mechanically renumbered by three later phases, so what was
repaired was residue, not evidence.  Removing the number ends that cycle instead
of extending it.

EXCLUDED BY NAME, WITH THE REASON:
  * `compiler/tychoc0.ty` -- the FROZEN bootstrap compiler.  Its self-citations
    are known to be off by -50 (recorded at docs/bootstrap.md:106) and the file
    cannot be edited, so policing it would produce an unfixable red.  Citations
    INTO it from live files are still checked; only its own are skipped.
  * `*.err` / `*.out` -- GOLDEN COMPILER OUTPUT.  A line like
    `tests/diag/dym_var.ty:3: error: unknown variable` is a diagnostic the
    compiler printed, not a citation a human wrote, and `make test` owns whether
    it is right.  A doc gate must never demand an edit to a generated file.
"""
import re
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# WHAT A MARKDOWN CITATION IS ALLOWED TO NAME (see "SRC_PREFIX WAS THE HOLE"
# in the header). Every tracked source tree, plus the repository's own top-level
# documents. `plan.md` is EXCLUDED ON PURPOSE and the reason is in the header.
SRC_PREFIX = ("docs/", "src/", "compiler/", "runtime/", "corelib/", "tests/",
              "scripts/", "tools/", "examples/",
              # added 2026-07-31: source trees the gate already trusts as source
              # in the SOURCE -> DOC direction, but ignored in this one.
              "server/", "bench/", "fuzz/", "editors/", ".githooks/",
              # added 2026-07-31: the top-level documents. Permanent, stable
              # files that this tree cites as heavily as it cites a source tree
              # (`FRICTION.md` alone carried 204 unchecked refs).
              "FRICTION.md", "README.md", "ROADMAP.md", "CLAUDE.md",
              "CONTRIBUTING.md", "RELEASE_NOTES.md", "SECURITY.md")

# Source trees scanned for the SOURCE -> DOC direction. It used to say here that
# it was deliberately wider than SRC_PREFIX, "which must not move"; SRC_PREFIX
# has now moved and covers every tree named below, so the two differ only in that
# this one adds `Makefile` and SRC_PREFIX adds `docs/` and the top-level
# documents. `Makefile` is named because it is a file, not a tree -- it carried
# one of these citations too.
DOC_SCAN_PREFIX = ("src/", "compiler/", "runtime/", "corelib/", "tests/",
                   "scripts/", "tools/", "examples/", "bench/", "fuzz/",
                   "server/", "editors/", ".githooks/", "Makefile")

# A source file naming a document: `docs/<path>.md`, optionally `:N` or `:N-M`.
# Anchored to `docs/` so it cannot fire on an arbitrary word ending in .md, and
# the extension is required so `docs/spec/` (a directory) is not a citation.
DOCCITE = re.compile(r'(docs/[A-Za-z0-9_./-]*\.md)(?::(\d+)(?:-(\d+))?)?')

# A source file naming another SOURCE file (the third direction). The `:N` is
# MANDATORY here -- a bare `tests/run.sh` is a mention, not a citation -- and the
# match is filtered against the tracked-file set below, so `foo.c:12` quoted from
# some tool's output cannot fire unless `foo.c` is really in the repo.
SRCCITE = re.compile(r'((?:[A-Za-z0-9_][A-Za-z0-9_./-]*\.[A-Za-z0-9]+)|Makefile)'
                     r':(\d+)(?:-(\d+))?(?:@([A-Za-z0-9_]+))?')

# Files whose OWN source->source citations are not policed (see the header).
SRC_SKIP_CITER = ("compiler/tychoc0.ty",)
SRC_SKIP_SUFFIX = (".err", ".out")

CITE = re.compile(r'`(?:([A-Za-z0-9_./-]+\.[A-Za-z0-9]+))?:(\d+)(?:-(\d+))?'
                  r'(?:@([^`]+))?`')   # the anchor token MAY contain spaces
                                       # (`@'main' must be`): a banned-space version
                                       # silently matched nothing and scored those
                                       # citations as unchecked. Fail closed.

# A CITATION TO A DEFINITION (see the header): `path@SYMBOL`, with NO number.
# The absence of the colon is what separates it from CITE above, so the two
# grammars cannot both match the same span. The symbol is a bare identifier --
# no spaces, unlike the line-anchor token, because a phrase is not a definition
# and the anchored form already covers phrases.
SYMCITE = re.compile(r'`([A-Za-z0-9_./-]+\.[A-Za-z0-9]+)@([A-Za-z0-9_]+)`')

# The same form in a source file (no backticks to delimit it), filtered against
# the tracked set exactly as SRCCITE is, so an email address or a stray `a.b@c`
# in some quoted output cannot fire.
SYMCITE_SRC = re.compile(r'\b((?:[A-Za-z0-9_][A-Za-z0-9_./-]*\.[A-Za-z0-9]+)'
                         r'|Makefile)@([A-Za-z0-9_]+)')

# Frozen verification evidence: never demand an anchor here (see the header).
ARCHIVED = ("docs/internals/plan-", "-DONE.md")

# THE FOURTH DIRECTION (see the header): a phase reference into the live plan.
# Optional backticks, singular or plural, any case, and a separator class that
# survives a hard wrap onto a comment-led continuation line.
LIVE_PLAN = "plan.md"
_PLAN = r'`?' + LIVE_PLAN.replace(".", r"\.") + r'`?'
_SEP = r'[\s>#*/-]*'
PLANREF = re.compile(_PLAN + _SEP + r'\bphases?\s+\d+', re.I)

# THE SAME REFERENCE IN THE OTHER WORD ORDER (see the header). A mirror of the
# above, plus the two things the direction forces: an OPTIONAL connecting word,
# and a number list, since in the plural spelling the number adjacent to the
# filename is not the one the word introduces.
PLANREF_REV = re.compile(r'\bphases?\s+\d+(?:\s*(?:,|and|&|\+|to)\s*\d+)*' +
                         _SEP + r'(?:of|in|from|within)?' + _SEP + _PLAN, re.I)

# Exempt from PLANREF, each for the reason spelled out in the header. ARCHIVED
# covers the frozen plans; these two are named individually.
PLANREF_SKIP = (LIVE_PLAN, "compiler/tychoc0.ty")

# The drift window the weak-anchor COUNT (never a failure) is reported over.
# 25 is the observed scale, not a bound: an earlier plan found anchored ranges 9
# and 82 lines off. The "anywhere in the file" figure is printed beside it so the
# choice of window cannot flatter the total.
WEAK_WINDOW = 25

_cache = {}


def anchored_lines(src, a, b, tok):
    """-> the line numbers in [a,b] that literally contain `tok`."""
    return [i for i in range(a, b + 1) if tok in src[i - 1]]


def where_at(on):
    """-> ':3, :4, :12, :35, ...' -- ELIDED, NOT SILENTLY TRUNCATED. A list of
    four printed beside a count of five reads as the whole set, which is the
    same kind of overstatement this file's counts exist to avoid."""
    return ", ".join(":%d" % i for i in on[:4]) + (", ..." if len(on) > 4 else "")


def recurs(src, a, b, tok):
    """-> (within WEAK_WINDOW of the range, anywhere in the file) outside [a,b].

    A token that reappears near the citation is one a drift can re-match by
    accident; this is COUNTED, never failed on (see the header)."""
    out = [i for i, l in enumerate(src, 1) if tok in l and not (a <= i <= b)]
    return (any(a - WEAK_WINDOW <= i <= b + WEAK_WINDOW for i in out), bool(out))


def lines_of(path):
    """-> list of lines, or None if the file does not exist"""
    if path not in _cache:
        fp = os.path.join(ROOT, path)
        _cache[path] = (open(fp, errors="replace").read().split("\n")
                        if os.path.exists(fp) else None)
    return _cache[path]


def main():
    mds = subprocess.run(["git", "ls-files", "*.md"], cwd=ROOT,
                         capture_output=True, text=True, check=True).stdout.split()
    fails, n_bare, n_anchored, n_prov, n_frozen_doc = [], 0, 0, 0, 0
    # THE BARE-REFERENCE POLICY (see the header): the bare total is split into
    # the buckets a policy could and could not act on, so it stops reading as a
    # backlog. Buckets are mutually exclusive, in this order.
    n_bare_frozen, n_bare_plan, n_bare_prov = 0, 0, 0
    n_weak, n_weak_file, n_weak_prov = 0, 0, 0
    n_sym, n_sym_src = 0, 0     # `path@SYMBOL` refs (see the header)
    for md in mds:
        cur = None
        cur_ln = 0          # the line `cur` was named on (see the docs/ rule)
        frozen = md.startswith(ARCHIVED[0]) and md.endswith(ARCHIVED[1])
        prov = False
        for ln, line in enumerate(open(os.path.join(ROOT, md), errors="replace"), 1):
            # A bare `:N` inherits the last path named in the SAME paragraph.
            # Carrying it further is what makes long documents unusable: a `:8969`
            # fifty lines below an unrelated `tests/ffi/run.sh:12` is not a citation
            # into that file, and treating it as one yields noise, not findings.
            if not line.strip():
                cur = None
            # A `> Provenance:` BLOCK: the opening line plus every `>` line that
            # continues it. Anything that is not a blockquote line closes it.
            stripped = line.lstrip()
            if stripped.startswith("> Provenance:"):
                prov = True
            elif not stripped.startswith(">"):
                prov = False
            # A CITATION TO A DEFINITION: `path@SYMBOL` (see the header). It
            # names no line, so it is checked for one thing only -- the token is
            # still spelled that way in that file -- and it deliberately does
            # NOT set `cur`: a form whose whole point is to carry no line number
            # must not become the silent subject of a following bare `:N`.
            for m in SYMCITE.finditer(line):
                sp, sym = m.group(1), m.group(2)
                if not sp.startswith(SRC_PREFIX):
                    continue
                n_sym += 1
                ssrc = lines_of(sp)
                where = "%s:%d  `%s`" % (md, ln, m.group(0).strip("`"))
                if ssrc is None:
                    fails.append("%s -> %s: NO SUCH FILE" % (where, sp))
                elif not any(sym in l for l in ssrc):
                    fails.append(
                        "%s -> '%s' does not appear anywhere in %s. A symbol "
                        "citation survives insertions but not a RENAME or a "
                        "DELETION, which is the whole of what it promises: "
                        "either the definition moved to another file, or the "
                        "name changed and this citation is now about nothing."
                        % (where, sym, sp))
            for m in CITE.finditer(line):
                if m.group(1):
                    cur = m.group(1)
                    cur_ln = ln
                    # AN ABSOLUTE PATH IS A FAILURE, NOT A SKIP (see the header).
                    if cur.startswith("/"):
                        if not frozen:
                            rel = (cur[len(ROOT) + 1:] if cur.startswith(ROOT + "/")
                                   else None)
                            tail = m.group(0).strip("`").split(":", 1)[1]
                            fails.append(
                                "%s:%d  `%s` -> ABSOLUTE PATH, which this gate "
                                "cannot check: %s"
                                % (md, ln, m.group(0).strip("`"),
                                   "write it repo-relative, as `%s:%s`" % (rel, tail)
                                   if rel else
                                   "it names no file inside this repository"))
                        # Never let a following bare `:N` inherit an absolute
                        # path: it would be as unchecked as the one above.
                        cur = None
                        continue
                if not cur:
                    # A `> Provenance:` ref that inherits NO path is not merely
                    # un-anchored, it is entirely unchecked -- no file, no
                    # bounds, and the mandatory-anchor rule below never runs.
                    # Fail closed here; elsewhere a pathless `:N` stays a
                    # deliberate fail-open skip (see FAIL-OPEN CASES).
                    if prov and not frozen:
                        fails.append(
                            "%s:%d  `%s` -> a `> Provenance:` ref that names no "
                            "path and inherits none from its paragraph; nothing "
                            "about it is checked. Write the path: "
                            "`<path>:%s`" % (md, ln, m.group(0).strip("`"),
                                             m.group(0).strip("`").lstrip(":")))
                    continue
                if not cur.startswith(SRC_PREFIX):
                    continue
                # DOC -> DOC INSIDE A FROZEN ARCHIVE (added 2026-07-30,
                # docs/internals/plan-loops-cleanup-DONE.md phase 44).
                # Widening SRC_PREFIX to `docs/` reddened 35 refs in the
                # `plan-*-DONE.md` set, and every one of them is a BARE `:N` that
                # inherited a `docs/` path from its sentence while meaning
                # `src/tychoc.c` -- e.g. `:3181-3277` two words after
                # `docs/spec/02-grammar.md`. Repairing them means editing a frozen
                # record, which the ARCHIVED rule above forbids, so they are
                # skipped for the same reason the anchor and absolute-path rules
                # skip them. Counted separately in --stats so the hole is
                # declared rather than silent.
                #
                # KEYED ON "IS A DOCUMENT", NOT ON `docs/` (widened 2026-07-31
                # with SRC_PREFIX). The argument above is about the TARGET being
                # a document -- small, so a mis-inherited compiler line number
                # lands outside it -- and `FRICTION.md` and `server/README.md`
                # are documents that happen not to live under `docs/`. Keying on
                # the prefix was a proxy for that and stopped being one the
                # moment SRC_PREFIX grew. This LOSES NO EXISTING COVERAGE: of
                # the 751 frozen refs it now covers, the 524 under `docs/` were
                # already skipped and every one of the other 227 names a path
                # that was outside SRC_PREFIX until this same change, so none of
                # them was being checked a moment ago either.
                if frozen and cur.endswith(".md"):
                    n_frozen_doc += 1
                    continue
                # A DOCUMENT PATH IS INHERITED ONLY ALONG ITS OWN LINE
                # (docs/internals/plan-loops-cleanup-DONE.md phase 63). See the
                # header for why this is narrower than "always write the path",
                # and for the 45-vs-16 count that decided it. Widened from
                # `docs/` to any `.md` target on 2026-07-31 for the reason given
                # at the frozen skip above: the rule is about the target being a
                # SMALL document, and `FRICTION.md` (790 lines) has exactly the
                # property -- a mis-inherited `:N` can land inside it.
                if (m.group(1) is None and cur.endswith(".md")
                        and cur_ln != ln):
                    fails.append(
                        "%s:%d  `%s` -> a bare ref inheriting the document path "
                        "`%s` from line %d. A number that lands inside a document "
                        "is checked by nothing, so a `docs/` path carries only "
                        "along the line that names it. Write it: `%s%s`"
                        % (md, ln, m.group(0).strip("`"), cur, cur_ln,
                           cur, m.group(0).strip("`")))
                    continue
                a = int(m.group(2))
                b = int(m.group(3)) if m.group(3) else a
                anchor = m.group(4)
                src = lines_of(cur)
                where = "%s:%d  `%s`" % (md, ln, m.group(0).strip("`"))
                if src is None:
                    fails.append("%s -> %s: NO SUCH FILE" % (where, cur))
                    continue
                if a < 1 or b < a or b > len(src):
                    fails.append("%s -> %s has %d lines: OUT OF BOUNDS"
                                 % (where, cur, len(src)))
                    continue
                if anchor is None:
                    n_bare += 1
                    if frozen:
                        n_bare_frozen += 1
                    elif md == LIVE_PLAN:
                        n_bare_plan += 1
                    elif prov:
                        n_bare_prov += 1
                    # THE ONE MANDATORY ANCHOR (see the header). A single-line ref
                    # inside a `> Provenance:` block must carry `@token`; a RANGE
                    # must not be forced to, so it is deliberately not checked here.
                    if prov and b == a and not frozen:
                        fails.append(
                            "%s -> un-anchored single-line ref in a `> Provenance:` "
                            "block; write `%s:%d@<token>` with a token that appears "
                            "on that line. It currently reads: %s"
                            % (where, cur, a, src[a - 1].strip()[:70] or "(blank)"))
                    continue
                n_anchored += 1
                if prov and b == a and not frozen:
                    n_prov += 1
                on = anchored_lines(src, a, b, anchor)
                if not on:
                    hit = [i for i, l in enumerate(src, 1) if anchor in l][:3]
                    fails.append(
                        "%s -> lines %d-%d of %s do NOT contain '%s'%s"
                        % (where, a, b, cur, anchor,
                           ("; it appears at :" + ", :".join(map(str, hit)))
                           if hit else " (token absent from the whole file)"))
                    continue
                # AN AMBIGUOUS ANCHOR IS A FAILURE (see the header): a token on
                # more than one line of the range names no line, so the range can
                # drift inside itself and still pass.
                if len(on) > 1 and not frozen:
                    fails.append(
                        "%s -> AMBIGUOUS ANCHOR: '%s' is on %d lines of %s (%s), "
                        "so it names none of them and a drift inside the range "
                        "still passes. Anchor a token that occurs once, tighten "
                        "the range to its construct, or drop the anchor -- a "
                        "range with no single subject token is honestly bare."
                        % (where, anchor, len(on), cur, where_at(on)))
                    continue
                if not frozen:
                    near, anywhere = recurs(src, a, b, anchor)
                    n_weak += near
                    n_weak_file += anywhere
                    if near and prov:
                        n_weak_prov += 1
    # --- the second direction: SOURCE -> DOC (see the header) ----------------
    srcs = subprocess.run(["git", "ls-files"], cwd=ROOT,
                          capture_output=True, text=True, check=True).stdout.split()
    tracked = set(srcs)
    n_doc, n_src, n_src_anch = 0, 0, 0
    n_src_weak, n_src_weak_file = 0, 0
    for sf in srcs:
        if sf.endswith(".md") or not sf.startswith(DOC_SCAN_PREFIX):
            continue
        try:
            text = open(os.path.join(ROOT, sf), errors="replace").readlines()
        except (IsADirectoryError, OSError):
            continue
        # --- the third direction: SOURCE -> SOURCE (see the header) ----------
        cites_src = not (sf in SRC_SKIP_CITER or sf.endswith(SRC_SKIP_SUFFIX))
        for ln, line in enumerate(text, 1):
            if cites_src:
                # `path@SYMBOL` from a source file (see the header). Same single
                # promise as the Markdown form; `tracked` does the filtering
                # that backticks do there.
                for m in SYMCITE_SRC.finditer(line):
                    sp, sym = m.group(1), m.group(2)
                    if sp.endswith(".md") or sp not in tracked:
                        continue
                    n_sym_src += 1
                    ssrc = lines_of(sp)
                    if ssrc is not None and not any(sym in l for l in ssrc):
                        fails.append(
                            "%s:%d  `%s` -> '%s' does not appear anywhere in "
                            "%s. A symbol citation survives insertions but not "
                            "a RENAME or a DELETION, which is the whole of what "
                            "it promises."
                            % (sf, ln, m.group(0), sym, sp))
                for m in SRCCITE.finditer(line):
                    tgt = m.group(1)
                    if tgt.endswith(".md") or tgt not in tracked:
                        continue
                    sl = lines_of(tgt)
                    a = int(m.group(2))
                    b = int(m.group(3)) if m.group(3) else a
                    if a < 1 or b < a or b > len(sl):
                        n_src += 1
                        fails.append("%s:%d  `%s` -> %s has %d lines: OUT OF BOUNDS"
                                     % (sf, ln, m.group(0), tgt, len(sl)))
                        continue
                    if m.group(4) is None:
                        n_src += 1
                        continue
                    # ANCHORED source -> source (opt-in, see the header): the
                    # cited lines must literally contain the token.
                    n_src_anch += 1
                    on = anchored_lines(sl, a, b, m.group(4))
                    if not on:
                        hit = [i for i, l in enumerate(sl, 1) if m.group(4) in l][:3]
                        fails.append(
                            "%s:%d  `%s` -> lines %d-%d of %s do NOT contain '%s'%s"
                            % (sf, ln, m.group(0), a, b, tgt, m.group(4),
                               ("; it appears at :" + ", :".join(map(str, hit)))
                               if hit else " (token absent from the whole file)"))
                        continue
                    # AN AMBIGUOUS ANCHOR IS A FAILURE, same rule as the Markdown
                    # pass. No frozen exemption is needed here: SRC_SKIP_CITER
                    # already removes the one file that cannot be edited.
                    if len(on) > 1:
                        fails.append(
                            "%s:%d  `%s` -> AMBIGUOUS ANCHOR: '%s' is on %d lines "
                            "of %s (%s), so it names none of them and a drift "
                            "inside the range still passes. Anchor a token that "
                            "occurs once, tighten the range to its construct, or "
                            "drop the anchor -- a range with no single subject "
                            "token is honestly bare."
                            % (sf, ln, m.group(0), m.group(4), len(on), tgt,
                               where_at(on)))
                        continue
                    near, anywhere = recurs(sl, a, b, m.group(4))
                    n_src_weak += near
                    n_src_weak_file += anywhere
            for m in DOCCITE.finditer(line):
                doc = m.group(1)
                n_doc += 1
                where = "%s:%d  `%s`" % (sf, ln, m.group(0))
                dl = lines_of(doc)
                if dl is None:
                    fails.append("%s -> %s: NO SUCH DOCUMENT" % (where, doc))
                    continue
                if m.group(2):
                    a = int(m.group(2))
                    b = int(m.group(3)) if m.group(3) else a
                    if a < 1 or b < a or b > len(dl):
                        fails.append("%s -> %s has %d lines: OUT OF BOUNDS"
                                     % (where, doc, len(dl)))
    # --- the fourth direction: A ROTATING-PLAN REFERENCE (see the header) ----
    # Whole-file text, not a line loop: four of these are known to have wrapped
    # across two lines, and a line-based sweep is exactly what missed them.
    n_planref = 0
    for f in srcs:
        if f in PLANREF_SKIP:
            continue
        if f.startswith(ARCHIVED[0]) and f.endswith(ARCHIVED[1]):
            continue
        try:
            body = open(os.path.join(ROOT, f), errors="replace").read()
        except (IsADirectoryError, OSError):
            continue
        # BOTH WORD ORDERS (see the header). Merged by position, and a match
        # that overlaps one already reported is dropped: a sentence naming the
        # plan on both sides of its phase number is ONE stale citation, and
        # printing it twice would overstate the population it belongs to.
        found, last_end = [], -1
        for m in sorted(list(PLANREF.finditer(body)) +
                        list(PLANREF_REV.finditer(body)),
                        key=lambda m: (m.start(), -m.end())):
            if m.start() < last_end:
                continue
            found.append(m)
            last_end = m.end()
        for m in found:
            n_planref += 1
            ln = body.count("\n", 0, m.start()) + 1
            fails.append(
                "%s:%d  %r -> a phase reference into the rotating plan, which "
                "carries no line number and so is checked by nothing else here. "
                "The live plan is renumbered from 1 every time one is archived, "
                "so this names a different document's work today. Name the "
                "archived plan: `docs/internals/plan-<name>-DONE.md` phase %s. "
                "(`git log --diff-filter=A -- docs/internals/` gives the "
                "rotation boundaries; `git blame` on this line dates it into "
                "exactly one window; then check the phase number is one that "
                "document declares.)"
                % (f, ln, m.group(0), re.search(r'\d+', m.group(0)).group(0)))
    reachable = n_bare - n_bare_frozen - n_bare_plan - n_bare_prov
    if "--stats" in sys.argv:
        print("citation check: %d anchored (content-checked, %d of them the mandatory "
              "`> Provenance:` single-line refs), %d bare (bounds only), "
              "%d source->doc (existence), %d source->source (bounds), "
              "%d source->source anchored (content-checked), "
              "%d doc->doc skipped as frozen archive, "
              "%d rotating-plan phase reference(s) outside the allowed files, "
              "%d `path@SYMBOL` definition refs (%d of them from source)"
              % (n_anchored, n_prov, n_bare, n_doc, n_src, n_src_anch,
                 n_frozen_doc, n_planref, n_sym + n_sym_src, n_sym_src))
        # THE BARE-REFERENCE POLICY and ANCHOR STRENGTH (see the header). Both
        # are reported, neither is enforced; the weak counts are of the
        # non-frozen anchors, since a frozen one cannot be repaired anyway.
        print("bare refs by what a policy could reach: %d frozen record, "
              "%d live-plan evidence, %d exempt `> Provenance:` range, "
              "%d reachable prose" % (n_bare_frozen, n_bare_plan, n_bare_prov,
                                      reachable))
        print("anchor strength (counted, never failed on): markdown %d weak "
              "within +-%d lines / %d recurring anywhere in the file, of which "
              "%d are `> Provenance:` refs; source->source %d / %d"
              % (n_weak, WEAK_WINDOW, n_weak_file, n_weak_prov,
                 n_src_weak, n_src_weak_file))
    if fails:
        for f in fails:
            print("STALE  " + f)
        print("citation check: FAILED (%d stale citation(s) above)" % len(fails))
        return 1
    # The bare clause carries its split on the GREEN line, not only under
    # --stats: the undivided total is the number that has twice been mistaken
    # for a backlog, and the mistake was made from this line.
    print("citation check: ok (%d anchored contain the token they name and each "
          "names one line, %d bare in bounds (%d frozen record, %d live-plan "
          "evidence, %d exempt `> Provenance:` range, %d reachable prose), "
          "%d source->doc citations resolve, "
          "%d source->source in bounds, %d source->source anchored, "
          "%d `path@SYMBOL` definition refs name a symbol still in their file)"
          % (n_anchored, n_bare, n_bare_frozen, n_bare_plan, n_bare_prov,
             reachable, n_doc, n_src, n_src_anch, n_sym + n_sym_src))
    return 0


if __name__ == "__main__":
    sys.exit(main())
