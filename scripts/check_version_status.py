#!/usr/bin/env python3
"""Every STATUS claim in a tracked doc must name the version that ships.

The failure this exists to catch already happened once and is written up in
ROADMAP.md: the 0.6 -> 0.7 bump matched "Tycho 0.6" and missed "Tycho is 0.6",
so five files kept announcing the previous version after the release. Nothing
in the tree could see it -- a doc gate reads links and citations, never the
arithmetic or the claims in a sentence.

Scope is deliberately NARROW. There are ~1080 version-like tokens in tracked
Markdown and almost all of them are legitimate history: a CHANGELOG heading,
"breaks source compatibility with 0.6", "0.7.0 stopped compiling a copied
handle". Gating those would be a nuisance that gets switched off. What must
agree with the compiler is the STATUS claim -- the sentence telling a reader
which version this is -- and in this tree those are all phrased around
"pre-1.0" or a "Status:" label. The pattern is anchored on that PHRASE rather
than on a spelling of the version, which is what makes it survive the exact
miss ROADMAP records.

    python3 scripts/check_version_status.py
    python3 scripts/check_version_status.py --selfcheck
"""

import re
import subprocess
import sys

# A version token: 0.7 or 0.7.0, never a duration or a C standard. The
# lookbehind keeps the 1.0 INSIDE "pre-1.0" from reading as a claim -- the
# first version of this pattern matched it and flagged three correct lines.
VER = r"(?<!pre-)\d+\.\d+(?:\.\d+)?"
# A status claim is ADJACENCY, not line-membership. Anchoring on "somewhere on
# a line that also says pre-1.0" flagged a CHANGELOG sentence whose subject was
# 0.5.0 and whose mention of pre-1.0 was ten words away. What every real status
# line in this tree has is the version sitting directly against the phrase:
#     **Tycho is 0.7 - pre-1.0**        Tycho 0.7 - pre-1.0, no stability
#     > **Status: 0.7 - pre-1.0.        0.7 is pre-1.0 and there are
STATUS = re.compile(
    r"(?:^|[^\w.])(" + VER + r")\s*\**\s*(?:[-–—]|is)?\s*\**\s*pre-1\.0"
    r"|(?:Status:\s*\**\s*)(" + VER + r")\b",
    re.IGNORECASE,
)
# Read the shipped version out of the compiler, not out of another doc.
SRC = "src/tycho" + "c.c"  # never a literal path -- check_citations scans this file
VERDEF = re.compile(r'#define\s+TYCHO_VERSION\s+"([^"]+)"')


def tracked_markdown():
    out = subprocess.run(
        ["git", "ls-files", "*.md"], capture_output=True, text=True, check=True
    )
    return [p for p in out.stdout.split("\n") if p]


def shipped_version():
    with open(SRC, encoding="utf-8") as fh:
        m = VERDEF.search(fh.read())
    if not m:
        sys.exit("version check: FAILED (no TYCHO_VERSION in " + SRC + ")")
    return m.group(1)


def series(v):
    """0.7.0 and 0.7 are the same claim; a status line may write either."""
    return ".".join(v.split(".")[:2])


def scan(paths, want):
    """Return (offences, hits). hits is every status line found, offending or not."""
    offences, hits = [], []
    for path in paths:
        try:
            with open(path, encoding="utf-8") as fh:
                lines = fh.readlines()
        except OSError:
            continue
        for n, line in enumerate(lines, 1):
            for m in STATUS.finditer(line):
                found = m.group(1) or m.group(2)
                hits.append((path, n, found))
                if series(found) != series(want):
                    offences.append((path, n, found, line.strip()))
    return offences, hits


def report(offences, hits, want):
    for path, n, found, text in offences:
        print("STALE  {}:{}  says {}, ships {}".format(path, n, found, want))
        print("       " + (text[:110] + "..." if len(text) > 110 else text))
    if offences:
        print(
            "version check: FAILED ({} status claim(s) name a version other "
            "than {}; {} scanned)".format(len(offences), want, len(hits))
        )
        return 1
    print(
        "version check: ok ({} status claim(s) across {} file(s) all name {})".format(
            len(hits), len({h[0] for h in hits}), want
        )
    )
    return 0


def selfcheck():
    """Three controls. Each must be OBSERVED, not assumed -- a pattern that
    silently matches nothing is indistinguishable from a clean tree."""
    import os
    import tempfile

    ok = True
    with tempfile.TemporaryDirectory() as d:
        # [c1] a status line naming the wrong version must be caught.
        p = os.path.join(d, "c1.md")
        with open(p, "w", encoding="utf-8") as fh:
            fh.write("**Tycho is 0.1 - pre-1.0 software**, so beware.\n")
        off, _ = scan([p], "9.9.9")
        print("  [c1] wrong status version caught: {}".format("yes" if off else "NO"))
        ok &= bool(off)

        # [c2] history must NOT be caught, or the gate becomes a nuisance and
        # gets switched off. This is the leg that keeps the scope narrow.
        p = os.path.join(d, "c2.md")
        with open(p, "w", encoding="utf-8") as fh:
            fh.write("## [0.6.0] - 2026-08-11\n")
            fh.write("This release breaks source compatibility with 0.5.\n")
            fh.write("0.6.0 stopped compiling a copied handle, so upgrade.\n")
        off, hits = scan([p], "9.9.9")
        print(
            "  [c2] history left alone: {} (0 hits expected, got {})".format(
                "yes" if not hits else "NO", len(hits)
            )
        )
        ok &= not hits

        # [c3] the spelling ROADMAP records as the one that got MISSED last
        # time. Anchoring on the phrase, not the spelling, is the whole design.
        p = os.path.join(d, "c3.md")
        with open(p, "w", encoding="utf-8") as fh:
            fh.write("Tycho is 0.6 - pre-1.0, and this file is a direction.\n")
        off, _ = scan([p], "9.9.9")
        print("  [c3] 'Tycho is X' spelling caught: {}".format("yes" if off else "NO"))
        ok &= bool(off)

        # [c4] a correct status line must stay clean, or [c1] passes on a
        # checker that flags everything.
        p = os.path.join(d, "c4.md")
        with open(p, "w", encoding="utf-8") as fh:
            fh.write("**Tycho is 9.9 - pre-1.0 software**, no stability yet.\n")
        off, hits = scan([p], "9.9.9")
        print(
            "  [c4] correct status stays clean: {} ({} hit(s), 0 offence(s) "
            "expected, got {})".format("yes" if hits and not off else "NO",
                                       len(hits), len(off))
        )
        ok &= bool(hits) and not off

    print("version selfcheck: " + ("ok" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    if "--selfcheck" in sys.argv:
        return selfcheck()
    want = shipped_version()
    paths = tracked_markdown()
    offences, hits = scan(paths, want)
    # A regex that matches nothing looks exactly like a clean tree. This tree
    # has six known status claims; fewer means the pattern broke, not that the
    # docs got tidier.
    if len(hits) < 6:
        print(
            "version check: FAILED (only {} status claim(s) found across {} "
            "tracked docs -- the pattern is not matching, not the tree being "
            "clean)".format(len(hits), len(paths))
        )
        return 1
    return report(offences, hits, want)


if __name__ == "__main__":
    sys.exit(main())
