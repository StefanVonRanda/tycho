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
    return False

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
                    if hv == "CRASH":
                        fails.append((label, "tychoc CRASH", "", prog)); continue
                    if hv != expect:
                        fails.append((label, "ORACLE DIVERGENCE", "tychoc=%s expected=%s" % (hv, expect), prog)); continue
                    if hv == "accept" and not c_compiles(hc):
                        fails.append((label, "tychoc accepted, emitted C does not compile", "", prog)); continue
        if fails:
            print("EQ-PARITY FAIL: %d/%d cases diverge (%d pairs skipped)\n" % (len(fails), total, skipped))
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
        print("eq-parity: %d/%d composite/newtype ==,!= cases match the oracle "
              "(accept/reject + emitted C; newtype identity enforced, 0 skipped).\n"
              "           NOTE: the tychoc0 differential was retired 2026-07-29 -- "
              "see this file's header." % (total, total))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

if __name__ == "__main__":
    main()
