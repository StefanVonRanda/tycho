#!/usr/bin/env python3
"""Provenance-citation checker: does `path:N` still point at what it claims?

    python3 scripts/check_citations.py            # check (exit 1 on failure)
    python3 scripts/check_citations.py --stats    # also print the anchored/bare split

WHY THIS EXISTS.  The docs cite implementation lines as `src/tychoc.c:7206-7207`.
Every edit to a cited file shifts a share of those numbers, and the failure is
silent: the cited line still exists and still contains plausible-looking code, so
nothing looks wrong until a human reads it.  plan.md Phases 34, 35, 40 and 43 each
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

ARCHIVED PLANS ARE EXCLUDED, on the rule phase 4 of plan.md settled: the
`docs/internals/plan-*-DONE.md` set is frozen verification evidence, line numbers
recorded as they stood when the work was done.  Renumbering them would falsify
the record rather than repair it, so this gate must never demand an edit there.
(At the time the rule landed those files carried zero `> Provenance:` lines, so
the exclusion is a guard against a future one, not a way to pass today.)

WHAT THIS DOES **NOT** CATCH -- stated plainly so the coverage is not read wider
than it is:
  * A BARE citation that drifts onto a different-but-existing line.  That is the
    exact `15-program.md` failure mode (plan.md Phase 43): the lines still held
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
below -- and a RELATIVE path outside the implementation trees listed in
SRC_PREFIX is ignored (cross-document links are check_links.sh's job).

AN ABSOLUTE PATH IS A FAILURE, NOT A SKIP (added 2026-07-30, plan.md phase 23)
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

A PATHLESS `> Provenance:` REF IS A FAILURE (added 2026-07-30, plan.md phase 34)
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

THE THIRD DIRECTION: SOURCE -> SOURCE (added 2026-07-29, plan.md phase 8)
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

ANCHORED SOURCE -> SOURCE (added 2026-07-30, plan.md phase 13)
-------------------------------------------------------------
The wrong-line class needs an expected token, so SRCCITE now accepts an optional
`@token` suffix and content-checks the cited lines when one is present:

    Makefile:253@SKIPPED           as cited from scripts/asan_self.sh
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

SRC_PREFIX = ("src/", "compiler/", "runtime/", "corelib/", "tests/", "scripts/",
              "tools/", "examples/")

# Source trees scanned for the SOURCE -> DOC direction. Deliberately WIDER than
# SRC_PREFIX (which governs the md -> src direction and must not move): a comment
# in `bench/` or `fuzz/` can name a missing document just as easily. `Makefile` is
# named because it is a file, not a tree -- it carried one of these citations too.
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

# Frozen verification evidence: never demand an anchor here (see the header).
ARCHIVED = ("docs/internals/plan-", "-DONE.md")

_cache = {}


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
    fails, n_bare, n_anchored, n_prov = [], 0, 0, 0
    for md in mds:
        cur = None
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
            for m in CITE.finditer(line):
                if m.group(1):
                    cur = m.group(1)
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
                if not any(anchor in src[i - 1] for i in range(a, b + 1)):
                    hit = [i for i, l in enumerate(src, 1) if anchor in l][:3]
                    fails.append(
                        "%s -> lines %d-%d of %s do NOT contain '%s'%s"
                        % (where, a, b, cur, anchor,
                           ("; it appears at :" + ", :".join(map(str, hit)))
                           if hit else " (token absent from the whole file)"))
    # --- the second direction: SOURCE -> DOC (see the header) ----------------
    srcs = subprocess.run(["git", "ls-files"], cwd=ROOT,
                          capture_output=True, text=True, check=True).stdout.split()
    tracked = set(srcs)
    n_doc, n_src, n_src_anch = 0, 0, 0
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
                    if not any(m.group(4) in sl[i - 1] for i in range(a, b + 1)):
                        hit = [i for i, l in enumerate(sl, 1) if m.group(4) in l][:3]
                        fails.append(
                            "%s:%d  `%s` -> lines %d-%d of %s do NOT contain '%s'%s"
                            % (sf, ln, m.group(0), a, b, tgt, m.group(4),
                               ("; it appears at :" + ", :".join(map(str, hit)))
                               if hit else " (token absent from the whole file)"))
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
    if "--stats" in sys.argv:
        print("citation check: %d anchored (content-checked, %d of them the mandatory "
              "`> Provenance:` single-line refs), %d bare (bounds only), "
              "%d source->doc (existence), %d source->source (bounds), "
              "%d source->source anchored (content-checked)"
              % (n_anchored, n_prov, n_bare, n_doc, n_src, n_src_anch))
    if fails:
        for f in fails:
            print("STALE  " + f)
        print("citation check: FAILED (%d stale citation(s) above)" % len(fails))
        return 1
    print("citation check: ok (%d anchored contain the token they name, "
          "%d bare in bounds, %d source->doc citations resolve, "
          "%d source->source in bounds, %d source->source anchored)"
          % (n_anchored, n_bare, n_doc, n_src, n_src_anch))
    return 0


if __name__ == "__main__":
    sys.exit(main())
