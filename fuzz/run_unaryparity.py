#!/usr/bin/env python3
# Unary-operator typing: `-x`, `~x`, `not x` must type-check (accept) or be
# rejected exactly as the `expect` oracle says, for x drawn from every
# scalar/composite/newtype operand. tychoc's unary rules are STRICTER than the
# binary-arithmetic rules:
#   -x   : base must be int/float        (numeric newtype ok: -Meters is a Meters)
#   ~x   : type must be EXACTLY int      (so ~Slot, a newtype-int, is rejected)
#   not x: type must be bool
#
# A case is (op, operand): a program `c := OP x` over a fixed prelude of typed
# decls. `expect` (those rules, encoded below) is the oracle. An accepted program
# must also emit C that compiles.
#
# HISTORY -- THE PARITY ASSERTION WAS RETIRED 2026-07-29. Until then this lane was
# also a DIFFERENTIAL against the frozen self-hosted tychoc0, and that side was
# the sharper one: tychoc0 DESUGARS `-x`->(0-x) and `~x`->((0-x)-1) at parse, so
# unary fell under its permissive arithmetic rules (char +/- int, int-literal ->
# float) and over-accepted `~float`, `~char`, `-char`. Comparing two
# implementations that reach the answer by different routes is what made those
# fail-opens visible.
#
# WHY IT WENT: compiler/tychoc0.ty is FROZEN, and the breaking loop-syntax change
# of 2026-07-29 (three-clause `for` and bare `for:` replace `for i in range(...)`,
# `range` deleted) means it can no longer parse the corpus, so no lane builds it.
# See compiler/fixpoint.sh's header, ROADMAP.md and docs/architecture.md.
#
# WHAT IS LOST: the second route to the answer. The `expect` oracle is written
# down in this file and still gates tychoc, so a regression AWAY from the stated
# rules still reddens; what no longer reddens is a wrong rule that the oracle and
# the compiler agree on.
#
# Usage: run_unaryparity.py        (no seeds -- the matrix is fixed)
import os, subprocess, sys, tempfile, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TYCHOC = os.path.join(REPO, "tychoc")
FINDINGS = os.path.join(REPO, "fuzz", "findings")
RUN_TIMEOUT = 30

PRELUDE = '''\
type Slot = int
type Meters = float
struct Pt:
    x: int
    y: int
fn main():
    n := 7
    f := 1.5
    ch := 'A'
    s := "z"
    b := true
    ai := [1, 2]
    oi := Some(1)
    pt := Pt(1, 2)
    sl := Slot(5)
    mt := Meters(2.0)
'''

# (var, nominal label, erased base, is_newtype)
OPERANDS = [
    ("n",  "int",          "int",    False),
    ("f",  "float",        "float",  False),
    ("ch", "char",         "char",   False),
    ("s",  "string",       "string", False),
    ("b",  "bool",         "bool",   False),
    ("ai", "[int]",        "[int]",  False),
    ("oi", "Option(int)",  "Option(int)", False),
    ("pt", "Pt",           "Pt",     False),
    ("sl", "Slot",         "int",    True),
    ("mt", "Meters",       "float",  True),
]
OPS = ["-", "~", "not"]

def expr(op, var):
    return ("not " + var) if op == "not" else (op + var)

def expect(op, operand):
    # tychoc's rule. operand = (var, label, erased, is_nt)
    if op == "-":
        return "accept" if operand[2] in ("int", "float") else "reject"
    if op == "~":
        return "accept" if (operand[1] == "int") else "reject"   # EXACTLY int, no newtype
    return "accept" if operand[1] == "bool" else "reject"          # not

def skip_case(op, operand):
    # No skips: tychoc0 now tracks newtype identity, so `~<newtype-over-int>` is
    # rejected (a newtype is not EXACTLY int) exactly as tychoc rejects it. (Was an
    # unavoidable erasure divergence, now closed.)
    return False

def program(op, var):
    return PRELUDE + "    c := %s\n" % expr(op, var)

def classify(rc):
    if rc < 0:
        return "CRASH"
    return "accept" if rc == 0 else "reject"

def tychoc_verdict(src, base):
    r = subprocess.run([TYCHOC, src, "--emit-c", "-o", base],
                       capture_output=True, text=True, timeout=RUN_TIMEOUT)
    v = classify(r.returncode)
    c = base + ".c" if v == "accept" and os.path.exists(base + ".c") else None
    return v, c

def c_compiles(cpath):
    if not cpath or not os.path.exists(cpath) or os.path.getsize(cpath) == 0:
        return True
    r = subprocess.run(["cc", "-fsyntax-only", "-std=c11", "-w", cpath],
                       capture_output=True, text=True, timeout=RUN_TIMEOUT)
    return r.returncode == 0

def main():
    if not os.path.exists(TYCHOC):
        print("run 'make' first (no ./tychoc)"); sys.exit(2)
    os.makedirs(FINDINGS, exist_ok=True)
    tmp = tempfile.mkdtemp()
    try:
        src = os.path.join(tmp, "p.ty")
        total = 0; skipped = 0
        fails = []
        for op in OPS:
            for operand in OPERANDS:
                if skip_case(op, operand):
                    skipped += 1
                    continue
                total += 1
                prog = program(op, operand[0])
                with open(src, "w") as f:
                    f.write(prog)
                label = "%s %s" % (op, operand[1])
                exp = expect(op, operand)
                hv, hc = tychoc_verdict(src, os.path.join(tmp, "hc"))
                if hv == "CRASH":
                    fails.append((label, "tychoc CRASH", "", prog)); continue
                if hv != exp:
                    fails.append((label, "ORACLE DIVERGENCE", "tychoc=%s expected=%s" % (hv, exp), prog)); continue
                if hv == "accept" and not c_compiles(hc):
                    fails.append((label, "tychoc accepted, emitted C does not compile", "", prog)); continue
        if fails:
            print("UNARY-PARITY FAIL: %d/%d cases diverge (%d skipped)\n" % (len(fails), total, skipped))
            for i, (label, kind, detail, prog) in enumerate(fails):
                print("  [%s]  %s   %s" % (kind, label, detail))
                tag = (label).replace(" ", "_").replace("~", "tilde").replace("-", "neg").replace("[", "").replace("]", "").replace("(", "").replace(")", "")
                with open(os.path.join(FINDINGS, "unaryparity_%s.ty" % tag), "w") as f:
                    f.write("# %s -- %s %s\n%s" % (kind, label, detail, prog))
            print("\nfindings saved in fuzz/findings/unaryparity_*.ty")
            sys.exit(1)
        print("unary-parity: %d/%d unary-operator cases match the oracle "
              "(accept/reject + emitted C; newtype identity enforced, 0 skipped).\n"
              "              NOTE: the tychoc0 differential was retired 2026-07-29 -- "
              "see this file's header." % (total, total))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

if __name__ == "__main__":
    main()
