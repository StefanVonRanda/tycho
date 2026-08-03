#!/bin/sh
# Gate for tycho-scheme, the Scheme interpreter in tools/tycho-scheme/. Same
# shape as vm-check: step [9] tools-check --emit-c's every .ty in the tree, so a
# syntax error already reddens there, and [3b] entrypoints never looks under
# tools/ -- so nothing RAN the interpreter.
#
# WHAT IT ASSERTS:
#   [1] each program's output matches the golden, byte-identical, on TWO runs
#       (determinism: the environment pool and the evaluator are pure functions
#       of the source, so two runs must agree).
#   [2] the error cases die non-zero with EMPTY stdout (fail closed, the
#       interpreter's die() path).
#   [3] a deep-recursion program is NOT here -- the runtime now turns stack
#       exhaustion into a clean failure (`docs/internals/plan-tycho-scheme-DONE.md` phase 1: the stack-overflow guard
#       in runtime/tycho_rt.c), and the CRASH tests for that live in
#       tests/recursion/run.sh's generated-code side. These four programs stay
#       shallow because their goldens are answers, not crash tests.
#
# Re-record the golden with:  RECORD=1 sh tools/tycho-scheme/run.sh
set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "tycho-scheme: no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
RECORD="${RECORD:-0}"
golden="$PWD/tools/tycho-scheme/expected.out"
progs="$PWD/tools/tycho-scheme/progs"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL: $*"; fail=1; }

S="$T/tycho-scheme"
if ! "$TYCHOC" tools/tycho-scheme/main.ty -o "$S" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/build.log"
    echo "tycho-scheme: FAIL"; exit 1
fi

out="$T/all.out"
: > "$out"

# [1] the programs: run each twice, lock to the golden.
for p in fib closures ho sort; do
    if ! "$S" run "$progs/$p.scm" >"$T/$p.1" 2>"$T/$p.1.err"; then
        bad "$p first run failed"; sed 's/^/      /' "$T/$p.1.err"; continue
    fi
    if ! "$S" run "$progs/$p.scm" >"$T/$p.2" 2>"$T/$p.2.err"; then
        bad "$p second run failed"; continue
    fi
    cmp -s "$T/$p.1" "$T/$p.2" || bad "$p is not deterministic"
    cat "$T/$p.1" >> "$out"
done

if [ "$RECORD" = "1" ]; then
    cp "$out" "$golden"
    echo "tycho-scheme: golden recorded ($(wc -l < "$golden") lines)"
    exit 0
fi
if ! cmp -s "$out" "$golden"; then
    bad "output does not match the golden; re-record with RECORD=1 sh tools/tycho-scheme/run.sh"
    diff "$golden" "$out" | head -10 | sed 's/^/      /'
fi

# [2] the error cases: die non-zero, stdout EMPTY.
errcase() {
    name="$1"; shift
    printf '%s\n' "$1" > "$T/err.scm"
    out=""; code=0
    out=$("$S" run "$T/err.scm" 2>/dev/null); code=$?
    if [ "$code" -eq 0 ]; then bad "$name: exited 0"; return; fi
    if [ -n "$out" ]; then bad "$name: stdout not empty"; return; fi
}
errcase "unbound"    '(display (undefined-symbol))'
errcase "divzero"    '(/ 1 0)'
errcase "arity"      '((lambda (x) x) 1 2)'
errcase "badquote"   '(car 5)'
errcase "badcall"    '(5 6)'

if [ "$fail" -eq 1 ]; then
    echo "tycho-scheme: FAIL"
    exit 1
fi
echo "tycho-scheme: green (fib/closures/ho/sort match the golden byte-identically on two runs; 5 error cases die non-zero with empty stdout)"
