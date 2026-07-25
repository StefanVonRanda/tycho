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

    `src/tychoc.c:7181`                path + single line                -> BARE
    `src/tychoc.c:7206-7207`           path + range                      -> BARE
    `:7206-7207`                       continuation: inherits the last path named
                                       IN THE SAME PARAGRAPH              -> BARE
    `src/tychoc.c:7206-7207@main`      path + range + anchor token    -> ANCHORED
    `:3251@has_prefix`                 anchored continuation          -> ANCHORED

WHAT IS CHECKED
    BARE      the file exists, and 1 <= N <= M <= (lines in file).
    ANCHORED  the above, PLUS the cited line range must literally contain the
              anchor token.  The token is chosen when the citation is verified,
              so it is the expected content a bare `path:N` cannot carry.

WHAT THIS DOES **NOT** CATCH -- stated plainly so the coverage is not read wider
than it is:
  * A BARE citation that drifts onto a different-but-existing line.  That is the
    exact `15-program.md` failure mode (plan.md Phase 43): the lines still held
    plausible C, just for a different rule.  A bare citation carries no expected
    content, so nothing can check it -- only the ANCHORED form catches this.
    Anchoring is OPT-IN and adoption is partial; `--stats` prints the split, and
    the honest reading of a green run is "no anchored citation has drifted, and
    no citation of either kind points outside its file".
  * A drift that happens to keep the anchor token inside the new range (e.g. a
    range citing a `main` check that slides onto a different `main` mention).
    Pick a token distinctive within the file, not a common word.
  * A citation that was wrong the day it was written and anchored to its own
    wrong line (garbage in).
  * A docs claim that is wrong about *behaviour* rather than about a line number.
    Different class entirely; no line-checker can see it.

FAIL-OPEN CASES (deliberate, RULE 7): a bare `:N` whose paragraph names no path
is skipped rather than guessed at, and a path outside the implementation trees
listed in SRC_PREFIX is ignored (cross-document links are check_links.sh's job).
"""
import re
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SRC_PREFIX = ("src/", "compiler/", "runtime/", "corelib/", "tests/", "scripts/",
              "tools/", "examples/")

CITE = re.compile(r'`(?:([A-Za-z0-9_./-]+\.[A-Za-z0-9]+))?:(\d+)(?:-(\d+))?'
                  r'(?:@([^`]+))?`')   # the anchor token MAY contain spaces
                                       # (`@'main' must be`): a banned-space version
                                       # silently matched nothing and scored those
                                       # citations as unchecked. Fail closed.

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
    fails, n_bare, n_anchored = [], 0, 0
    for md in mds:
        cur = None
        for ln, line in enumerate(open(os.path.join(ROOT, md), errors="replace"), 1):
            # A bare `:N` inherits the last path named in the SAME paragraph.
            # Carrying it further is what makes long documents unusable: a `:8969`
            # fifty lines below an unrelated `tests/ffi/run.sh:12` is not a citation
            # into that file, and treating it as one yields noise, not findings.
            if not line.strip():
                cur = None
            for m in CITE.finditer(line):
                if m.group(1):
                    cur = m.group(1)
                if not cur or not cur.startswith(SRC_PREFIX):
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
                    continue
                n_anchored += 1
                if not any(anchor in src[i - 1] for i in range(a, b + 1)):
                    hit = [i for i, l in enumerate(src, 1) if anchor in l][:3]
                    fails.append(
                        "%s -> lines %d-%d of %s do NOT contain '%s'%s"
                        % (where, a, b, cur, anchor,
                           ("; it appears at :" + ", :".join(map(str, hit)))
                           if hit else " (token absent from the whole file)"))
    if "--stats" in sys.argv:
        print("citation check: %d anchored (content-checked), %d bare (bounds only)"
              % (n_anchored, n_bare))
    if fails:
        for f in fails:
            print("STALE  " + f)
        print("citation check: FAILED (%d stale citation(s) above)" % len(fails))
        return 1
    print("citation check: ok (%d anchored contain the token they name, "
          "%d bare in bounds)" % (n_anchored, n_bare))
    return 0


if __name__ == "__main__":
    sys.exit(main())
