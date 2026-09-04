#!/usr/bin/env python3
"""Corpus-size literals, checked without running a compiler. `make corpus-check`.

`make parse-check` opens by asserting its corpus sizes against literals, and the
literals must stay literals -- a leg that scores 0 of 0 is green by accident. But
nothing told you a literal had MOVED until the 13-second lane ran, and a commit
that deletes a `.ty` has no reason to run it: 0888bf28 deleted tools/tycho-rsa/
and left five numbers describing a tree that no longer existed, so the lane sat
red on main for three commits.

This is the cheap predictor. It computes each corpus size exactly the way the
lane does and compares it to the literal EXTRACTED from the owning file -- it
keeps no copy of any number, so it cannot itself rot. A red here is a literal to
update; a green here means parse-check's opening counts will pass.

Not covered, deliberately: the three census goldens and `--typecheck`'s refusal
count, which cannot be known without running tychoc1. tests/run.sh's own literals
are already cheap -- `sh tests/run.sh --count` asserts them and exits non-zero.

`--selfcheck` proves every leg can still fail, and that every pattern still
matches exactly once: a renamed variable would otherwise make a leg silently
score nothing.
"""
import os, re, sys

RUNSH = "compiler/run.sh"
VDIFF = "compiler/verdict_diff.py"
TSV = "compiler/reject_class.tsv"
ROOTS = ("tests", "corelib", "tools", "examples", "server", "bench")


def walk(root):
    return [os.path.join(d, n)
            for d, _, ns in os.walk(root) for n in ns if n.endswith(".ty")]


def flat(root):
    return [n for n in os.listdir(root) if n.endswith(".ty")]


def tsv_split():
    n = {}
    with open(TSV) as f:
        rows = [ln for ln in f.read().splitlines() if ln]
    for ln in rows:
        n[ln.split("\t")[1]] = n.get(ln.split("\t")[1], 0) + 1
    return n, len(rows)


def counts():
    """Every figure the lane derives from the tree, by the lane's own recipe."""
    n, nrows = tsv_split()
    return {
        # compiler/run.sh: n_acc / leg1  -- `ls tests/*.ty`
        "n_acc": len(flat("tests")),
        # n_lib / leg1b -- `find corelib -name '*.ty'`
        "n_lib": len(walk("corelib")),
        # n_new / leg1c -- `find tools examples server bench -name '*.ty'`
        "n_new": sum(len(walk(r)) for r in ("tools", "examples", "server", "bench")),
        # verdict_diff.py EXPECT -- every .ty under ROOTS
        "tree": sum(len(walk(r)) for r in ROOTS),
        "SYNTAX": n.get("SYNTAX", 0), "NAME": n.get("NAME", 0),
        "TYPE": n.get("TYPE", 0), "SEMANTIC": n.get("SEMANTIC", 0),
        "n_rej": len(flat("tests/reject")), "n_tsv": nrows,
    }


# (label, owning file, pattern with ONE capture group, key into counts()).
# Each pattern must match exactly once; a rename is a hard failure, not a skip.
LEGS = [
    ("run.sh n_acc",  RUNSH, r'\[ "\$n_acc" = (\d+) \]', "n_acc"),
    ("run.sh n_lib",  RUNSH, r'\[ "\$n_lib" = (\d+) \]', "n_lib"),
    ("run.sh n_new",  RUNSH, r'\[ "\$n_new" = (\d+) \]', "n_new"),
    ("run.sh leg1",   RUNSH, r'^leg_accept "leg1 [^\n]*\s(\d+)$', "n_acc"),
    ("run.sh leg1b",  RUNSH, r'^leg_accept "leg1b[^\n]*\s(\d+)$', "n_lib"),
    ("run.sh leg1c",  RUNSH, r'^leg_accept "leg1c[^\n]*\s(\d+)$', "n_new"),
    ("run.sh SYNTAX", RUNSH, r'\[ "\$nsyn" = (\d+) \]', "SYNTAX"),
    ("run.sh NAME",   RUNSH, r'\[ "\$nname" = (\d+) \]', "NAME"),
    ("run.sh TYPE",   RUNSH, r'\[ "\$ntype" = (\d+) \]', "TYPE"),
    ("run.sh SEMANTIC", RUNSH, r'\[ "\$nsem" = (\d+) \]', "SEMANTIC"),
    ("verdict_diff EXPECT", VDIFF, r'^EXPECT = (\d+)', "tree"),
]


def literal(text, pat, label):
    ms = re.findall(pat, text, re.M)
    if len(ms) != 1:
        print("corpus-check: %s -- pattern matched %d times, expected 1 "
              "(the literal was renamed or moved)" % (label, len(ms)))
        return None
    return int(ms[0])


def run(sources, c):
    bad = 0
    for label, path, pat, key in LEGS:
        lit = literal(sources[path], pat, label)
        if lit is None:
            bad += 1
        elif lit != c[key]:
            print("corpus-check: %s says %d, the tree has %d" % (label, lit, c[key]))
            bad += 1
    if c["n_rej"] != c["n_tsv"]:
        print("corpus-check: %d reject fixtures but %d classified rows -- "
              "rerun scripts/classify_rejects.py" % (c["n_rej"], c["n_tsv"]))
        bad += 1
    return bad


def selfcheck():
    """Every leg must redden on a wrong literal, and on a renamed one."""
    c = counts()
    live = {RUNSH: open(RUNSH).read(), VDIFF: open(VDIFF).read()}
    if run(live, c):
        print("selfcheck: the live tree is not clean; fix that first")
        return 1
    bad = 0
    for label, path, pat, key in LEGS:
        m = re.search(pat, live[path], re.M)
        old = m.group(0)
        # [a] a wrong literal must be caught. Assert the substitution applied.
        new = old.replace(str(c[key]), str(c[key] + 1), 1)
        assert new != old, "control did not substitute: " + label
        assert live[path].count(old) == 1, "control pattern is not unique: " + label
        s = dict(live); s[path] = live[path].replace(old, new, 1)
        assert literal(s[path], pat, label) == c[key] + 1, "control not applied: " + label
        if not run(s, c):
            print("selfcheck: %s stayed green with the literal one too high" % label)
            bad += 1
        # [b] and the revert must be green again.
        if run(dict(live), c):
            print("selfcheck: %s did not revert" % label)
            bad += 1
    # [c] a deleted .ty must redden -- 0888bf28's shape, without touching disk.
    fake = dict(c); fake["n_new"] -= 1; fake["tree"] -= 1
    if not run(live, fake):
        print("selfcheck: a deleted tools/ .ty stayed green")
        bad += 1
    print("selfcheck: %d legs, %s" % (len(LEGS), "FAILED" if bad else "all can fail"))
    return 1 if bad else 0


def main():
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    if "--selfcheck" in sys.argv:
        return selfcheck()
    c = counts()
    bad = run({RUNSH: open(RUNSH).read(), VDIFF: open(VDIFF).read()}, c)
    print("corpus-check: %d literals over tests=%d corelib=%d tools+examples+server+bench=%d "
          "tree=%d reject=%d" % (len(LEGS), c["n_acc"], c["n_lib"], c["n_new"],
                                 c["tree"], c["n_rej"]))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
