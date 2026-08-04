#!/bin/sh
# Gate for tycho-build, the make-like build tool in tools/tycho-build/main.ty.
#
# Re-record the golden with:  RECORD=1 sh tools/tycho-build/run.sh
#
# WHAT IT ASSERTS, and why each leg exists. The build tool's contract has two
# halves -- the DAG runs the right recipes in the right order, and the second
# build is a NO-OP -- and each leg pins one way it can betray you:
#
#   [1] FIRST BUILD RUNS THE RIGHT STEPS IN DAG ORDER. A chain (src -> out1,
#       out2 -> final) plus an independent branch (other -> other_src). The
#       golden locks the scheduler's dispatch lines: the two independent
#       branches and the chain's leaves first, the dependent `final` after its
#       deps complete. Outputs are checked by content.
#   [2] SECOND BUILD IS A NO-OP. Same tree, rebuilt: empty stdout, exit 0,
#       outputs untouched -- the differential. An up-to-date check that
#       re-ran recipes (or printed anything) reddens here.
#   [3] TOUCH ONLY ITS DEPENDENTS. `touch src` bumps src's mtime; out1, out2
#       and final rebuild, the independent branch does not. This also
#       exercises the rebuilt-in-this-run tie-breaker (final must rebuild even
#       though out1/out2's mtimes may equal its own within one second).
#   [4] A FAILED RECIPE FAILS THE BUILD AND SKIPS ITS DEPENDENTS. exit 1, a
#       `FAILED bad (exit 3)` line, and the dependent's output file absent.
#   [5] DETERMINISM. Two clean fixtures build to byte-identical stdout.
#   [6] USAGE / PARSE / IO ERRORS EXIT 2. Missing buildfile, a garbage line,
#       an unknown target, a dependency cycle.
set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
D=tools/tycho-build
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
RECORD="${RECORD:-0}"
fail=0

"$TYCHOC" "$D/main.ty" -o "$T/tb" >"$T/build.log" 2>&1 || { echo "FAIL (tool build)"; cat "$T/build.log"; exit 2; }

# the fixture: a chain (src -> out1, out2 -> final) plus an independent branch
fix() {                                                   # $1 = fixture dir
    mkdir -p "$1"
    cat > "$1/buildfile" <<'EOF'
all: final other
final: out1 out2
    cat out1 out2 > final
out1: src
    cp src out1
out2: src
    cp src out2
other: other_src
    cp other_src other
EOF
    printf 'SOURCE-ONE\n' > "$1/src"
    printf 'OTHER-SRC\n' > "$1/other_src"
}

# [1] first build: golden dispatch order, correct outputs, exit 0
fix "$T/a"
( cd "$T/a" && "$T/tb" buildfile ) > "$T/a1.out" 2> "$T/a1.err"; rc=$?
[ "$rc" -eq 0 ] || { echo "FAIL [1] first build exit $rc"; cat "$T/a1.err"; fail=1; }
if [ "$RECORD" = 1 ]; then
    cp "$T/a1.out" "$D/expected.out"; echo "rec  [1] first-build golden"
elif ! cmp -s "$T/a1.out" "$D/expected.out"; then
    echo "FAIL [1] first-build stdout != golden"; diff "$D/expected.out" "$T/a1.out" | head; fail=1
fi
[ -f "$T/a/out1" ] && [ -f "$T/a/out2" ] && [ -f "$T/a/other" ] && [ -f "$T/a/final" ] \
    || { echo "FAIL [1] outputs missing"; fail=1; }
[ "$(cat "$T/a/out1")" = "SOURCE-ONE" ] && [ "$(cat "$T/a/final")" = "SOURCE-ONE
SOURCE-ONE" ] && [ "$(cat "$T/a/other")" = "OTHER-SRC" ] \
    || { echo "FAIL [1] output contents wrong"; fail=1; }

# [2] second build is a NO-OP: empty stdout, exit 0
( cd "$T/a" && "$T/tb" buildfile ) > "$T/a2.out" 2> "$T/a2.err"; rc=$?
[ "$rc" -eq 0 ] || { echo "FAIL [2] no-op build exit $rc"; cat "$T/a2.err"; fail=1; }
[ -s "$T/a2.out" ] && { echo "FAIL [2] no-op build printed:"; cat "$T/a2.out"; fail=1; }

# [3] touch src -> only its dependents rebuild (out1, out2, final; not other)
sleep 1; touch "$T/a/src"
( cd "$T/a" && "$T/tb" buildfile ) > "$T/a3.out" 2> "$T/a3.err"; rc=$?
[ "$rc" -eq 0 ] || { echo "FAIL [3] rebuild exit $rc"; cat "$T/a3.err"; fail=1; }
exp3="build out1
build out2
build final"
printf '%s\n' "$exp3" > "$T/exp3"
[ "$(cat "$T/a3.out")" = "$exp3" ] || { echo "FAIL [3] rebuild lines wrong"; diff "$T/exp3" "$T/a3.out" | head; fail=1; }

# [4] a failing recipe fails the build and skips its dependents
mkdir -p "$T/f"
cat > "$T/f/buildfile" <<'EOF'
all: bad dep
bad: src
    exit 3
dep: bad
    printf built > dep_out
EOF
printf 'S\n' > "$T/f/src"
( cd "$T/f" && "$T/tb" buildfile ) > "$T/f.out" 2> "$T/f.err"; rc=$?
[ "$rc" -eq 1 ] || { echo "FAIL [4] failing build exit $rc (want 1)"; fail=1; }
grep -q "build bad" "$T/f.out" || { echo "FAIL [4] no 'build bad' line"; fail=1; }
grep -q "FAILED bad (exit 3)" "$T/f.out" || { echo "FAIL [4] no FAILED line"; cat "$T/f.out"; fail=1; }
grep -q "build dep" "$T/f.out" && { echo "FAIL [4] dependent ran after failure"; fail=1; }
[ -f "$T/f/dep_out" ] && { echo "FAIL [4] dependent output exists"; fail=1; }

# [5] determinism: two clean fixtures build byte-identically
fix "$T/c1"; fix "$T/c2"
( cd "$T/c1" && "$T/tb" buildfile ) > "$T/c1.out" 2>/dev/null
( cd "$T/c2" && "$T/tb" buildfile ) > "$T/c2.out" 2>/dev/null
cmp -s "$T/c1.out" "$T/c2.out" || { echo "FAIL [5] builds differ"; diff "$T/c1.out" "$T/c2.out" | head; fail=1; }

# [6] usage / parse / io errors exit 2
"$T/tb" "$T/nonexistent" >/dev/null 2>&1; [ $? -eq 2 ] || { echo "FAIL [6] missing buildfile"; fail=1; }
printf 'garbage line\n' > "$T/bad"
"$T/tb" "$T/bad" >/dev/null 2>&1;   [ $? -eq 2 ] || { echo "FAIL [6] parse error"; fail=1; }
( cd "$T/a" && "$T/tb" buildfile nosuch ) >/dev/null 2>&1; [ $? -eq 2 ] || { echo "FAIL [6] unknown target"; fail=1; }
printf 'a: b\nb: a\n    touch a\n' > "$T/cyc"
"$T/tb" "$T/cyc" >/dev/null 2>&1;   [ $? -eq 2 ] || { echo "FAIL [6] cycle"; fail=1; }

[ "$fail" -eq 0 ] && echo "tycho-build: all green (6 legs)" || { echo "tycho-build: FAIL"; exit 1; }
