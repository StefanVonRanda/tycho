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

Column 2, the CLASS, is checked the same way and for the same reason: the
SYNTAX boundary was a typed line number until 2026-09-04, and when it went stale
three fixtures moved SYNTAX -> SEMANTIC with every cited line still holding its
own message -- invisible to the line leg above. The boundary is derived now
(classify_rejects.find_parse_end), and [c4]/[c5]/[c6] are what say so.

--selfcheck runs six controls: the committed table clean on both legs, a line
bumped by one caught, a moved boundary caught and restored, an insertion above
parse_program following, and a renamed parse_program raising.
"""
import re, sys, os, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
import classify_rejects as C

TSV = "compiler/reject_class.tsv"


def score(f):
    return len(re.sub(r"%[-0-9.*]*(?:ll)?[a-zA-Z]", "", f))


def group(sites):
    """{line: [(format, regex)]} -- a LIST, because one line can hold two rules.
    A ternary (`cond ? "a" : "b"`) is two formats at one line, and a dict keyed
    on the line silently kept only the second: src/tychoc.c:619 reported the
    binary-literal rule as the site of the hex-literal fixture."""
    by = {}
    for l, f, rx in sites:
        by.setdefault(l, []).append((f, rx))
    return by


def check_class(rows, sites):
    """Rows whose recorded CLASS is no longer what their site decides.

    The line leg above cannot see this: a boundary that moves reclassifies a
    fixture while every cited line still holds its own message. That happened on
    2026-09-04 -- three const-folding sites crossed the hard-coded SYNTAX
    boundary and the table would have carried SEMANTIC as the truth.
    """
    by_line = group(sites)
    bad = []
    for path, cls, line, msg in rows:
        n = int(line)
        if n == 0:
            continue            # FALLBACK rows are classified by hand, no site
        hits = [(f, rx) for f, rx in by_line.get(n, []) if rx.match(msg)]
        if not hits:
            continue            # already reported by check()
        got = C.classify_site(n, hits[0][0])
        if got != cls:
            bad.append((path, n, "the site there is %s, the table says %s" % (got, cls)))
    return bad


def check(rows, sites):
    """Rows whose cited line does not hold a site matching their message."""
    by_line = group(sites)
    bad = []
    for path, cls, line, msg in rows:
        n = int(line)
        if n == 0:
            continue            # FALLBACK rows are classified by hand, no site
        hits = by_line.get(n, [])
        if not hits:
            bad.append((path, n, "no diagnostic site at that line"))
        elif not any(rx.match(msg) for _, rx in hits):
            bad.append((path, n, "the site there emits: " + hits[0][0][:60]))
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
        if check_class(rows, sites):
            print("  selfcheck [c3] FAILED: a committed row's class is not what its site decides"); rc = 1
        else:
            print("  selfcheck [c3] every row's class is what its site decides")
        # [c4] the real V2b condition: the SYNTAX boundary moves and fixtures
        # reclassify while every cited line still holds its message. Pulled back
        # to the highest SYNTAX row's own line, so the shift is derived from the
        # table rather than typed, and asserted to have moved.
        syn = [int(l) for _, c, l, _ in rows if c == "SYNTAX" and int(l) > 0]
        moved = max(syn)
        old = C.PARSE_END
        assert moved != old, "boundary control did not move the boundary"
        C.PARSE_END = moved                      # the row at `moved` is no longer < the boundary
        drift = check_class(rows, sites)
        C.PARSE_END = old
        if not drift:
            print("  selfcheck [c4] FAILED: boundary %d -> %d and no row reclassified" % (old, moved)); rc = 1
        else:
            print("  selfcheck [c4] boundary %d -> %d reclassifies %d row(s), caught"
                  % (old, moved, len(drift)))
        if check_class(rows, sites):
            print("  selfcheck [c4] FAILED: the boundary was not restored"); rc = 1
        # [c5] the boundary is derived, and the derivation follows an insertion
        # above parse_program rather than needing a human to retype it.
        src = open(C.SRC, encoding="utf-8", errors="replace").read().split("\n")
        i = [k for k, ln in enumerate(src) if ln.startswith(C.PARSE_FN)][0]
        tmpdir = tempfile.mkdtemp(prefix="reject-sites-")
        tmp = tmpdir + "/shifted.c"
        open(tmp, "w").write("\n".join(src[:i] + [""] * 23 + src[i:]))
        got = C.find_parse_end(tmp)
        if got != old + 23:
            print("  selfcheck [c5] FAILED: 23 lines inserted above parse_program, "
                  "boundary %d -> %d" % (old, got)); rc = 1
        else:
            print("  selfcheck [c5] 23 lines above parse_program move the boundary %d -> %d"
                  % (old, got))
        # [c6] and it fails loudly rather than guessing when the anchor is gone.
        open(tmp, "w").write("\n".join(src).replace(C.PARSE_FN, "static ProcVec parse_prog2(", 1))
        try:
            C.find_parse_end(tmp)
            print("  selfcheck [c6] FAILED: parse_program renamed and a boundary came back anyway"); rc = 1
        except LookupError:
            print("  selfcheck [c6] a renamed parse_program raises instead of guessing")
        os.remove(tmp); os.rmdir(tmpdir)
        return rc
    bad = check(rows, sites) + check_class(rows, sites)
    for p, n, why in bad[:20]:
        print("  STALE %s cites src/tycho" "c.c:%d -- %s" % (p, n, why))
    if bad:
        print("reject-sites: %d of %d rows cite a line that moved or changed class -- "
              "rerun scripts/classify_rejects.py %s" % (len(bad), len(rows), TSV))
        return 1
    print("reject-sites: %d rows, every cited diagnostic site still holds its message "
          "and its class (SYNTAX boundary: line %d, derived)" % (len(rows), C.PARSE_END))
    return 0


if __name__ == "__main__":
    sys.exit(main())
