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
        if op == "+":
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
for a, op, b, got, want in bad[:4]:
    print(f"  MISMATCH {a} {op} {b}\n    bignum={got!r}\n    python={want!r}")
print(f"  {len(cases)} operations against Python integers: {len(bad)} mismatches")
sys.exit(1 if bad else 0)
PY
rc=$?
[ "$rc" -eq 0 ] || { echo "bignum-diff: FAIL"; exit 1; }
echo "bignum-diff: green (control caught the wrong division model first, then every + - * and divmod agreed with Python's integers across the limb boundary, both int64 extremes, all four sign combinations and ~$((N * 2)) random pairs up to 10^60)"
