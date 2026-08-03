#!/bin/sh
# Gate for tycho-scheme, the Scheme interpreter AND its bytecode compiler.
# Same shape as vm-check: step [9] tools-check --emit-c's every .ty in the
# tree, so a syntax error already reddens there, and [3b] entrypoints never
# looks under tools/ -- so nothing RAN the interpreter or the compiler.
#
# WHAT IT ASSERTS:
#   [1] each program's interpreter output matches the golden, byte-identical,
#       on TWO runs (determinism).
#   [2] THE COMPILER: each program compiles to tycho-vm bytecode and the VM's
#       run is byte-identical to the interpreter's -- the differential that
#       proves the compiler lowers the same subset to the same answers
#       (`docs/internals/plan-tycho-scheme-DONE.md` phase 1, the compiler
#       front end; the VM's pair/closure ops came from it).
#   [3] the compiler dies non-zero at COMPILE time on what it cannot lower
#       (unbound variables, non-compilable primitives, primitive shadowing),
#       never emitting something silently wrong.
#   [4] the interpreter's runtime error cases die non-zero with EMPTY stdout.
#   [5] a deep-recursion program is NOT here -- the runtime's stack guard
#       (`docs/internals/plan-tycho-scheme-DONE.md` phase 1) turns stack
#       exhaustion into a clean failure, and the crash tests live in
#       tests/recursion/run.sh's generated-code side. The programs stay
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
    echo "FAIL: tychoc compile (interpreter)"; sed 's/^/      /' "$T/build.log"
    echo "tycho-scheme: FAIL"; exit 1
fi
V="$T/tycho-vm"
if ! "$TYCHOC" tools/tycho-vm/main.ty -o "$V" >"$T/vm.log" 2>&1; then
    echo "FAIL: tychoc compile (tycho-vm)"; sed 's/^/      /' "$T/vm.log"
    echo "tycho-scheme: FAIL"; exit 1
fi

out="$T/all.out"
: > "$out"

# [1] + [2] the programs: interpreter (twice, deterministic, locked to the
# golden), then compiled and run on the VM, byte-identical to the interpreter.
for p in fib closures ho sort shadow eqsym; do
    if ! "$S" run "$progs/$p.scm" >"$T/$p.1" 2>"$T/$p.1.err"; then
        bad "$p first run failed"; sed 's/^/      /' "$T/$p.1.err"; continue
    fi
    if ! "$S" run "$progs/$p.scm" >"$T/$p.2" 2>"$T/$p.2.err"; then
        bad "$p second run failed"; continue
    fi
    cmp -s "$T/$p.1" "$T/$p.2" || bad "$p is not deterministic"
    cat "$T/$p.1" >> "$out"
    if ! "$S" compile "$progs/$p.scm" -o "$T/$p.tyc" >"$T/$p.c.err" 2>&1; then
        bad "$p: the compiler refused it"; sed 's/^/      /' "$T/$p.c.err"; continue
    fi
    if ! "$V" run "$T/$p.tyc" >"$T/$p.vm" 2>"$T/$p.vm.err"; then
        bad "$p: the VM rejected the compiled output"; sed 's/^/      /' "$T/$p.vm.err"; continue
    fi
    cmp -s "$T/$p.vm" "$T/$p.1" || bad "$p: compiled output differs from the interpreter"
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

# [3] the compiler's compile-time rejections: die non-zero, nothing emitted.
crej() {
    name="$1"; src="$2"
    printf '%s\n' "$src" > "$T/cr.scm"
    if "$S" compile "$T/cr.scm" -o "$T/cr.tyc" >/dev/null 2>"$T/cr.err"; then
        bad "compile-reject $name: compiled anyway"
    elif [ -s "$T/cr.tyc" ]; then
        bad "compile-reject $name: emitted a .tyc despite dying"
    else
        echo "ok    compile-reject $name"
    fi
}
crej "unbound"        '(display undefined-symbol)'
crej "not-compilable" '(define (f x) (pair? x))'
crej "prim-shadow"    '(define + 5)'
crej "arity"          '(define (f a b) (- a))'

# [4] the interpreter's runtime error cases: die non-zero, stdout EMPTY.
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
echo "tycho-scheme: green (6 programs byte-identical interpreter-vs-VM-compiled; interpreter golden locked on two runs; 4 compile-time rejections; 5 interpreter error cases die with empty stdout)"
