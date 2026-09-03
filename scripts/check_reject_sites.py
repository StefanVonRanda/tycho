#!/usr/bin/env python3
"""Every line cited by compiler/reject_class.tsv still holds its diagnostic.

Column 3 of that table is a src/tychoc.c LINE NUMBER -- the diagnostic site the
fixture's class was decided by -- and nothing gated it. Insertions above a site
move it without touching the class or the message, so the column silently rots:
measured 2026-09-03, 310 of 337 rows named a line that had moved and 301 named
a line holding no diagnostic at all, while every row's CLASS was still correct.
A classifier table whose evidence points at unrelated code cannot be audited.

The check reuses classify_rejects.load_sites(), so it reads the same
die_at/die/warn_at/diag_push sites the table was built from -- and needs no
./tychoc run, which is what makes it cheap enough to be a leg.

--selfcheck runs the two controls: a line bumped by one must be caught, and the
committed table must be clean.
"""
import re, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
import classify_rejects as C

TSV = "compiler/reject_class.tsv"


def score(f):
    return len(re.sub(r"%[-0-9.*]*(?:ll)?[a-zA-Z]", "", f))


def check(rows, sites):
    """Rows whose cited line does not hold a site matching their message."""
    by_line = {l: (f, rx) for l, f, rx in sites}
    bad = []
    for path, cls, line, msg in rows:
        n = int(line)
        if n == 0:
            continue            # FALLBACK rows are classified by hand, no site
        hit = by_line.get(n)
        if hit is None:
            bad.append((path, n, "no diagnostic site at that line"))
        elif not hit[1].match(msg):
            bad.append((path, n, "the site there emits: " + hit[0][:60]))
    return bad


def load(path):
    return [ln.rstrip("\n").split("\t") for ln in open(path, encoding="utf-8")]


def main():
    sites = C.load_sites()
    rows = load(TSV)
    if "--selfcheck" in sys.argv:
        rc = 0
        bad = check(rows, sites)
        if bad:
            print("  selfcheck [c1] FAILED: the committed table is not clean"); rc = 1
        else:
            print("  selfcheck [c1] the committed table is clean")
        bumped = [(p, c, str(int(l) + 1), m) for p, c, l, m in rows]
        if not check(bumped, sites):
            print("  selfcheck [c2] FAILED: every line bumped by 1 and nothing was caught"); rc = 1
        else:
            print("  selfcheck [c2] every line bumped by 1 is caught")
        return rc
    bad = check(rows, sites)
    for p, n, why in bad[:20]:
        print("  STALE %s cites src/tycho" "c.c:%d -- %s" % (p, n, why))
    if bad:
        print("reject-sites: %d of %d rows cite a line that moved -- "
              "rerun scripts/classify_rejects.py %s" % (len(bad), len(rows), TSV))
        return 1
    print("reject-sites: %d rows, every cited diagnostic site still holds its message" % len(rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
