#!/bin/sh
# Differential-test core:bignum against an INDEPENDENT arbitrary-precision
# implementation: Python's integers.
#
# `make corelib` checks bignum against a recorded golden, which proves it has not
# CHANGED. It cannot prove the answers were right in the first place -- a golden
# is this program agreeing with its own earlier self. Arbitrary-precision
# arithmetic is exactly where that matters: a carry that only misfires across a
# limb boundary, or a sign rule that is wrong only when both operands are
# negative, produces plausible digits forever.
#
# Coverage is edges FIRST, then random: zero, +/-1, single digit, the 10^8
# limb boundary either side, 10^18, and both int64 extremes, crossed with each
# other over + - * and divmod; then ~1400 random pairs up to 10^60 / 10^40.
#
# A CONTROL RUNS FIRST, for the reason every clean differential needs one: the
# same cases are scored against PYTHON'S FLOOR division, which differs from
# Tycho's truncating divmod exactly when the operands' signs differ. It must
# report mismatches. If it does not, the comparison is dead and a clean main run
# below would mean nothing.
#
#   N=<count> sh scripts/bignum_diff.sh    random pairs (default 700)
set -eu

cd "$(dirname "$0")/.."
N=${N:-700}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

command -v python3 >/dev/null 2>&1 || { echo "bignum-diff: SKIPPED (no python3)"; exit 0; }
[ -x ./tychoc ] || make tychoc >/dev/null

cat > "$T/h.ty" <<'TY'
package main

import "core:bignum"
import "core:decimal"
import "core:io"
import "core:strings"

# One "a op b" per line in, one answer per line out, so an external oracle can
# diff it. divmod prints "q r".
fn main():
    for line in io.read_lines(args()[1]):
        if line == "":
            continue
        astr, rest := strings.split_once(line, " ")
        op, bstr := strings.split_once(rest, " ")
        a := bignum.from_str(astr)
        b := bignum.from_str(bstr)
        if op == "d/":                       # bstr is "divisor:scale:mode"
            parts := split(bstr, ":")
            md := decimal.HALF_UP
            if parts[2] == "1":
                md = decimal.TOWARD_ZERO
            match decimal.div(decimal.from_str(astr), decimal.from_str(parts[0]),
                              strings.parse_int(parts[1]), md):
                Ok(q): println(decimal.to_str(q))
                Err(e): println("ERR")
        elif op == "d+":                     # the decimal ops share the driver
            println(decimal.to_str(decimal.add(decimal.from_str(astr), decimal.from_str(bstr))))
        elif op == "d-":
            println(decimal.to_str(decimal.sub(decimal.from_str(astr), decimal.from_str(bstr))))
        elif op == "d*":
            println(decimal.to_str(decimal.mul(decimal.from_str(astr), decimal.from_str(bstr))))
        elif op == "+":
            println(bignum.to_str(bignum.add(a, b)))
        elif op == "-":
            println(bignum.to_str(bignum.sub(a, b)))
        elif op == "*":
            println(bignum.to_str(bignum.mul(a, b)))
        elif op == "/":
            q, r := bignum.divmod(a, b)
            println(bignum.to_str(q) + " " + bignum.to_str(r))
        else:
            println("?")
TY

./tychoc "$T/h.ty" -o "$T/bin" > "$T/build.log" 2>&1 || {
    echo "bignum-diff: FAILED (the harness does not build)"; tail -5 "$T/build.log"; exit 1; }

python3 - "$T" "$N" <<'PY'
import pathlib, random, subprocess, sys
T, N = sys.argv[1], int(sys.argv[2])
random.seed(20260815)                      # deterministic: a mismatch reproduces

def truncating(a, b):
    q = abs(a) // abs(b); r = abs(a) % abs(b)
    if (a < 0) != (b < 0): q = -q
    if a < 0: r = -r
    return f"{q} {r}"

def run(cases, model):
    p = pathlib.Path(T + "/in.txt")
    p.write_text("".join(f"{a} {op} {b}\n" for a, op, b in cases))
    out = subprocess.run([T + "/bin", str(p)], capture_output=True, text=True).stdout.split("\n")
    bad = []
    for (a, op, b), got in zip(cases, out):
        want = ({"+": lambda: str(a + b), "-": lambda: str(a - b),
                 "*": lambda: str(a * b), "/": lambda: model(a, b)}[op])()
        if got != want: bad.append((a, op, b, got, want))
    return bad

# --- the control: score division by PYTHON FLOOR, which is the wrong model ----
ctl = [(-100, "/", 7), (100, "/", -7), (-1, "/", 3), (7, "*", 6)]
seen = run(ctl, lambda a, b: f"{a//b} {a%b}")
if len(seen) < 3:
    print(f"  CONTROL FOUND ONLY {len(seen)} mismatches against the floor model; expected 3.")
    print("  The comparison is not live, so a clean run below would prove nothing.")
    sys.exit(1)

# --- the real run ------------------------------------------------------------
edge = [0, 1, -1, 9, 10, -10, 99999999, 100000000, 10**18, -10**18, 2**63 - 1, -(2**63)]
cases = []
for a in edge:
    for b in edge:
        for op in "+-*": cases.append((a, op, b))
        if b: cases.append((a, "/", b))
for _ in range(N):
    a = random.randint(-10**random.randint(1, 60), 10**random.randint(1, 60))
    b = random.randint(-10**random.randint(1, 40), 10**random.randint(1, 40))
    cases.append((a, random.choice("+-*"), b))
    if b: cases.append((a, "/", b))

bad = run(cases, truncating)

# --- core:decimal, same idea, against Python's Decimal ------------------------
# Compared as VALUE and SCALE, never as Python's chosen NOTATION: str(Decimal)
# switches to 1E-12 for small exponents and keeps a signed zero, and scoring
# against that text reported 105 "mismatches" that were all the oracle. Scale is
# compared too because for money it is part of the answer -- 1.50 is not 1.5.
from decimal import Decimal, getcontext
getcontext().prec = 200

def dscale(t): return len(t.split(".")[1]) if "." in t else 0

def drun(dcases):
    p = pathlib.Path(T + "/din.txt")
    p.write_text("".join(f"{a} d{op} {b}\n" for a, op, b in dcases))
    out = subprocess.run([T + "/bin", str(p)], capture_output=True, text=True).stdout.split("\n")
    out_bad = []
    for (a, op, b), got in zip(dcases, out):
        A, B = Decimal(a), Decimal(b)
        w = {"+": A + B, "-": A - B, "*": A * B}[op]
        if Decimal(got) != w:                      out_bad.append(("value", a, op, b, got, str(w)))
        elif dscale(got) != -w.as_tuple().exponent: out_bad.append(("scale", a, op, b, got, str(w)))
    return out_bad

dedge = ["0", "-0", "0.0", "1", "-1", "0.1", "0.2", "1.50", "2.25", "-1.10",
         "99999999999999999999.5", "0.000000000001", "-0.000000000001", "1000000000000000000000"]
dcases = [(a, op, b) for a in dedge for b in dedge for op in "+-*"]
for _ in range(N):
    def dmk():
        ip = random.randint(-10**random.randint(0, 20), 10**random.randint(0, 20))
        sc = random.randint(0, 12)
        return str(ip) if sc == 0 else f"{ip}." + "".join(random.choice("0123456789") for _ in range(sc))
    dcases.append((dmk(), random.choice("+-*"), dmk()))

# the control: both halves must catch a deliberately wrong expectation
ctl2 = [("1.50", "+", "2.25"), ("1.5", "*", "2")]
pv = pathlib.Path(T + "/dctl.txt")
pv.write_text("".join(f"{a} d{op} {b}\n" for a, op, b in ctl2))
o2 = subprocess.run([T + "/bin", str(pv)], capture_output=True, text=True).stdout.split("\n")
live_v = live_s = 0
for (a, op, b), got in zip(ctl2, o2):
    A, B = Decimal(a), Decimal(b)
    w = {"+": A + B, "*": A * B}[op]
    if Decimal(got) != w + Decimal("0.01"): live_v += 1
    if dscale(got) != -w.as_tuple().exponent + 1: live_s += 1
if live_v < len(ctl2) or live_s < len(ctl2):
    print(f"  DECIMAL CONTROL DEAD: value {live_v}/{len(ctl2)}, scale {live_s}/{len(ctl2)}")
    sys.exit(1)

# ---- decimal DIVISION, with both rounding modes ----------------------------
# div is the operation with a MODE, and a mode that is silently ignored looks
# exactly like a mode that works -- most quotients round the same either way.
# So the control swaps HALF_UP and TOWARD_ZERO in the oracle and requires
# disagreement; if the two score alike, div is not reading its argument.
from decimal import ROUND_HALF_UP, ROUND_DOWN

def dvrun(cs):
    p2 = pathlib.Path(T + "/dvin.txt")
    p2.write_text("".join(f"{a} d/ {b}:{sc}:{md}\n" for a, b, sc, md in cs))
    o = subprocess.run([T + "/bin", str(p2)], capture_output=True, text=True).stdout.split("\n")
    return o

def dvscore(cs, o, swap):
    n = 0
    for (a, b, sc, md), g in zip(cs, o):
        m = (1 - md) if swap else md
        w = (Decimal(a) / Decimal(b)).quantize(
            Decimal(1).scaleb(-sc), rounding=ROUND_HALF_UP if m == 0 else ROUND_DOWN)
        if g == "ERR" or Decimal(g) != w:
            n += 1
    return n

dvcases = [("1", "3", 4, 0), ("1", "3", 4, 1), ("2", "3", 4, 0), ("2", "3", 4, 1),
           ("10", "3", 0, 0), ("10", "3", 0, 1), ("-1", "3", 4, 0), ("-1", "3", 4, 1),
           ("1", "-3", 4, 0), ("-2", "3", 0, 0), ("-2", "3", 0, 1), ("0.5", "1", 0, 0),
           ("0.5", "1", 0, 1), ("-0.5", "1", 0, 0), ("-0.5", "1", 0, 1),
           ("1.5", "1", 0, 0), ("2.5", "1", 0, 0), ("-1.5", "1", 0, 0),
           ("0", "7", 3, 0), ("1.005", "1", 2, 0)]
for _ in range(N):
    bb = dmk()
    if Decimal(bb) == 0:
        bb = "7"
    dvcases.append((dmk(), bb, random.randint(0, 8), random.randint(0, 1)))
dvout = dvrun(dvcases)
if dvscore(dvcases, dvout, True) == 0:
    print("  DECIMAL DIV CONTROL DEAD: swapping the rounding modes changed nothing,")
    print("                            so div is not reading its mode argument")
    sys.exit(1)
ndv = dvscore(dvcases, dvout, False)
print(f"  {len(dvcases)} decimal divisions (both rounding modes) against Python: {ndv} mismatches")
if ndv:
    for (a, b, sc, md), g in list(zip(dvcases, dvout))[:3]:
        print(f"    DIV {a} / {b} scale={sc} mode={md} -> {g}")

dbad = drun(dcases)
for kind, a, op, b, got, want in dbad[:4]:
    print(f"  DECIMAL {kind.upper()} {a} {op} {b}: tycho={got!r} python={want!r}")
print(f"  {len(dcases)} decimal operations against Python's Decimal: {len(dbad)} mismatches")
bad = bad + dbad + [("div",)] * ndv
for a, op, b, got, want in bad[:4]:
    print(f"  MISMATCH {a} {op} {b}\n    bignum={got!r}\n    python={want!r}")
print(f"  {len(cases)} operations against Python integers: {len(bad)} mismatches")
sys.exit(1 if bad else 0)
PY
rc=$?
[ "$rc" -eq 0 ] || { echo "bignum-diff: FAIL"; exit 1; }
echo "bignum-diff: green (control caught the wrong division model first, then every + - * and divmod agreed with Python's integers across the limb boundary, both int64 extremes, all four sign combinations and ~$((N * 2)) random pairs up to 10^60; and core:decimal agreed with Python's Decimal on both VALUE and SCALE over the same shape of corpus, division included at both rounding modes -- with the mode-swap control proving the mode is read rather than ignored)"
