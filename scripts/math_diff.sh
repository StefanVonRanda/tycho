#!/bin/sh
# core:math against Python's own arithmetic.
#
# Both defects this package has had were found by READING a claim -- gcd's
# "non-negative" (FRICTION #64) and sign's "-1/0/1" (FRICTION #65) -- and both
# were invisible to every gate in this tree, because `make corelib` compares
# core:math to a golden THIS REPO RECORDED. A golden proves the answers have not
# changed; it cannot say they were ever right, and it agrees with whatever the
# code did on the day it was written. Two defects in one 75-line package is the
# argument for an independent oracle rather than a third careful reading.
#
# Python is that oracle for min/max/clamp/sign/abs/gcd/ipow. Where the semantics
# genuinely differ the oracle encodes the DOCUMENTED tycho answer rather than
# Python's, and says so at the site:
#
#   * Python ints are arbitrary precision, tycho's are int64. gcd(min, 0) and
#     gcd(min, min) are 2^63, which does not fit, and follow abs() in returning
#     min itself -- documented in corelib/math/math.ty, so that is the expectation.
#   * ipow has no overflow guard by design ("caller's job"), so a case whose true
#     answer leaves int64 is SKIPPED rather than scored -- and the skip count is
#     printed, because a silent skip is how a sweep reports covering work it did
#     not do.
#
# Two controls, both required. A deliberately wrong expectation must be CAUGHT,
# or a zero-mismatch run means nothing. And the corpus must actually contain the
# edges it claims to -- asserted by count, not by assumption.
set -eu

cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT INT TERM

command -v python3 >/dev/null 2>&1 || { echo "math-diff: SKIPPED (no python3)"; exit 0; }
[ -x ./tychoc ] || make tychoc >/dev/null

python3 - "$T" <<'PY'
import sys, pathlib
T = pathlib.Path(sys.argv[1])
MIN, MAX = -(2**63), 2**63 - 1

def lit(n):
    # int64 min has no literal spelling: the token 9223372036854775808 is out of
    # range on its own. corelib's own fixtures write it as this expression.
    if n == MIN: return "(0 - 9223372036854775807) - 1"
    if n < 0:    return f"(0 - {-n})"
    return str(n)

# --- the corpus. Edges first, then a spread -- the edges are the point. -------
edge = [0, 1, -1, 2, -2, 7, -7, 12, -12, 18, MAX, MIN, MAX - 1, MIN + 1,
        2**31, -(2**31), 2**32, 2**62, -(2**62), 6, 35, 210]
spread = []
s = 1
for i in range(60):                      # a deterministic LCG: no Math.random,
    s = (s * 6364136223846793005 + 1442695040888963407) % (2**64)   # so a rerun
    v = s % (2**63)                                                 # is the same
    spread.append(v if i % 2 else -v)
ints = edge + spread

pairs = []
for a in ints:
    for b in (0, 1, -1, 12, MIN, MAX, a):
        pairs.append((a, b))
pairs = pairs[:600]

# --- what tycho must be ASKED ------------------------------------------------
ty = ["package main", 'import "core:math"', "", "fn main():"]
ty.append("    pa := [" + ", ".join(lit(a) for a, _ in pairs) + "]")
ty.append("    pb := [" + ", ".join(lit(b) for _, b in pairs) + "]")
ty.append("    i := 0")
ty.append("    for i < len(pa):")
ty.append('        println(str(math.gcd(pa[i], pb[i])) + " " + str(math.abs(pa[i])) + " " '
          '+ str(math.sign(pa[i])) + " " + str(math.min(pa[i], pb[i])) + " " '
          '+ str(math.max(pa[i], pb[i])))')
ty.append("        i = i + 1")

# clamp: lo <= hi only. lo > hi is a caller error with no documented answer --
# scoring it here would pin whatever the code happens to do, which is the exact
# failure this lane exists to avoid. Flagged in FRICTION instead.
tri = []
for x in ints[:40]:
    for lo, hi in ((0, 10), (-10, 10), (MIN, MAX), (-1, 1), (5, 5)):
        tri.append((x, lo, hi))
ty.append("    tx := [" + ", ".join(lit(x) for x, _, _ in tri) + "]")
ty.append("    tl := [" + ", ".join(lit(l) for _, l, _ in tri) + "]")
ty.append("    th := [" + ", ".join(lit(h) for _, _, h in tri) + "]")
ty.append("    j := 0")
ty.append("    for j < len(tx):")
ty.append('        println(str(math.clamp(tx[j], tl[j], th[j])))')
ty.append("        j = j + 1")

# ipow: exp >= 0 is the contract, and exp < 0 returning 0 is documented.
powc = [(b, e) for b in (0, 1, -1, 2, -2, 3, -3, 10) for e in range(0, 64)]
powc += [(b, -1) for b in (2, -2, 0)] + [(2, 62), (2, 63), (-2, 63)]
ty.append("    pb2 := [" + ", ".join(lit(b) for b, _ in powc) + "]")
ty.append("    pe := [" + ", ".join(lit(e) for _, e in powc) + "]")
ty.append("    k := 0")
ty.append("    for k < len(pb2):")
ty.append('        println(str(math.ipow(pb2[k], pe[k])))')
ty.append("        k = k + 1")

# --- the FLOAT arm, and it is the reason this lane is not decoration ----------
# The first version of this script was ints only. It caught the gcd defect (38
# mismatches) and scored the sign-of-infinity defect GREEN -- 1197 clean answers
# while never once calling sign on a float. min/max/clamp/sign are generic and
# their int instantiation says nothing about their float one.
#
# NaN is scored for `sign` alone. tycho's min/max are written as `a < b`, and
# every comparison against a NaN is false, so which argument comes back is an
# artefact of the branch order rather than a documented answer -- pinning it here
# would freeze an accident. Infinities ARE well defined and are scored throughout.
fexpr = {"inf": "inf", "-inf": "(0.0 - inf)", "nan": "(inf - inf)"}
def flit(s):
    if s in fexpr: return fexpr[s]
    return f"(0.0 - {s[1:]})" if s.startswith("-") else s

finite = ["0.0", "1.0", "-1.0", "2.5", "-2.5", "0.1", "-0.1", "3.0", "-3.0",
          "1e-300", "-1e-300", "1e308", "-1e308", "5e-324", "-5e-324",
          "4503599627370496.0", "-4503599627370496.0"]
fvals  = finite + ["inf", "-inf"]
fpairs = [(a, b) for a in fvals for b in fvals]
fsign  = fvals + ["nan"]                 # sign is the only one NaN is scored for
ftri   = [(x, lo, hi) for x in fvals for lo, hi in
          (("0.0", "1.0"), ("-1.0", "1.0"), ("-inf", "inf"), ("2.5", "2.5"))]

ty.append("    inf := 1.0e308 * 10.0")
ty.append('    println("--F--")')
ty.append("    fs := [" + ", ".join(flit(v) for v in fsign) + "]")
ty.append("    m := 0")
ty.append("    for m < len(fs):")
ty.append('        println(str(math.sign(fs[m])))')
ty.append("        m = m + 1")
ty.append("    fa := [" + ", ".join(flit(a) for a, _ in fpairs) + "]")
ty.append("    fb := [" + ", ".join(flit(b) for _, b in fpairs) + "]")
ty.append("    n := 0")
ty.append("    for n < len(fa):")
ty.append('        println(str(math.min(fa[n], fb[n])) + " " + str(math.max(fa[n], fb[n])))')
ty.append("        n = n + 1")
ty.append("    cx := [" + ", ".join(flit(x) for x, _, _ in ftri) + "]")
ty.append("    cl := [" + ", ".join(flit(l) for _, l, _ in ftri) + "]")
ty.append("    ch := [" + ", ".join(flit(h) for _, _, h in ftri) + "]")
ty.append("    q := 0")
ty.append("    for q < len(cx):")
ty.append('        println(str(math.clamp(cx[q], cl[q], ch[q])))')
ty.append("        q = q + 1")

fexp = []
for v in fsign:
    x = float(v)
    fexp.append("0" if x != x else str((x > 0) - (x < 0)))
for a, b in fpairs:
    x, y = float(a), float(b)
    fexp.append(f"{repr(min(x, y))} {repr(max(x, y))}")
for x, lo, hi in ftri:
    x, lo, hi = float(x), float(lo), float(hi)
    fexp.append(repr(lo if x < lo else (hi if x > hi else x)))
(T / "expected_f").write_text("\n".join(fexp) + "\n")

n_inf = sum(1 for a, b in fpairs if a in ("inf", "-inf") or b in ("inf", "-inf"))
if n_inf < 20 or "nan" not in fsign:
    print("math-diff: FAILED (the float corpus does not reach the infinities, "
          "which is the only reason it exists)")
    sys.exit(1)

(T / "p").mkdir(exist_ok=True)          # its own dir: siblings share a package
(T / "p" / "main.ty").write_text("\n".join(ty) + "\n")

# --- what Python says it must ANSWER -----------------------------------------
def ty_abs(x):  return x if x == MIN else abs(x)
def ty_sign(x): return (x > 0) - (x < 0)
def ty_gcd(a, b):
    # The documented exception, quoted from corelib/math/math.ty: where the true
    # answer is 2^63 it does not fit, and follows abs() in returning min itself.
    import math as m
    g = m.gcd(abs(a), abs(b))
    return MIN if g > MAX else g

exp = []
for a, b in pairs:
    exp.append(f"{ty_gcd(a,b)} {ty_abs(a)} {ty_sign(a)} {min(a,b)} {max(a,b)}")
for x, lo, hi in tri:
    exp.append(str(lo if x < lo else (hi if x > hi else x)))
skipped = 0
for b, e in powc:
    if e < 0:
        exp.append("0"); continue        # documented
    v = b ** e
    if v > MAX or v < MIN:
        exp.append(None); skipped += 1   # no overflow guard by design
    else:
        exp.append(str(v))
(T / "expected").write_text("\n".join("" if v is None else v for v in exp) + "\n")
(T / "skipmask").write_text("".join("1" if v is None else "0" for v in exp) + "\n")

# --- the corpus must contain the edges it claims to --------------------------
n_min   = sum(1 for a, b in pairs if a == MIN or b == MIN)
n_neg   = sum(1 for a, b in pairs if a < 0)
n_over  = skipped
bad = []
if n_min  < 10: bad.append(f"only {n_min} pairs touch int64 min")
if n_neg  < 50: bad.append(f"only {n_neg} pairs are negative")
if n_over < 5:  bad.append(f"only {n_over} ipow cases overflow, so the skip path is untested")
if bad:
    print("math-diff: FAILED (the corpus does not exercise its own edges: "
          + "; ".join(bad) + ")")
    sys.exit(1)
print(f"  corpus: {len(pairs)} int pairs, {len(tri)} clamp triples, "
      f"{len(powc)} ipow cases ({n_min} touch min, {n_neg} negative, "
      f"{n_over} ipow SKIPPED as out-of-range by design)")
print(f"  float:  {len(fsign)} sign, {len(fpairs)} min/max pairs, {len(ftri)} clamp "
      f"triples ({n_inf} reach an infinity)")
PY

./tychoc "$T/p/main.ty" -o "$T/probe" >"$T/build.log" 2>&1 || {
    echo "math-diff: FAILED (the probe does not build)"; tail -5 "$T/build.log"; exit 1; }
"$T/probe" > "$T/actual" 2>"$T/run.log" || {
    echo "math-diff: FAILED (the probe died)"; tail -5 "$T/run.log"; exit 1; }

python3 - "$T" <<'PY'
import sys, pathlib
T = pathlib.Path(sys.argv[1])
whole = T.joinpath("actual").read_text().splitlines()
if "--F--" not in whole:
    print("math-diff: FAILED (the float arm never ran -- no --F-- marker)")
    sys.exit(1)
cut  = whole.index("--F--")
act, act_f = whole[:cut], whole[cut + 1:]
exp  = T.joinpath("expected").read_text().splitlines()
exp_f = T.joinpath("expected_f").read_text().splitlines()
mask = T.joinpath("skipmask").read_text().strip()

# Floats are compared as VALUES, not as text: tycho and Python both round-trip
# their own output, but they need not choose the same digits for it, and a lane
# that compared strings would report a pile of defects that are a formatting
# difference. Parsing both sides is what makes the comparison about arithmetic.
def as_float(s):
    try: return float(s.strip())
    except ValueError: return None

bad_f = []
if len(act_f) != len(exp_f):
    print(f"math-diff: FAILED (float arm printed {len(act_f)} lines, oracle expects {len(exp_f)})")
    sys.exit(1)
for i, (x, y) in enumerate(zip(act_f, exp_f)):
    xs, ys = x.split(), y.split()
    if len(xs) != len(ys):
        bad_f.append((i, x, y)); continue
    for xv, yv in zip(xs, ys):
        a2, b2 = as_float(xv), as_float(yv)
        if a2 is None or b2 is None:
            if xv.strip() != yv.strip(): bad_f.append((i, x, y))
        elif not (a2 == b2 or (a2 != a2 and b2 != b2)):
            bad_f.append((i, x, y))
        else:
            continue
        break

if len(act) != len(exp):
    print(f"math-diff: FAILED (tycho printed {len(act)} lines, the oracle expects {len(exp)})")
    sys.exit(1)

def score(a, e, m):
    bad = []
    for i, (x, y) in enumerate(zip(a, e)):
        if m[i] == "1":            # out-of-range ipow: no contract, not scored
            continue
        if x.strip() != y.strip():
            bad.append((i, x, y))
    return bad

bad = score(act, exp, mask)

# CONTROL 1: a deliberately wrong expectation must be CAUGHT. Without this a
# zero-mismatch run is indistinguishable from a comparison that never ran.
wrong = list(exp)
first = mask.index("0")
wrong[first] = (wrong[first] or "0") + "9"
if not score(act, wrong, mask):
    print("math-diff: FAILED (control dead -- a deliberately wrong expectation "
          "scored clean, so the comparison is not comparing)")
    sys.exit(1)

# CONTROL 2: the skip mask must not be swallowing real work.
if mask.count("1") >= mask.count("0"):
    print(f"math-diff: FAILED (control dead -- {mask.count('1')} of {len(mask)} "
          "lines are SKIPPED, so most of the corpus is unscored)")
    sys.exit(1)

# CONTROL 3: the float arm must be able to fail on its own. The int-only version
# of this lane scored 1197 clean answers while the sign-of-infinity defect sat
# untouched in front of it, so "the whole thing is green" is not the claim -- each
# arm has to be shown able to redden.
if not [1 for i, (x, y) in enumerate(zip(act_f, [e + "9" for e in exp_f]))
        if x.strip() != y.strip()]:
    print("math-diff: FAILED (control dead -- the float arm accepts a wrong expectation)")
    sys.exit(1)

if bad or bad_f:
    print(f"math-diff: FAIL ({len(bad)} int mismatches, {len(bad_f)} float)")
    for i, x, y in (bad + bad_f)[:12]:
        print(f"    line {i}: tycho {x!r}  python {y!r}")
    sys.exit(1)
print(f"math-diff: green ({mask.count('0')} scored int answers and {len(exp_f)} float "
      f"answers match Python, {mask.count('1')} out-of-range ipow cases skipped by "
      "design; a wrong expectation was caught in each arm)")
PY
