#!/usr/bin/env python3
"""Every golden a runner names must be tracked by git.

A lane records a golden, `make <lane>-check` goes green, and the file never
appears in `git status` -- because `.gitignore` ignores `*.out` broadly and
un-ignores per directory, one line per lane that ever recorded one. The tree
stays green forever and a FRESH CLONE fails with `no golden -- run RECORD=1`.
That is not hypothetical: `tools/tycho-ar/expected.out` shipped exactly that
way and was caught by hand, which is why `.gitignore` now carries a paragraph
of apology above `!/tools/tycho-q/*.out`. Nothing in the tree could see it --
`grep -rn error-unmatch scripts/ Makefile` returned nothing for six weeks.

This walks every tracked `run.sh`, extracts the goldens it names, and asserts
each one is tracked. It needs no build: it is `git ls-files` over a text scan,
so it costs milliseconds and belongs early in a sweep.

WHAT COUNTS AS "NAMES A GOLDEN"
-------------------------------
Every token ending `.out` or `.err` on a non-comment line, minus the scratch
files. Scratch is identified MECHANICALLY, not by a name list: a token rooted
at a variable this same script assigns from `mktemp` (`T="$(mktemp -d)"`) is
a temp path, so `$T/c.out` is dropped and `examples/site/expected.out` is not.
That rule reads the runner rather than encoding a convention, so a lane that
renames its temp variable stays classified.

Four substitutions make the rest resolvable, each of them read out of the same
runner:

  $PWD/x            -> x                      (tools/tycho-q/run.sh:71)
  $D/expected.out   -> D's literal assignment (examples/weblog/run.sh:23)
  dir/$name.out     -> dir/*.out              (corelib/run.sh:24)
  ${f%.ty}.err      -> the dirname of the enclosing `for f in <glob>`
                                              (tests/conc/run.sh:79 over :77)

and a bare token with no directory resolves against the runner's own directory
when the runner opens with `cd "$(dirname "$0")"` (examples/life/run.sh:7),
against the repo root otherwise.

A resolved token is either a LITERAL -- which must exist and be tracked -- or a
GLOB, which must match at least one file, all of them tracked.

TRACKED, NOT UN-IGNORED -- and the gap that leaves
--------------------------------------------------
This asserts the golden is in the index. It does NOT assert `.gitignore` would
let it back in if it were deleted and re-recorded, because a tracked file beats
every ignore rule and so the two questions are genuinely different.

Four lanes are live examples of the difference, measured 2026-08-01 with
`git check-ignore -v`: `examples/mandelbrot`, `examples/raytrace`,
`examples/weblog` and `examples/webserver` each have a tracked `expected.out`
sitting inside a directory `.gitignore:77`'s `/examples/*` excludes outright,
with no `!/examples/<dir>/*.out` beneath it. `RECORD=1` over a deleted golden
there re-creates an INVISIBLE file. That is not fixable in four un-ignore lines
-- un-ignoring the directory also exposes the binaries and emitted `.c` those
lanes build in place, which is why the four with a per-directory `.gitignore`
(sqlite, life, snake, minesweeper) needed one. It is filed as its own phase.

Checking it here would redden on those four today, so this gate deliberately
asks the narrower question it can answer cleanly.

THE VACUOUS PASS, AND THE THREE GUARDS AGAINST IT
-------------------------------------------------
"Walk a list, assert each entry" passes trivially when the walk finds nothing.
A bad glob, a renamed variable, a lane that starts spelling its golden some new
way, and this exits 0 having checked zero files. So:

  1. EVERY tracked `run.sh` must be classified. A runner either yields at least
     one golden, or it is named in NO_GOLDEN below with a reason. A new runner,
     or an existing one that stops matching the scan, is a HARD FAILURE naming
     the file -- it cannot drop out silently.
  2. EVERY glob must match at least one file. A directory convention that moves
     out from under the scan reddens here instead of going quiet.
  3. Every golden found is PRINTED, with the `run.sh:line` that names it. The
     output is the evidence; the exit code alone is not.

FLOOR, NOT COUNT -- and why
---------------------------
Guard 1 and 2 are per-lane floors of one. There is deliberately NO global
expected total.

A total is fragile in the way that matters: every new fixture under `tests/`
adds a golden, so the number moves on ordinary work. A number that moves on
ordinary work gets bumped reflexively -- and a floor nobody thinks about before
raising is not a check, it is a chore that reports its own edit history. The
failure this gate exists to catch is not "the tree has fewer goldens than it
did", it is "a lane's goldens became invisible", and a per-lane floor of one
names exactly that, on the lane, without a number to maintain.

# gap: two things this scan does not follow.
#
# (a) A golden whose path is computed from a value the scan cannot see -- read
#     out of a file, or built from a command substitution whose output is not a
#     literal.
# (b) A golden that does not end `.out` or `.err`. That extension set is not a
#     guess: every `golden=`/`gold=` assignment in the tree ends `.out`
#     (11 of them, checked 2026-08-01), the four lanes that name theirs as a
#     bare literal are `life.out`, `mine.out`, `snake.out` and `$D/expected.out`,
#     and the only other recorded outputs are the `.err` diagnostic goldens under
#     tests/diag, tests/warn and tests/conc/abort. A lane recording, say, an
#     `expected.json` would be outside it.
#
# Guard 1 catches either case when it leaves the runner with ZERO goldens -- the
# lane then fails as unclassified. What neither guard catches is a runner that
# names one golden the scan follows AND a second one it does not: the first
# keeps the lane classified and the second is never checked. No runner in the
# tree does that today (checked 2026-08-01 over all 35).

Usage:  python3 scripts/check_goldens.py [-v]
        make goldens-check
"""

import os
import re
import subprocess
import sys

# Runners that name no golden, each with the reason. Guard 1 above requires a
# reason to be here rather than a blank line: a runner that stops asserting
# anything should be an edit to this list, not a silent absence.
NO_GOLDEN = {
    "bench/run.sh": "driver: dispatches the per-bench runners, asserts a time/rss limit",
    "bench/conc/run.sh": "self-consistency: the three builds' checksums must agree with each other",
    "bench/dbquery/run.sh": "self-consistency checksum, as bench/conc/run.sh",
    "bench/dijkstra/run.sh": "self-consistency checksum, as bench/conc/run.sh",
    "bench/gcscan/run.sh": "timing/rss only",
    "bench/indexer/run.sh": "self-consistency checksum, as bench/conc/run.sh",
    "bench/interp/run.sh": "self-consistency checksum, as bench/conc/run.sh",
    "bench/json/run.sh": "self-consistency checksum, as bench/conc/run.sh",
    "bench/latency/run.sh": "timing only",
    "bench/lru/run.sh": "self-consistency checksum, as bench/conc/run.sh",
    "bench/prongB/run.sh": "timing/rss only",
    "bench/raytrace/run.sh": "self-consistency checksum, as bench/conc/run.sh",
    "bench/site/run.sh": "self-consistency checksum, as bench/conc/run.sh",
    "bench/trie/run.sh": "self-consistency checksum, as bench/conc/run.sh",
    "bench/window/run.sh": "timing only",
    "compiler/run.sh": "a differential, not a golden -- see its header, two binaries must print identically",
    "server/run.sh": "asserts HTTP responses from a live daemon, not a recorded stdout",
    "tests/recursion/run.sh": "asserts the compiler fails closed on deep input; no recorded output",
}

TEMP_ASSIGN = re.compile(r'^\s*([A-Za-z_][A-Za-z0-9_]*)=.*\bmktemp\b')
LIT_ASSIGN = re.compile(r'^\s*([A-Za-z_][A-Za-z0-9_]*)=("?)([A-Za-z0-9_./-]+)\2\s*(?:#.*)?$')
FOR_LOOP = re.compile(r'^\s*for\s+([A-Za-z_][A-Za-z0-9_]*)\s+in\s+([A-Za-z0-9_./*?-]+)')
SELF_CD = re.compile(r'^\s*cd\s+"?\$\(dirname\s+"?\$0"?\)"?\s*(?:\|\||$)')
CMDSUB = re.compile(r'\$\([^()]*\)')
TOKEN = re.compile(r"""[^\s"'`;()|&<>=]+\.(?:out|err)\b""")
VARROOT = re.compile(r'^\$\{?([A-Za-z_][A-Za-z0-9_]*)\}?/')


def tracked_set():
    out = subprocess.run(["git", "ls-files", "-z"], capture_output=True, text=True, check=True)
    return set(p for p in out.stdout.split("\0") if p)


def scan(path, tracked):
    """Yield (line_no, resolved_pattern, is_glob) for each golden `path` names."""
    src = open(path, encoding="utf-8", errors="replace").read().splitlines()
    tempvars, litvars, loopvars = set(), {}, {}
    for line in src:
        m = TEMP_ASSIGN.match(line)
        if m:
            tempvars.add(m.group(1))
    base = os.path.dirname(path) if any(SELF_CD.match(l) for l in src) else ""

    for n, line in enumerate(src, 1):
        m = LIT_ASSIGN.match(line)
        if m and m.group(1) not in tempvars:
            litvars[m.group(1)] = m.group(3)
        m = FOR_LOOP.match(line)
        if m:
            loopvars[m.group(1)] = m.group(2)
        if line.lstrip().startswith("#"):
            continue

        # A command substitution is opaque; collapse it to a marker so the token
        # regex does not split on the spaces inside it. `$(basename "$hi" .ty).err`
        # becomes `@.err`, which reads as "variable basename" below.
        #
        # But collapsing ALSO eats a golden named INSIDE one: `$(cat
        # "${f%.ty}.err")` collapses to a bare `@` and tests/conc/run.sh:79's
        # three abort goldens vanished silently -- the exact vacuous pass this
        # file is about, inside the scan meant to prevent it. So scan the raw
        # line too and take the union. A token split by a `(` cannot end in
        # `.out`/`.err` with a character before the dot, so the raw pass adds
        # no false positives.
        toks = dict.fromkeys(TOKEN.findall(CMDSUB.sub("@", line)) + TOKEN.findall(line))
        for tok in toks:
            # (a) scratch: rooted at a variable this runner got from mktemp
            r = VARROOT.match(tok)
            if r and r.group(1) in tempvars:
                continue
            # (b) $PWD/ is a no-op prefix used to survive a mid-script cd
            tok = re.sub(r'^\$\{?PWD\}?/', "", tok)
            # (c) a literal-assigned variable at the root
            r = VARROOT.match(tok)
            if r and r.group(1) in litvars:
                tok = litvars[r.group(1)] + tok[r.end() - 1:]
            # (d) a loop variable anywhere: take the enclosing glob's directory
            lv = re.match(r'^\$\{?([A-Za-z_][A-Za-z0-9_]*)[%#]', tok) or VARROOT.match(tok)
            if lv and lv.group(1) in loopvars and "/" not in tok.rstrip(tok.split(".")[-1]):
                d = os.path.dirname(loopvars[lv.group(1)])
                tok = os.path.join(d, "*." + tok.rsplit(".", 1)[1])

            d, b = os.path.split(tok)
            if "$" in d or "@" in d:
                yield n, tok, None          # unresolvable directory -> reported as a gap
                continue
            if "$" in b or "@" in b:
                tok = os.path.join(d, "*." + b.rsplit(".", 1)[1])
                is_glob = True
            else:
                is_glob = "*" in b or "?" in b
            yield n, os.path.normpath(os.path.join(base, tok)), is_glob


def main():
    verbose = "-v" in sys.argv
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)
    tracked = tracked_set()
    runners = sorted(p for p in tracked if p == "run.sh" or p.endswith("/run.sh"))

    errors, gaps, rows = [], [], []
    lanes = 0
    for r in runners:
        found = {}
        for n, pat, is_glob in scan(r, tracked):
            if is_glob is None:
                gaps.append((r, n, pat))
                continue
            found.setdefault(pat, (n, is_glob))
        if not found:
            if r not in NO_GOLDEN:
                errors.append("%s: names no golden and is not in NO_GOLDEN "
                              "(a new runner, or one whose golden the scan stopped "
                              "following -- add it to NO_GOLDEN with a reason, or fix "
                              "the scan)" % r)
            continue
        if r in NO_GOLDEN:
            errors.append("%s: is in NO_GOLDEN but the scan found a golden -- "
                          "remove the entry" % r)
        lanes += 1
        for pat, (n, is_glob) in sorted(found.items()):
            if is_glob:
                hits = sorted(p for p in tracked if _glob_match(pat, p))
                disk = sorted(_disk_glob(pat))
                if not disk and not hits:
                    errors.append("%s:%d: %s matches nothing -- the lane's golden "
                                  "convention moved and this check went blind" % (r, n, pat))
                    continue
                untracked = [p for p in disk if p not in tracked]
                for p in untracked:
                    errors.append("%s:%d: %s exists but is NOT tracked by git "
                                  "-- a fresh clone fails with `no golden`" % (r, n, p))
                rows.append((r, n, pat, len(disk), len(untracked)))
                if verbose:
                    for p in disk:
                        print("      %s %s" % ("!!" if p not in tracked else "ok", p))
            else:
                ok = pat in tracked
                if not os.path.exists(pat):
                    errors.append("%s:%d: %s is named as a golden but does not exist"
                                  % (r, n, pat))
                elif not ok:
                    errors.append("%s:%d: %s exists but is NOT tracked by git "
                                  "-- a fresh clone fails with `no golden`" % (r, n, pat))
                rows.append((r, n, pat, 1, 0 if ok else 1))

    total = 0
    for r, n, pat, cnt, bad in rows:
        total += cnt
        mark = "FAIL" if bad else "ok  "
        print("%s %-28s %-32s %4d file%s" % (mark, "%s:%d" % (r, n), pat, cnt,
                                             " " if cnt == 1 else "s"))

    for r, n, pat in gaps:
        print("gap  %s:%d %s (directory is not statically resolvable; see the "
              "`# gap:` note in this script's header)" % (r, n, pat))

    if not errors:
        print("\n%d runner%s scanned, %d name a golden, %d in NO_GOLDEN, "
              "%d golden file%s checked, all tracked by git."
              % (len(runners), "" if len(runners) == 1 else "s", lanes,
                 len(NO_GOLDEN), total, "" if total == 1 else "s"))

    if errors:
        sys.stdout.flush()   # the table is the evidence; it must precede the verdict
        print("\ngoldens-check: FAIL", file=sys.stderr)
        for e in errors:
            print("  " + e, file=sys.stderr)
        return 1
    print("goldens-check: ok")
    return 0


def _glob_match(pat, p):
    import fnmatch
    return fnmatch.fnmatch(p, pat) and os.path.dirname(p) == os.path.dirname(pat)


def _disk_glob(pat):
    import glob as _g
    return [p for p in _g.glob(pat) if os.path.isfile(p)]


if __name__ == "__main__":
    sys.exit(main())
