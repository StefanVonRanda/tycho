#!/usr/bin/env python3
"""Per-file verdict differential: ./tychoc vs ./tychoc1, over the WHOLE tree.

compiler/run.sh's legs 1..1c and 2 score the corpora a parser phase was written
against. This scores every .ty under tests/ corelib/ tools/ examples/ server/
bench/ -- 1,078 files, including the tests/conc, tests/diag and tests/reject/pkg
subtrees no other leg reaches. Three of the four defects it found in Phase 4
were in those subtrees.

tychoc's verdict is SPLIT the way compiler/reject_class.tsv is, by the site that
emitted the message, because a type error is an ACCEPT for a parser: a file
tychoc refuses for a SEMANTIC reason must PARSE, and one it refuses for a SYNTAX
reason must be REFUSED. An UNMATCHED message is a failure, not a skip -- it means
the classifier cannot name the rule, so the file was never really compared.

Phase 5 added the NAME class and a second tychoc1 mode. Each file is scored
twice against the SAME tychoc verdict:

  --parse    must reject exactly the SYNTAX files            (leg5)
  --resolve  must reject exactly the SYNTAX + NAME files     (leg6)

and every file tychoc ACCEPTS must resolve with `late=0` -- the unused-local and
unused-import diagnostics, which src/tychoc.c drains only after resolve succeeds
and which therefore only mean anything on a file it accepts.

Phase 5b added leg8, and it is the one leg here whose subject is not a VERDICT.
Every diagnostic now carries `file:line`, and a line that is present but WRONG
satisfies "the AST has a line field" while failing Phase 9, which pins message
text byte for byte -- legs 5 and 6 are green either way, because the file is
refused with or without the right number in it. So each NAME file's location is
compared against ./tychoc's for the same input. It found 7 on its first run: the
six parse-time declaration categories were being registered in ARRAY order, so
of two colliding declarations tychoc1 named the wrong one.

Phase 5d added leg8b beside it, one step further in: a line that agrees says
nothing about the WORDS, and a NAME file is refused either way.
"""
import os, re, subprocess, sys

sys.path.insert(0, "scripts")
import classify_rejects as C

ROOTS = ("tests", "corelib", "tools", "examples", "server", "bench")
EXPECT = 1338          # a leg that scores 0 of 0 is green by accident
                       # 1308 -> 1311: the three V2 `packed` fixtures (2026-09-04)
                       # 1311 -> 1319: the eight V2b bytes-bridge fixtures
                       # 1330 -> 1331: tests/reject/fstring_hole_name.ty
                       # 1336 -> 1338: the two struct/enum `$Name` typaram fixtures

# NAME fixtures whose diagnostic carries no file:line in EITHER compiler, because
# merge_pkg (src/tychoc.c:14408, :14234) names the offending FILE and exits. Both
# are scored as agreeing that there is no location; anything else unlocated is a
# skip and reddens the lane.
NO_LINE = {
    "tests/reject/pkg/pkg_header_missing/main.ty",
    "tests/reject/pkg/pkg_header_mismatch/main.ty",
}
SITES = C.load_sites()
# The fixtures `--typecheck` does not refuse -- the same list compiler/run.sh
# carries, with the same reasons. Phase 6b turned `--typecheck` into the whole
# semantic check, so SEMANTIC is scored as a refusal here too.
#
# An entry that no longer misses is not merely untidy: line 166 below skips a
# KNOWN file when scoring leg11, so a stale name silently drops that file's
# `file:line` agreement with ./tychoc out of the score. Re-measured
# 2026-09-03 with `./tychoc1 <f> --typecheck` over every entry -- 17 of the 25
# were rejecting and were deleted. leg10b below is what stops that recurring:
# the set is compared against the entries that ACTUALLY missed this run, the way
# compiler/run.sh compares its own, so a fixed fixture reddens the lane.
KNOWN_TYPE_MISS = {
    "tests/reject/generic_recur_grow.ty",
    # THE PENDING-TYPE RULES left this list when src/tychoc.c's B-3 grounding
    # walk (:5879, :6108, :8909) was ported to compiler/types/tcheck.ty as a
    # `pend` LIST with a line, a done flag and a per-block mark -- the `pendarr`
    # SET R21f-11 built could carry none of the three.
    # THE CONCURRENCY RULES left this list under R21c, which ported the five
    # spawn legs (src/tychoc.c:6047-6058) and wait's argument (:6776) into
    # compiler/types/tcheck.ty in the bootstrap's own order.
}
LIT = lambda f: len(re.sub(r"%[-0-9.*]*(?:ll)?[a-zA-Z]", "", f))

# leg6b -- compiler/run.sh sets no TYCHO_CORELIB, so every other leg here runs
# tychoc1 with the argv0-relative `./corelib` fallback. `corelib/run.sh:5`
# exports an ABSOLUTE root, and that is the one configuration in which the
# under_corelib divergence R7d fixed could fire: tychoc1 compared the package
# dir to the root LEXICALLY, so an absolute root against a relative entry path
# said "not under corelib", the unused-local exemption was lost, and the whole
# compiler lane was blind to it for four commits. So resolution is scored a
# SECOND time under an absolute root and the two must agree, verdict and late
# count alike.
#
# R7f: "anything else keyed on the corelib root is covered by the same
# differential" was an ASSUMPTION, and the wrong one -- resolve is not the only
# pass that can read the root. leg6c and leg10c score --parse and --typecheck
# under the same absolute root, on the VERDICT and on the first diagnostic line,
# so a root-keyed rule firing at either of those passes reddens too.
# Measured while adding them: `--parse` today CANNOT diverge -- driver.ty@parse_only
# takes no argv0, builds no Loader and reads no root, it lexes and parses one file.
# leg6c is therefore insurance against that changing, at one extra run per file.
# `--typecheck` is the opposite: driver.ty@typecheck_only calls load_into + resolve
# before types.check, so it reads the root on every file, and leg10c is a real leg.
ABS_ENV = dict(os.environ, TYCHO_CORELIB=os.path.abspath("corelib"))
late_of = lambda r: next((w for w in r.stdout.split() if w.startswith("late=")), "late=?")
# First diagnostic line, so a root-keyed rule that changed the MESSAGE without
# changing the verdict still reddens leg6c/leg10c.
err1 = lambda r: next((l.strip() for l in r.stderr.split("\n") if ": error: " in l), "")


def msg1(r):
    """tychoc1's first diagnostic MESSAGE, stripped of location and driver prefix
    exactly as tychoc_verdict strips tychoc's -- so the two are comparable. The
    `tychoc1: <msg>` form with no `error:` is merge_pkg's, and it is a message
    like any other; matching only the located form scored those two as an empty
    string and made a byte-identical pair look like a divergence."""
    for ln in r.stderr.split("\n"):
        m = re.match(r"^(?:tychoc1: )?(?:(?:\S+?(?::\d+)?): )?error: (.*)$", ln)
        if m:
            return m.group(1).strip()
        if ln.startswith("tychoc1: ") and ": error:" not in ln:
            return ln[9:].strip()
    return ""


def tychoc_verdict(path):
    r = subprocess.run(["./tychoc", path, "--emit-c", "-o", "/tmp/_vd_out"],
                       capture_output=True, text=True)
    if r.returncode == 0:
        return "ACCEPT", "", ""
    msg = loc = ""
    for ln in r.stderr.split("\n"):
        m = re.match(r"^(?:tychoc: )?(?:(\S+?(?::\d+)?): )?error: (.*)$", ln)
        if m:
            loc = m.group(1) or ""
            msg = m.group(2).strip()
            break
        if ln.startswith("tychoc: ") and ": error:" not in ln:
            msg = ln[8:].strip()
            break
    hits = [s for s in SITES if s[2].match(msg) and LIT(s[1]) >= 8]
    if not hits:
        return (C.fallback(msg) or "UNMATCHED"), msg, loc
    best = max(hits, key=lambda h: LIT(h[1]))
    return C.classify_site(best[0], best[1]), msg, loc


def main():
    files = sorted(os.path.join(d, n)
                   for root in ROOTS for d, _, ns in os.walk(root)
                   for n in ns if n.endswith(".ty"))
    n = {"ACCEPT": 0, "SEMANTIC": 0, "NAME": 0, "TYPE": 0, "SYNTAX": 0, "UNMATCHED": 0}
    bad = rbad = lbad = tbad = abad = pabad = tabad = 0
    known_hit = set()
    lok = lbad2 = lskip = 0
    mok = mbad = 0
    nolinehit = set()
    tok = tbad2 = tskip = 0
    sixb = 0
    ntype_scored = 0
    for f in files:
        cls, msg, loc = tychoc_verdict(f)
        n[cls] += 1
        p = subprocess.run(["./tychoc1", f, "--parse"], capture_output=True, text=True)
        parsed = p.returncode == 0
        pa = subprocess.run(["./tychoc1", f, "--parse"], capture_output=True,
                            text=True, env=ABS_ENV)
        if (pa.returncode == 0) != parsed or err1(pa) != err1(p):
            pabad += 1
            print("  ABS-ROOT-PARSE-DISAGREE %s relative=(rc=%d %s) absolute=(rc=%d %s)"
                  % (f, p.returncode, err1(p)[:60], pa.returncode, err1(pa)[:60]))
        if cls == "UNMATCHED":
            print("  UNMATCHED %s :: %s" % (f, msg[:100]))
            bad += 1
            continue
        if parsed != (cls != "SYNTAX"):
            bad += 1
            print("  DISAGREE %s tychoc=%s tychoc1=%s :: %s"
                  % (f, cls, "accept" if parsed else "reject",
                     (msg or p.stderr.split("\n")[0]).strip()[:90]))
        q = subprocess.run(["./tychoc1", f, "--resolve"], capture_output=True, text=True)
        resolved = q.returncode == 0
        if resolved != (cls not in ("SYNTAX", "NAME")):
            rbad += 1
            print("  RESOLVE-DISAGREE %s tychoc=%s tychoc1=%s :: %s"
                  % (f, cls, "accept" if resolved else "reject",
                     (msg or q.stderr.split("\n")[0]).strip()[:90]))
        qa = subprocess.run(["./tychoc1", f, "--resolve"], capture_output=True,
                            text=True, env=ABS_ENV)
        if (qa.returncode == 0) != resolved or late_of(qa) != late_of(q):
            abad += 1
            print("  ABS-ROOT-DISAGREE %s relative=(rc=%d %s) absolute=(rc=%d %s) :: %s"
                  % (f, q.returncode, late_of(q), qa.returncode, late_of(qa),
                     qa.stderr.split("\n")[0].strip()[:90]))
        if cls == "NAME":
            m = re.search(r"^(\S+?(?::\d+)?): error: ", q.stderr, re.M)
            mine = m.group(1) if m else ""
            if not loc and not mine and f in NO_LINE:
                # merge_pkg's two package-header diagnostics report on a FILE, not
                # on a line, in BOTH compilers -- there is no location to compare,
                # and agreeing that there is none is the whole verdict here. Named
                # rather than blanket, so a NEW unlocated NAME file still reddens;
                # NO_LINE_UNUSED below reddens when one of these gains a line.
                lok += 1
                nolinehit.add(f)
            elif not loc or not mine:
                lskip += 1
                print("  LINE-UNLOCATED %s tychoc=%r tychoc1=%r" % (f, loc, mine))
            elif mine != loc:
                lbad2 += 1
                print("  LINE-DISAGREE %s tychoc=%s tychoc1=%s" % (f, loc, mine))
            else:
                lok += 1
            # Phase 5d: the LINE agreeing says nothing about the words. Both
            # compilers refuse a NAME file with or without the right sentence in
            # it, so legs 5, 6 and 8 stay green while the text drifts -- which is
            # how `(<kind> in <file>)` sat on seven resolve.ty sites unseen.
            his = msg1(q)
            if his == msg:
                mok += 1
            else:
                mbad += 1
                print("  MSG-DISAGREE %s\n    tychoc : %s\n    tychoc1: %s" % (f, msg, his))
        t = subprocess.run(["./tychoc1", f, "--typecheck"], capture_output=True, text=True)
        typed = t.returncode == 0
        ta = subprocess.run(["./tychoc1", f, "--typecheck"], capture_output=True,
                            text=True, env=ABS_ENV)
        if (ta.returncode == 0) != typed or err1(ta) != err1(t):
            tabad += 1
            print("  ABS-ROOT-TYPECHECK-DISAGREE %s relative=(rc=%d %s) absolute=(rc=%d %s)"
                  % (f, t.returncode, err1(t)[:60], ta.returncode, err1(ta)[:60]))
        if typed != (cls == "ACCEPT"):
            if f in KNOWN_TYPE_MISS:
                known_hit.add(f)          # the entry is still earning its exemption
            else:
                tbad += 1
                print("  TYPE-DISAGREE %s tychoc=%s tychoc1=%s :: %s"
                      % (f, cls, "accept" if typed else "reject",
                         (msg or t.stderr.split("\n")[0]).strip()[:90]))
        if cls == "ACCEPT" and typed and " sixb=1" in t.stdout:
            sixb += 1
        if cls == "TYPE" and f not in KNOWN_TYPE_MISS:
            ntype_scored += 1
            m = re.search(r"^(\S+?(?::\d+)?): error: ", t.stderr, re.M)
            mine = m.group(1) if m else ""
            if not loc or not mine:
                tskip += 1
                print("  TYPE-LINE-UNLOCATED %s tychoc=%r tychoc1=%r" % (f, loc, mine))
            elif mine != loc:
                tbad2 += 1
                print("  TYPE-LINE-DISAGREE %s tychoc=%s tychoc1=%s" % (f, loc, mine))
            else:
                tok += 1
        if cls == "ACCEPT" and resolved:
            late = [w for w in q.stdout.split() if w.startswith("late=")]
            if late and late[0] != "late=0":
                lbad += 1
                print("  LATE %s %s :: %s" % (f, late[0], q.stderr.split("\n")[0][:90]))
    print("leg5  whole-tree verdicts: files=%d tychoc(accept=%d semantic=%d name=%d type=%d syntax=%d) disagreements=%d"
          % (len(files), n["ACCEPT"], n["SEMANTIC"], n["NAME"], n["TYPE"], n["SYNTAX"], bad))
    print("leg6  whole-tree resolution: disagreements=%d unused-local/import on an accepted file=%d"
          % (rbad, lbad))
    print("leg6b whole-tree resolution under an ABSOLUTE TYCHO_CORELIB (%s): disagreements=%d"
          % (ABS_ENV["TYCHO_CORELIB"], abad))
    print("leg6c whole-tree PARSE under the same absolute root: disagreements=%d" % pabad)
    print("leg8  NAME diagnostic file:line vs ./tychoc: scored=%d agree=%d disagree=%d unlocated=%d"
          % (lok + lbad2 + lskip, lok, lbad2, lskip))
    print("leg8b NAME diagnostic MESSAGE vs ./tychoc: scored=%d agree=%d disagree=%d"
          % (mok + mbad, mok, mbad))
    print("leg10 whole-tree typecheck: disagreements=%d (%d TYPE files known-missed)"
          % (tbad, len(KNOWN_TYPE_MISS)))
    print("leg10c whole-tree TYPECHECK under the same absolute root: disagreements=%d" % tabad)
    print("leg11 TYPE diagnostic file:line vs ./tychoc: scored=%d agree=%d disagree=%d unlocated=%d"
          % (tok + tbad2 + tskip, tok, tbad2, tskip))
    print("leg12 accepted programs with a generic/newtype/handle/bounded construct this pass "
          "could NOT ground: %d of %d -- Phase 6a counted every program that DECLARED one "
          "(202), which no amount of work could drive down; the flag now marks an "
          "instantiation, a generic return or a bounded capacity actually left `?`"
          % (sixb, n["ACCEPT"]))
    stale = sorted(KNOWN_TYPE_MISS - known_hit)
    print("leg10b KNOWN_TYPE_MISS entries still missing: %d of %d"
          % (len(known_hit), len(KNOWN_TYPE_MISS)))
    for f in stale:
        print("  STALE-EXEMPTION %s :: --typecheck no longer misses it (or the file is gone)" % f)
    if stale:
        print("parse-check: %d KNOWN_TYPE_MISS entries are stale -- delete them" % len(stale))
        return 1
    if tok + tbad2 + tskip != ntype_scored:
        print("parse-check: leg11 scored %d of %d TYPE files"
              % (tok + tbad2 + tskip, ntype_scored))
        return 1
    if nolinehit != NO_LINE:
        print("parse-check: NO_LINE entries that are now located -- delete them: %s"
              % " ".join(sorted(NO_LINE - nolinehit)))
        return 1
    if lok + lbad2 + lskip != n["NAME"]:
        print("parse-check: leg8 scored %d of %d NAME files" % (lok + lbad2 + lskip, n["NAME"]))
        return 1
    if mok + mbad != n["NAME"]:
        print("parse-check: leg8b scored %d of %d NAME files" % (mok + mbad, n["NAME"]))
        return 1
    if len(files) != EXPECT:
        print("parse-check: the tree is %d .ty files, expected %d" % (len(files), EXPECT))
        return 1
    return 1 if (bad or rbad or lbad or lbad2 or lskip or mbad or tbad or tbad2
                  or tskip or abad or pabad or tabad) else 0


if __name__ == "__main__":
    sys.exit(main())
