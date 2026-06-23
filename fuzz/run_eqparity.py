#!/usr/bin/env python3
# Equality-operand parity for COMPOSITE and NEWTYPE types: tychoc (the C reference
# compiler) and tychoc0 (self-hosted) MUST agree on whether `L == R` / `L != R`
# type-checks (accept) or is rejected, for L/R drawn from arrays, options,
# structs, tuples, maps, scalars, and newtypes.
#
# Why it exists: run_typeparity.py is exhaustive over the SCALAR binary-op matrix,
# but tychoc0's structural-equality codegen checked only the LEFT operand's type,
# so `[int] == 7` and `[int] == [string]` (and every other composite mismatch)
# over-accepted in tychoc0 while tychoc rejects them -- a fail-open invisible to
# the output-only fixpoint differential and to the tychoc-only reject harness.
# This lane is the `==`/`!=` analogue of run_typeparity.py, over composite/newtype
# operands rather than scalars. Deterministic and exhaustive over the operand
# matrix -- no seeds.
#
# A case is (l, r, op): a program `c := <l.var> op <r.var>` over a fixed prelude
# of typed declarations. The PARITY assertion is tychoc == tychoc0; `expect`
# (accept iff the two operands have the same nominal type) is a sanity check that
# the fixture trips the rule as designed in the oracle. A both-accept program must
# also emit C that compiles in both.
#
# DOCUMENTED SKIP -- newtype identity erasure: tychoc0 stores a zero-cost newtype
# as its underlying type (a Meters is just a float at the C level), so it CANNOT
# distinguish `Meters == float` or `Meters == Seconds` the way tychoc does. That
# is a known, accepted less-strict divergence (same precedent as the reject
# harness's newtype skip-list), so pairs whose ERASED bases match but whose
# nominal types differ, with at least one a newtype, are skipped and counted.
#
# Usage: run_eqparity.py        (no seeds -- the matrix is fixed)
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
struct Pr:
    a: int
    b: string
fn main():
    ai := [1, 2]
    af := [1.0]
    asx := ["x"]
    oi := Some(1)
    os := Some("x")
    pt := Pt(1, 2)
    pr := Pr(1, "x")
    ti := (1, 2)
    ts := ("a", "b")
    mi := ["k": 1]
    ms := ["k": "v"]
    n := 7
    f := 1.5
    s := "z"
    sl := Slot(5)
    mt := Meters(2.0)
'''

# (var, nominal type, erased base, is_newtype)
OPERANDS = [
    ("ai",  "[int]",            "[int]",    False),
    ("af",  "[float]",          "[float]",  False),
    ("asx", "[string]",         "[string]", False),
    ("oi",  "Option(int)",      "Option(int)",    False),
    ("os",  "Option(string)",   "Option(string)", False),
    ("pt",  "Pt",               "Pt", False),
    ("pr",  "Pr",               "Pr", False),
    ("ti",  "(int,int)",        "(int,int)",       False),
    ("ts",  "(string,string)",  "(string,string)", False),
    ("mi",  "[string:int]",     "[string:int]",    False),
    ("ms",  "[string:string]",  "[string:string]", False),
    ("n",   "int",    "int",    False),
    ("f",   "float",  "float",  False),
    ("s",   "string", "string", False),
    ("sl",  "Slot",   "int",    True),
    ("mt",  "Meters", "float",  True),
]
OPS = ["==", "!="]

def skip_pair(l, r):
    # documented newtype-erasure divergence: same erased base, different nominal,
    # at least one a newtype -> tychoc0 cannot tell them apart by design.
    return (l[3] or r[3]) and l[2] == r[2] and l[1] != r[1]

def program(lvar, op, rvar):
    return PRELUDE + "    c := %s %s %s\n" % (lvar, op, rvar)

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
        fails = []   # (label, kind, detail, program)
        for l in OPERANDS:
            for r in OPERANDS:
                if skip_pair(l, r):
                    skipped += 1
                    continue
                for op in OPS:
                    total += 1
                    prog = program(l[0], op, r[0])
                    with open(src, "w") as f:
                        f.write(prog)
                    label = "%s %s %s" % (l[1], op, r[1])
                    expect = "accept" if l[1] == r[1] else "reject"
                    hv, hc = tychoc_verdict(src, os.path.join(tmp, "hc"))
                    zv, zc = tychoc0_verdict(h0, src, os.path.join(tmp, "h0.c"))
                    if hv == "CRASH":
                        fails.append((label, "tychoc CRASH", "", prog)); continue
                    if zv == "CRASH":
                        fails.append((label, "tychoc0 CRASH", "", prog)); continue
                    if hv != expect:
                        fails.append((label, "FIXTURE DRIFT", "tychoc=%s expected=%s" % (hv, expect), prog)); continue
                    if hv != zv:
                        fails.append((label, "ACCEPT/REJECT DIVERGENCE",
                                      "tychoc=%s tychoc0=%s" % (hv, zv), prog)); continue
                    if hv == "accept" and not c_compiles(hc):
                        fails.append((label, "tychoc accepted, emitted C does not compile", "", prog)); continue
                    if zv == "accept" and not c_compiles(zc):
                        fails.append((label, "tychoc0 accepted, emitted C does not compile", "", prog)); continue
        if fails:
            print("EQ-PARITY FAIL: %d/%d cases diverge (%d newtype-erasure pairs skipped)\n" % (len(fails), total, skipped))
            seen = set()
            for i, (label, kind, detail, prog) in enumerate(fails):
                print("  [%s]  %s   %s" % (kind, label, detail))
                tag = (kind + label).replace(" ", "_").replace("/", "").replace("(", "").replace(")", "").replace(",", "").replace(":", "")
                if tag in seen:
                    continue
                seen.add(tag)
                with open(os.path.join(FINDINGS, "eqparity_%s.ty" % tag[:60]), "w") as f:
                    f.write("# %s -- %s %s\n%s" % (kind, label, detail, prog))
            print("\nfindings saved in fuzz/findings/eqparity_*.ty")
            sys.exit(1)
        print("eq-parity: %d/%d composite/newtype ==,!= cases AGREE "
              "(accept/reject + emitted C; %d newtype-erasure pairs skipped) -- tychoc == tychoc0"
              % (total, total, skipped))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

if __name__ == "__main__":
    main()
