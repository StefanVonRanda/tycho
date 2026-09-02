import os, subprocess, sys, tempfile, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The SHIPPED compiler, not the C bootstrap: every other gate runs tychoc1,
# and checks present in tychoc are absent from it. Override with TYCHOC=.
TYCHOC = os.environ.get("TYCHOC") or os.path.join(REPO, "tychoc1")
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
    _ai := [1, 2]
    _af := [1.0]
    _asx := ["x"]
    _oi := Some(1)
    _os := Some("x")
    _pt := Pt(1, 2)
    _pr := Pr(1, "x")
    _ti := (1, 2)
    _ts := ("a", "b")
    _mi := ["k": 1]
    _ms := ["k": "v"]
    _n := 7
    _f := 1.5
    _s := "z"
    _sl := Slot(5)
    _mt := Meters(2.0)
'''

# (var, nominal type, erased base, is_newtype)
OPERANDS = [
    ("_ai",  "[int]",            "[int]",    False),
    ("_af",  "[float]",          "[float]",  False),
    ("_asx", "[string]",         "[string]", False),
    ("_oi",  "Option(int)",      "Option(int)",    False),
    ("_os",  "Option(string)",   "Option(string)", False),
    ("_pt",  "Pt",               "Pt", False),
    ("_pr",  "Pr",               "Pr", False),
    ("_ti",  "(int,int)",        "(int,int)",       False),
    ("_ts",  "(string,string)",  "(string,string)", False),
    ("_mi",  "[string:int]",     "[string:int]",    False),
    ("_ms",  "[string:string]",  "[string:string]", False),
    ("_n",   "int",    "int",    False),
    ("_f",   "float",  "float",  False),
    ("_s",   "string", "string", False),
    ("_sl",  "Slot",   "int",    True),
    ("_mt",  "Meters", "float",  True),
]
OPS = ["==", "!="]

def skip_pair(l, r):
    return False

def program(lvar, op, rvar):
    return PRELUDE + "    _c := %s %s %s\n" % (lvar, op, rvar)

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
              "see this file's header." % (total, total))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

if __name__ == "__main__":
    main()
