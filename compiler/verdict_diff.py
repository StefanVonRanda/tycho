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
"""
import os, re, subprocess, sys

sys.path.insert(0, "scripts")
import classify_rejects as C

ROOTS = ("tests", "corelib", "tools", "examples", "server", "bench")
EXPECT = 1078          # a leg that scores 0 of 0 is green by accident
SITES = C.load_sites()
LIT = lambda f: len(re.sub(r"%[-0-9.*]*(?:ll)?[a-zA-Z]", "", f))


def tychoc_verdict(path):
    r = subprocess.run(["./tychoc", path, "--emit-c", "-o", "/tmp/_vd_out"],
                       capture_output=True, text=True)
    if r.returncode == 0:
        return "ACCEPT", ""
    msg = ""
    for ln in r.stderr.split("\n"):
        m = re.match(r"^(?:tychoc: )?(?:\S+?(?::\d+)?: )?error: (.*)$", ln)
        if m:
            msg = m.group(1).strip()
            break
        if ln.startswith("tychoc: ") and ": error:" not in ln:
            msg = ln[8:].strip()
            break
    hits = [s for s in SITES if s[2].match(msg) and LIT(s[1]) >= 8]
    if not hits:
        return (C.fallback(msg) or "UNMATCHED"), msg
    best = max(hits, key=lambda h: LIT(h[1]))
    needs = any(k in best[1] for k in C.NEEDS_SYMBOLS)
    if best[0] < C.PARSE_END and not needs:
        return "SYNTAX", msg
    if any(k in best[1] for k in C.NAME_SITES):
        return "NAME", msg
    return "SEMANTIC", msg


def main():
    files = sorted(os.path.join(d, n)
                   for root in ROOTS for d, _, ns in os.walk(root)
                   for n in ns if n.endswith(".ty"))
    n = {"ACCEPT": 0, "SEMANTIC": 0, "NAME": 0, "SYNTAX": 0, "UNMATCHED": 0}
    bad = rbad = lbad = 0
    for f in files:
        cls, msg = tychoc_verdict(f)
        n[cls] += 1
        p = subprocess.run(["./tychoc1", f, "--parse"], capture_output=True, text=True)
        parsed = p.returncode == 0
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
        if cls == "ACCEPT" and resolved:
            late = [w for w in q.stdout.split() if w.startswith("late=")]
            if late and late[0] != "late=0":
                lbad += 1
                print("  LATE %s %s :: %s" % (f, late[0], q.stderr.split("\n")[0][:90]))
    print("leg5  whole-tree verdicts: files=%d tychoc(accept=%d semantic=%d name=%d syntax=%d) disagreements=%d"
          % (len(files), n["ACCEPT"], n["SEMANTIC"], n["NAME"], n["SYNTAX"], bad))
    print("leg6  whole-tree resolution: disagreements=%d unused-local/import on an accepted file=%d"
          % (rbad, lbad))
    if len(files) != EXPECT:
        print("parse-check: the tree is %d .ty files, expected %d" % (len(files), EXPECT))
        return 1
    return 1 if (bad or rbad or lbad) else 0


if __name__ == "__main__":
    sys.exit(main())
