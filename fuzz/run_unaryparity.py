#!/usr/bin/env python3
# Unary-operator parity: tychoc (the C reference compiler) and tychoc0 (self-
# hosted) MUST agree on whether `-x`, `~x`, `not x` type-check (accept) or are
# rejected, for x drawn from every scalar/composite/newtype operand.
#
# Why it exists: the parity-lane family (run_typeparity.py scalar binops,
# run_eqparity.py composite ==) left unary operators uncovered, and the fixpoint
# differential is OUTPUT-only so it can't see an accept/reject divergence.
# tychoc's unary rules are STRICTER than the binary-arithmetic rules:
#   -x   : base must be int/float        (numeric newtype ok: -Meters is a Meters)
#   ~x   : type must be EXACTLY int      (so ~Slot, a newtype-int, is rejected)
#   not x: type must be bool
# tychoc0 DESUGARS `-x`->(0-x) and `~x`->((0-x)-1) at parse, so they fall under
# the permissive arithmetic rules (char +/- int, int-literal -> float), which
# over-accept `~float`, `~char`, `-char`, ... This lane pins those down.
#
# A case is (op, operand): a program `c := OP x` over a fixed prelude of typed
# decls. PARITY (tychoc == tychoc0) is the assertion; `expect` (tychoc's rule,
# encoded below) is a sanity check on the fixture. A both-accept program must
# also emit C that compiles in both.
#
# DOCUMENTED SKIP -- newtype identity erasure: tychoc0 stores a newtype as its
# base, so `~Slot` desugars to int arithmetic and accepts, while tychoc rejects
# it (a Slot is not the literal type int). tychoc0 cannot tell them apart by
# design (same precedent as run_eqparity.py), so `~<newtype-over-int>` is skipped
# and counted. `~<newtype-over-float>` is NOT skipped: ~float is itself illegal,
# so once the float fail-open is closed the newtype-float case rejects too.
#
# Usage: run_unaryparity.py        (no seeds -- the matrix is fixed)
import os, subprocess, sys, tempfile, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TYCHOC = os.path.join(REPO, "tychoc")
HSRC = os.path.join(REPO, "compiler", "tychoc0.ty")
FINDINGS = os.path.join(REPO, "fuzz", "findings")
RUN_TIMEOUT = 30
BUILD_TIMEOUT = 300

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
    # ~<newtype-over-int>: tychoc0 erases to int and accepts (~int is legal),
    # tychoc rejects the newtype. Unavoidable by design -> skip.
    return op == "~" and operand[3] and operand[2] == "int"

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

def tychoc0_verdict(h0, src, cpath):
    with open(src, "rb") as fi:
        r = subprocess.run([h0], stdin=fi, capture_output=True, timeout=RUN_TIMEOUT)
    v = classify(r.returncode)
    if v == "accept" and r.stdout:
        with open(cpath, "wb") as fo:
            fo.write(r.stdout)
        return v, cpath
    return v, None

def c_compiles(cpath):
    if not cpath or not os.path.exists(cpath) or os.path.getsize(cpath) == 0:
        return True
    r = subprocess.run(["cc", "-fsyntax-only", "-std=c11", "-w", cpath],
                       capture_output=True, text=True, timeout=RUN_TIMEOUT)
    return r.returncode == 0

def build_tychoc0(tmp):
    base = os.path.join(tmp, "h0src")
    r = subprocess.run([TYCHOC, HSRC, "--emit-c", "-o", base],
                       capture_output=True, text=True, timeout=BUILD_TIMEOUT)
    if r.returncode != 0 or not os.path.exists(base + ".c"):
        print("FATAL: tychoc could not emit tychoc0 C:\n" + r.stderr[:1500]); sys.exit(2)
    exe = os.path.join(tmp, "tychoc0")
    b = subprocess.run(["cc", "-O2", "-std=c11", "-pthread", base + ".c", "-o", exe, "-lm"],
                       capture_output=True, text=True, timeout=BUILD_TIMEOUT)
    if b.returncode != 0:
        print("FATAL: tychoc0 C did not compile:\n" + b.stderr[:1500]); sys.exit(2)
    return exe

def main():
    if not os.path.exists(TYCHOC):
        print("run 'make' first (no ./tychoc)"); sys.exit(2)
    os.makedirs(FINDINGS, exist_ok=True)
    tmp = tempfile.mkdtemp()
    try:
        h0 = build_tychoc0(tmp)
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
                zv, zc = tychoc0_verdict(h0, src, os.path.join(tmp, "h0.c"))
                if hv == "CRASH":
                    fails.append((label, "tychoc CRASH", "", prog)); continue
                if zv == "CRASH":
                    fails.append((label, "tychoc0 CRASH", "", prog)); continue
                if hv != exp:
                    fails.append((label, "FIXTURE DRIFT", "tychoc=%s expected=%s" % (hv, exp), prog)); continue
                if hv != zv:
                    fails.append((label, "ACCEPT/REJECT DIVERGENCE",
                                  "tychoc=%s tychoc0=%s" % (hv, zv), prog)); continue
                if hv == "accept" and not c_compiles(hc):
                    fails.append((label, "tychoc accepted, emitted C does not compile", "", prog)); continue
                if zv == "accept" and not c_compiles(zc):
                    fails.append((label, "tychoc0 accepted, emitted C does not compile", "", prog)); continue
        if fails:
            print("UNARY-PARITY FAIL: %d/%d cases diverge (%d newtype-erasure skipped)\n" % (len(fails), total, skipped))
            for i, (label, kind, detail, prog) in enumerate(fails):
                print("  [%s]  %s   %s" % (kind, label, detail))
                tag = (label).replace(" ", "_").replace("~", "tilde").replace("-", "neg").replace("[", "").replace("]", "").replace("(", "").replace(")", "")
                with open(os.path.join(FINDINGS, "unaryparity_%s.ty" % tag), "w") as f:
                    f.write("# %s -- %s %s\n%s" % (kind, label, detail, prog))
            print("\nfindings saved in fuzz/findings/unaryparity_*.ty")
            sys.exit(1)
        print("unary-parity: %d/%d unary-operator cases AGREE "
              "(accept/reject + emitted C; %d newtype-erasure skipped) -- tychoc == tychoc0"
              % (total, total, skipped))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

if __name__ == "__main__":
    main()
