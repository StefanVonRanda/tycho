#!/usr/bin/env python3
"""The language surface is FROZEN. This is what enforces it.

Measured 2026-08-22: 91 commits touched docs/spec/ and 69 touched src/tychoc.c
in ten days, 11 of them `feat` on the compiler. A surface that moves that fast
cannot be learned, documented or depended on, and a freeze written as prose in
ROADMAP.md is the kind that drifts -- this repo has the receipts.

Three surfaces, two policies:

  keywords  (src/tychoc.c's lexer)          FROZEN HARD -- no additions, no removals
  builtins  (docs/reference/builtins.md)    FROZEN HARD -- same
  corelib   (every exported `fn` in corelib/) ADDITIONS ALLOWED but RECORDED; a
            removal or a CHANGED SIGNATURE reddens, because that is what breaks
            a program somebody already wrote

Adding a keyword or a builtin is then a two-part act: the code, plus a visible
`--record` diff to surface.lock that a reviewer can refuse. That is the whole
point -- not to make it impossible, but to make it impossible by ACCIDENT.

An unrecorded corelib addition reddens too, since 2026-09-03. Until then it
printed a `note` line attached to no verdict, so the lock never grew and the
notes accumulated: 22 functions across http/httpd/io/json/markdown/path/raster/
strings/toml had arrived under a green lane, each in a real fix or perf commit
but none through the deliberate step this file exists to force. A permanently
green count of "22 addition(s) allowed" is exactly the shape R14 was about --
a verdict that describes work nobody did. The addition is still ALLOWED (it is
not a violation, and the message says so); it just has to be recorded.

    python3 scripts/surface_lock.py             # check
    python3 scripts/surface_lock.py --record    # re-lock, deliberately
    python3 scripts/surface_lock.py --selfcheck
"""

import json
import os
import re
import subprocess
import sys

LOCK = "surface.lock"
KW = re.compile(r'!strcmp\(s,\s*"([a-z_]+)"\)\s*\)\s*return\s+TK_')
# A CONTEXTUAL keyword is lexed as an identifier and matched on its text
# (`soa`, `sink`, `where`, `subscript`...). It is surface a user must learn, so
# freezing only the TK_ ones would have left `soa` free to change -- caught
# 2026-08-22 by comparing the extractor's 32 against the measured reserved list.
# Any accessor, not just `t->text`: sink is `cur(ps)->text`, where is
# `peek(ps, 3)->text`, zero is `e->sval`. Matching only one spelling found 32 of
# them and left `soa`, `sink`, `where`, `subscript` and `yield` OUTSIDE the
# freeze -- the hole was found by diffing this list against the reserved-word
# list measured by hand on 2026-08-14. Over-inclusion is the safe direction: it
# catches a few internal spellings too, and the cost of that is one deliberate
# --record on a rename.
KWCTX = re.compile(r'!strcmp\([\w>()&\s,.\-]*->(?:text|sval),\s*"([a-z_]+)"\)')
# Both the plain `name(` form and the method form `m.get(`.
BUILTIN = re.compile(r"^\|\s*`(?:[a-z_0-9]+\.)?([a-z_0-9]+)\(")
FN = re.compile(r"^fn\s+([a-z_0-9]+)\s*\((.*?)\)\s*(->.*?)?:\s*(#.*)?$")
# A marker matched by strncmp rather than an identifier compare: `# deprecated:`
# is language surface (grid-check exists for it) and no ->text pattern reaches it.
KWMARK = re.compile(r'strncmp\(\w+,\s*"([a-z_]+):",')


def keywords(src="src/tychoc.c"):
    t = open(src).read()
    return sorted(set(KW.findall(t)) | set(KWCTX.findall(t)) | set(KWMARK.findall(t)))


def builtins(doc="docs/reference/builtins.md"):
    out = set()
    for line in open(doc):
        m = BUILTIN.match(line)
        if m:
            out.add(m.group(1))
    return sorted(out)


def corelib(root="corelib"):
    """pkg.fn -> its signature, so a CHANGED one is visible and not just a count."""
    out = {}
    files = subprocess.run(
        ["git", "ls-files", "corelib/*/*.ty"], capture_output=True, text=True
    ).stdout.split()
    for f in files:
        if "/test/" in f:
            continue
        pkg = os.path.basename(os.path.dirname(f))
        for line in open(f):
            m = FN.match(line.rstrip())
            if m:
                sig = "(%s)%s" % (m.group(2).strip(), (m.group(3) or "").strip())
                out["%s.%s" % (pkg, m.group(1))] = sig
    return out


def snapshot():
    return {"keywords": keywords(), "builtins": builtins(), "corelib": corelib()}


def compare(old, new):
    """(hard_failures, allowed_additions) -- the split IS the policy."""
    bad, added = [], []
    for k in ("keywords", "builtins"):
        o, n = set(old[k]), set(new[k])
        for x in sorted(n - o):
            bad.append("%s ADDED: %s -- the surface is frozen" % (k, x))
        for x in sorted(o - n):
            bad.append("%s REMOVED: %s" % (k, x))
    o, n = old["corelib"], new["corelib"]
    for name in sorted(set(n) - set(o)):
        added.append("corelib added: %s%s" % (name, n[name]))
    for name in sorted(set(o) - set(n)):
        bad.append("corelib REMOVED: %s%s" % (name, o[name]))
    for name in sorted(set(o) & set(n)):
        if o[name] != n[name]:
            bad.append("corelib SIGNATURE CHANGED: %s\n    was %s\n    now %s"
                       % (name, o[name], n[name]))
    return bad, added


def rc(bad, added):
    """The verdict. An unrecorded addition fails too -- it is not a violation of
    the freeze, but leaving it green is what let 22 of them pile up unreviewed."""
    return 1 if (bad or added) else 0


def verdict(bad, added):
    return "FAILED" if bad else ("UNRECORDED" if added else "ok")


SELF_OLD = {
    "keywords": ["fn", "match"],
    "builtins": ["len", "str"],
    "corelib": {"io.read": "(p: string)-> string", "io.write": "(p: string)-> bool"},
}


def selfcheck():
    ok = True

    def leg(name, got, want):
        nonlocal ok
        if got != want:
            ok = False
        print("  %-52s %s (got %r)" % (name, "ok" if got == want else "FAILED", got))

    new = dict(SELF_OLD, keywords=["fn", "match", "yield"])
    leg("[1] a NEW keyword reddens", len(compare(SELF_OLD, new)[0]), 1)
    new = dict(SELF_OLD, builtins=["len"])
    leg("[2] a REMOVED builtin reddens", len(compare(SELF_OLD, new)[0]), 1)
    new = dict(SELF_OLD, corelib=dict(SELF_OLD["corelib"], io_new="()"))
    leg("[3] a NEW corelib fn is ALLOWED", compare(SELF_OLD, new)[0], [])
    leg("[3b] and is reported", len(compare(SELF_OLD, new)[1]), 1)
    # [3c] is the leg R13c added: [3] and [3b] both passed for as long as an
    # addition was a note attached to no verdict, so neither could tell an
    # allowed addition from an unreviewed one.
    leg("[3c] an UNRECORDED addition still fails the verdict", rc(*compare(SELF_OLD, new)), 1)
    leg("[3d] and is not called a violation", verdict(*compare(SELF_OLD, new)), "UNRECORDED")
    c = dict(SELF_OLD["corelib"])
    c["io.read"] = "(p: string, n: int)-> string"
    leg("[4] a CHANGED corelib signature reddens", len(compare(SELF_OLD, dict(SELF_OLD, corelib=c))[0]), 1)
    c = dict(SELF_OLD["corelib"])
    del c["io.write"]
    leg("[5] a REMOVED corelib fn reddens", len(compare(SELF_OLD, dict(SELF_OLD, corelib=c))[0]), 1)
    leg("[6] an unchanged surface is clean", compare(SELF_OLD, SELF_OLD), ([], []))
    leg("[6b] and passes the verdict", rc(*compare(SELF_OLD, SELF_OLD)), 0)
    # The live extractors must find something, or every leg above is vacuous
    # against an empty set -- a lock of nothing matches anything.
    leg("[7] the keyword extractor finds keywords", len(keywords()) > 10, True)
    leg("[8] the builtin extractor finds builtins", len(builtins()) > 10, True)
    leg("[9] the corelib extractor finds functions", len(corelib()) > 100, True)
    # [10] is the leg that found the real hole. Three accessor spellings and a
    # strncmp marker mean "grep the lexer" is not one pattern, and the first two
    # versions of this file silently left soa/sink/where/subscript/yield/
    # deprecated OUT of the freeze. The list is the reserved words measured by
    # hand on 2026-08-14; if an extractor stops reaching one, this says so.
    KNOWN = ["soa", "sink", "where", "subscript", "yield", "zero", "bounded",
             "pass", "deprecated", "select", "spawn", "handle", "inout", "match"]
    leg("[10] every hand-measured construct is reachable",
        [w for w in KNOWN if w not in keywords()], [])
    print("surface selfcheck: %s" % ("ok" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    if "--selfcheck" in sys.argv:
        return selfcheck()
    new = snapshot()
    if "--record" in sys.argv:
        json.dump(new, open(LOCK, "w"), indent=1, sort_keys=True)
        print("surface lock: recorded %d keywords, %d builtins, %d corelib functions"
              % (len(new["keywords"]), len(new["builtins"]), len(new["corelib"])))
        return 0
    if not os.path.exists(LOCK):
        print("surface lock: no %s -- run --record once" % LOCK)
        return 2
    bad, added = compare(json.load(open(LOCK)), new)
    for line in added:
        print("UNRECORDED %s" % line)
    for line in bad:
        print("FROZEN %s" % line)
    print("surface lock: %s (%d keywords, %d builtins, %d corelib functions; "
          "%d unrecorded addition(s), %d violation(s))"
          % (verdict(bad, added), len(new["keywords"]), len(new["builtins"]),
             len(new["corelib"]), len(added), len(bad)))
    if added and not bad:
        print("  an addition is allowed, but not silently: read the list above, "
              "then `python3 scripts/surface_lock.py --record` in the same commit")
    return rc(bad, added)


if __name__ == "__main__":
    sys.exit(main())
