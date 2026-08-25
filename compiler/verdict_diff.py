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
"""
import os, re, subprocess, sys

sys.path.insert(0, "scripts")
import classify_rejects as C

ROOTS = ("tests", "corelib", "tools", "examples", "server", "bench")
EXPECT = 1080          # a leg that scores 0 of 0 is green by accident
SITES = C.load_sites()
# The fixtures `--typecheck` does not refuse -- the same list compiler/run.sh
# carries, with the same reasons. Phase 6b turned `--typecheck` into the whole
# semantic check, so SEMANTIC is scored as a refusal here too.
KNOWN_TYPE_MISS = {
    "tests/reject/generic_recur_grow.ty",
    "tests/reject/infer_bare_empty.ty",
    "tests/reject/infer_use_before_ground.ty",
    "tests/reject/len_scalar.ty",
    "tests/reject/typeset_notin.ty",
    "tests/reject/void_grounds_pending_push.ty",
    # THE CONCURRENCY RULES, a family of their own: what `spawn` may be applied
    # to, `parallel for`'s reduction and control-flow rules, capturing a task,
    # and wait's argument. None of them is a type rule -- each is a statement
    # SHAPE -- and they are Phase 6c's, named there.
    "tests/conc/reject/builtin.ty",
    "tests/conc/reject/capture.ty",
    "tests/conc/reject/closure.ty",
    "tests/conc/reject/inout.ty",
    "tests/conc/reject/parfor_assign.ty",
    "tests/conc/reject/parfor_break.ty",
    "tests/conc/reject/parfor_push.ty",
    "tests/conc/reject/parfor_read_acc.ty",
    "tests/conc/reject/parfor_return.ty",
    "tests/conc/reject/select_return_in_parfor.ty",
    "tests/conc/reject/wait_nontask.ty",
    # a match ARM naming a foreign package's variant, or a Result arm written
    # with one: the arm-name rules, also Phase 6c
    "tests/reject/pkg/foreign_variant_bare/main.ty",
    "tests/reject/pkg/foreign_variant_is/main.ty",
    "tests/reject/pkg/result_arm_mangled/main.ty",
    # the same rule, reached only through an INSTANTIATED generic: the error is
    # in the template body and only a bound $T makes it one
    "tests/diag/generic_inst_name.ty",
    "tests/reject/pkg/generic_inst_callsite/helper.ty",
    "tests/reject/pkg/generic_inst_callsite/main.ty",
    "tests/reject/pkg/generic_inst_srcfile/helper.ty",
    "tests/reject/pkg/generic_inst_srcfile/main.ty",
}
LIT = lambda f: len(re.sub(r"%[-0-9.*]*(?:ll)?[a-zA-Z]", "", f))


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
    needs = any(k in best[1] for k in C.NEEDS_SYMBOLS)
    if best[0] < C.PARSE_END and not needs:
        return "SYNTAX", msg, loc
    if any(k in best[1] for k in C.NAME_SITES):
        return "NAME", msg, loc
    if (any(k in best[1] for k in C.TYPE_SITES)
            and not any(k in best[1] for k in C.TYPE_EXCLUDE)):
        return "TYPE", msg, loc
    return "SEMANTIC", msg, loc


def main():
    files = sorted(os.path.join(d, n)
                   for root in ROOTS for d, _, ns in os.walk(root)
                   for n in ns if n.endswith(".ty"))
    n = {"ACCEPT": 0, "SEMANTIC": 0, "NAME": 0, "TYPE": 0, "SYNTAX": 0, "UNMATCHED": 0}
    bad = rbad = lbad = tbad = 0
    lok = lbad2 = lskip = 0
    tok = tbad2 = tskip = 0
    sixb = 0
    ntype_scored = 0
    for f in files:
        cls, msg, loc = tychoc_verdict(f)
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
        if cls == "NAME":
            m = re.search(r"^(\S+?(?::\d+)?): error: ", q.stderr, re.M)
            mine = m.group(1) if m else ""
            if not loc or not mine:
                lskip += 1
                print("  LINE-UNLOCATED %s tychoc=%r tychoc1=%r" % (f, loc, mine))
            elif mine != loc:
                lbad2 += 1
                print("  LINE-DISAGREE %s tychoc=%s tychoc1=%s" % (f, loc, mine))
            else:
                lok += 1
        t = subprocess.run(["./tychoc1", f, "--typecheck"], capture_output=True, text=True)
        typed = t.returncode == 0
        if typed != (cls == "ACCEPT"):
            if f not in KNOWN_TYPE_MISS:
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
    print("leg8  NAME diagnostic file:line vs ./tychoc: scored=%d agree=%d disagree=%d unlocated=%d"
          % (lok + lbad2 + lskip, lok, lbad2, lskip))
    print("leg10 whole-tree typecheck: disagreements=%d (%d TYPE files known-missed)"
          % (tbad, len(KNOWN_TYPE_MISS)))
    print("leg11 TYPE diagnostic file:line vs ./tychoc: scored=%d agree=%d disagree=%d unlocated=%d"
          % (tok + tbad2 + tskip, tok, tbad2, tskip))
    print("leg12 accepted programs with a generic/newtype/handle/bounded construct this pass "
          "could NOT ground: %d of %d -- Phase 6a counted every program that DECLARED one "
          "(202), which no amount of work could drive down; the flag now marks an "
          "instantiation, a generic return or a bounded capacity actually left `?`"
          % (sixb, n["ACCEPT"]))
    if tok + tbad2 + tskip != ntype_scored:
        print("parse-check: leg11 scored %d of %d TYPE files"
              % (tok + tbad2 + tskip, ntype_scored))
        return 1
    if lok + lbad2 + lskip != n["NAME"]:
        print("parse-check: leg8 scored %d of %d NAME files" % (lok + lbad2 + lskip, n["NAME"]))
        return 1
    if len(files) != EXPECT:
        print("parse-check: the tree is %d .ty files, expected %d" % (len(files), EXPECT))
        return 1
    return 1 if (bad or rbad or lbad or lbad2 or lskip or tbad or tbad2 or tskip) else 0


if __name__ == "__main__":
    sys.exit(main())
