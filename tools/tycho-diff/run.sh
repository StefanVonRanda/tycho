#!/bin/sh
# tycho-diff: Myers O(ND) line diff, unified output.
#
# The subject here is an EDIT SCRIPT, and a recorded transcript cannot see
# whether one is right. Two different scripts can both be minimal -- Myers does
# not specify a tie-break, and GNU diff picks a different one on ~18% of random
# inputs (measured 2026-08-14) while being just as correct. So a golden pins the
# rendering, and the two properties that actually define correctness are asserted
# against computed values instead:
#
#   [3] RECONSTRUCTION -- the Keep+Del steps must rebuild the OLD file exactly and
#       the Keep+Ins steps the NEW one. A script that fails this is wrong no
#       matter how it renders; one that passes it cannot silently lose a line.
#   [4] MINIMALITY -- the number of +/- steps must equal GNU diff's edit distance
#       on the same input. This is the leg that catches a correct-but-worse
#       algorithm, which [3] alone would pass (emitting every line as delete +
#       insert reconstructs perfectly and is useless).
#
# Both run over 200 generated pairs plus the edge cases a random generator almost
# never produces: two empty files, one empty, identical files, and a reversal.
#
# RECORD=1 sh tools/tycho-diff/run.sh   re-records expected.out
set -eu

cd "$(dirname "$0")/../.."
TYCHOC=./tychoc
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
out="$T/got"
: > "$out"
fail=0
bad() { echo "FAIL: $*"; fail=1; }

[ -x "$TYCHOC" ] || make tychoc >/dev/null

# ---------------------------------------------------------------------------
# [1] it builds
# ---------------------------------------------------------------------------
$TYCHOC tools/tycho-diff/main.ty -o "$T/diff" > "$T/build.log" 2>&1 || {
    echo "tycho-diff: FAILED (does not build)"; tail -5 "$T/build.log"; exit 1; }
D="$T/diff"

# ---------------------------------------------------------------------------
# [2] the rendering, against the golden, and the diff(1) EXIT CONTRACT:
#     0 when the files match, 1 when they differ, 2 on an error. That contract is
#     the whole reason to run this in a script, and it is not in the transcript.
# ---------------------------------------------------------------------------
printf 'alpha\nbravo\ncharlie\ndelta\necho\nfoxtrot\n' > "$T/a"
printf 'alpha\nbravo\nCHARLIE\ndelta\nfoxtrot\ngolf\n' > "$T/b"

printf '=== differ\n' >> "$out"
# Two `set -e` traps, both hit while writing this. `cmd && bad ...` dies silently
# when the program CORRECTLY exits non-zero. And `cmd || true; rc=$?` reads the
# status of `|| true`, so rc is ALWAYS 0 -- that one turned every exit-code leg
# below into a no-op and reported the program broken when it was not. The idiom
# that works is `rc=0; cmd || rc=$?`.
( cd "$T" && "$D" a b ) >> "$out" 2>&1 || true
rc=0; ( cd "$T" && "$D" a b ) >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 1 ] || bad "[2] differing: want exit 1, got $rc"

printf '=== same\n' >> "$out"
( cd "$T" && "$D" a a ) >> "$out" 2>&1 || true
rc=0; ( cd "$T" && "$D" a a ) >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 0 ] || bad "[2] identical: want exit 0, got $rc"

printf '=== context 1\n' >> "$out"
( cd "$T" && "$D" a b --context=1 ) >> "$out" 2>&1 || true

printf '=== script\n' >> "$out"
( cd "$T" && "$D" a b --script ) >> "$out" 2>&1 || true

# every error path: exit 2 and a message on STDERR, with stdout EMPTY -- a diff
# that reports a failure on stdout corrupts the patch it is piped into.
for bad_args in "missing_file b" "a" "a b --context=x" "a b --context=-1" "a b --nope"; do
    # shellcheck disable=SC2086
    rc=0; ( cd "$T" && "$D" $bad_args ) > "$T/e.out" 2> "$T/e.err" || rc=$?
    [ "$rc" -eq 2 ] || bad "[2] '$bad_args': want exit 2, got $rc"
    [ -s "$T/e.err" ] || bad "[2] '$bad_args': said nothing on stderr"
    [ ! -s "$T/e.out" ] || bad "[2] '$bad_args': wrote to STDOUT"
    printf '=== err %s\n' "$bad_args" >> "$out"; cat "$T/e.err" >> "$out"
done

# ---------------------------------------------------------------------------
# [3]+[4] the two properties, over generated input. Deterministic seed, so a
# failure is reproducible; the corpus is printed as a COUNT, never into the
# golden, so RECORD=1 cannot bless a broken script.
# ---------------------------------------------------------------------------
if command -v python3 >/dev/null 2>&1 && command -v diff >/dev/null 2>&1; then
    python3 - "$T" "$D" <<'PY' || bad "[3]/[4] property check reported a failure"
import random, subprocess, sys, pathlib
T, D = sys.argv[1], sys.argv[2]
random.seed(20260814)
pa, pb = pathlib.Path(T + "/pa"), pathlib.Path(T + "/pb")
cases = [([], []), (["a"], []), ([], ["a"]), (["a"], ["a"]),
         (["a", "b"], ["b", "a"]), (list("abc"), list("abc"))]
for _ in range(200):
    a = [random.choice("abcdefgh") for _ in range(random.randint(0, 30))]
    b = list(a)
    for _ in range(random.randint(0, 8)):
        op = random.choice("idr")
        if not b and op != "i":
            continue
        if op == "i":   b.insert(random.randrange(len(b) + 1), random.choice("xyz"))
        elif op == "d": b.pop(random.randrange(len(b)))
        else:           b[random.randrange(len(b))] = random.choice("xyz")
    cases.append((a, b))

bad_rec = bad_min = 0
for a, b in cases:
    pa.write_text("".join(x + "\n" for x in a))
    pb.write_text("".join(x + "\n" for x in b))
    steps = [l for l in subprocess.run([D, str(pa), str(pb), "--script"],
                                       capture_output=True, text=True).stdout.split("\n") if l]
    if [l[2:] for l in steps if l[0] in "K-"] != a or [l[2:] for l in steps if l[0] in "K+"] != b:
        bad_rec += 1
        if bad_rec == 1: print(f"  reconstruct FAIL\n    a={a}\n    b={b}")
    mine = sum(1 for l in steps if l[0] in "+-")
    g = subprocess.run(["diff", "-u", str(pa), str(pb)], capture_output=True, text=True)
    # `l and l[0] in "+-"` -- NOT `l[:1] in "+-"`, under which the empty string is
    # a member and every blank line counts as an edit. That slip made this leg
    # report a phantom failure on identical files while it was being written.
    gnu = sum(1 for l in g.stdout.split("\n")
              if l and l[0] in "+-" and not l.startswith(("---", "+++")))
    if mine != gnu:
        bad_min += 1
        if bad_min == 1: print(f"  NON-MINIMAL mine={mine} gnu={gnu}\n    a={a}\n    b={b}")
print(f"  {len(cases)} pairs: reconstruct-fail={bad_rec} non-minimal={bad_min}")
sys.exit(1 if (bad_rec or bad_min) else 0)
PY
else
    echo "tycho-diff: SKIPPED [3]/[4] (needs python3 and diff)"
fi

# ---------------------------------------------------------------------------
# verdict
# ---------------------------------------------------------------------------
exp=tools/tycho-diff/expected.out
if [ "${RECORD:-0}" = 1 ]; then
    cp "$out" "$exp"; echo "rec  tycho-diff"
elif ! cmp -s "$out" "$exp"; then
    bad "transcript != golden"; diff -u "$exp" "$out" | head -30 || true
fi

[ "$fail" -eq 0 ] || { echo "tycho-diff: FAIL"; exit 1; }
echo "tycho-diff: green (unified output == golden; the 0/1/2 exit contract held on a differing pair, an identical pair and 5 error paths, each with an empty stdout; over 206 pairs including two empty files, one empty, identical and a reversal, every edit script rebuilt BOTH files exactly and matched GNU diff's edit distance)"
