set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC="${TYCHOC:-./tychoc1}"
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
for p in fib closures ho sort shadow eqsym globals; do
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
    # Small arenas stress pool reuse after parsing/analysis dies before layout.
    if ! TYCHO_BLOCK=64 "$S" compile "$progs/$p.scm" -o "$T/$p.tiny.tyc" >"$T/$p.tiny.err" 2>&1; then
        bad "$p: tiny-arena compilation failed"
    elif ! cmp -s "$T/$p.tyc" "$T/$p.tiny.tyc"; then
        bad "$p: tiny-arena compilation changed bytecode"
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

# Definitions are hoisted before expressions, preserving both groups' order.
printf '%s\n' '(display 9)' '(define a (begin (display 1) 7))' '(display a)' \
    '(define b (begin (display 2) 8))' '(display b)' '(newline)' > "$T/order.scm"
printf '12978\n' > "$T/order.expected"
if ! "$S" compile "$T/order.scm" -o "$T/order.tyc" >"$T/order.compile.err" 2>&1; then
    bad "stable partition: compilation failed"
elif ! "$V" run "$T/order.tyc" >"$T/order.out" 2>"$T/order.vm.err"; then
    bad "stable partition: VM run failed"
elif ! cmp -s "$T/order.expected" "$T/order.out"; then
    bad "stable partition: definition or expression order changed"
fi

# Exercise the internal layout with a live Program snapshot and reused output.
mkdir "$T/owners"
awk '/^fn main\(\):$/ { sub(/main/, "scheme_cli_main"); n++ } { print }
    END { if (n != 1) exit 1 }' tools/tycho-scheme/main.ty > "$T/owners/main.ty" || exit 1
cp tools/tycho-scheme/main_compiler.ty "$T/owners/main_compiler.ty"
cp tests/fixtures/scheme_layout_owner.src "$T/owners/owner.ty"
if ! "$TYCHOC" "$T/owners/owner.ty" -o "$T/owners/check" >"$T/owners/build.log" 2>&1; then
    bad "layout-owner: harness compilation failed"
    sed 's/^/      /' "$T/owners/build.log"
elif ! "$T/owners/check" >"$T/owners/out" 2>"$T/owners/err"; then
    bad "layout-owner: harness failed"
    sed 's/^/      /' "$T/owners/err"
elif ! grep -qx 'layout-owner: independent inputs, outputs, siblings and snapshots' "$T/owners/out"; then
    bad "layout-owner: unexpected output"
fi
cp tests/fixtures/scheme_globals_ready.src "$T/owners/owner.ty"
if ! "$TYCHOC" "$T/owners/owner.ty" -o "$T/owners/ready" >"$T/owners/ready.build.log" 2>&1; then
    bad "globals-ready: harness compilation failed"
    sed 's/^/      /' "$T/owners/ready.build.log"
elif ! "$T/owners/ready" >"$T/owners/ready.out" 2>"$T/owners/ready.err"; then
    bad "globals-ready: harness failed"
    sed 's/^/      /' "$T/owners/ready.err"
elif ! grep -qx 'scheme globals readiness: ok' "$T/owners/ready.out"; then
    bad "globals-ready: unexpected output"
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
crej "known-fn-arity" '(define (f x) x) (f)'
crej "literal-arity"  '((lambda (x) x))'

# Uninitialized bindings must trap, not silently become the legitimate '().
globalerr() {
    name="$1"; src="$2"
    printf '%s\n' "$src" > "$T/globalerr.scm"
    if "$S" run "$T/globalerr.scm" >"$T/globalerr.interp" 2>"$T/globalerr.err"; then
        bad "global-error $name: interpreter accepted it"
    elif [ -s "$T/globalerr.interp" ]; then
        bad "global-error $name: interpreter stdout not empty"
    elif [ "$(wc -l < "$T/globalerr.err")" -ne 1 ] || ! grep -Eq "^tycho-scheme: ((set! on )?unbound variable '[^']+'|attempt to call a non-function|expected [0-9]+ argument\(s\), got [0-9]+)$" "$T/globalerr.err"; then
        bad "global-error $name: unexpected interpreter error"
    fi
    if ! "$S" compile "$T/globalerr.scm" -o "$T/globalerr.tyc" >"$T/globalerr.compile.err" 2>&1; then
        bad "global-error $name: compilation failed"
    elif "$V" run "$T/globalerr.tyc" >"$T/globalerr.vm" 2>"$T/globalerr.vm.err"; then
        bad "global-error $name: VM accepted it"
    elif [ -s "$T/globalerr.vm" ]; then
        bad "global-error $name: VM stdout not empty"
    elif [ "$(wc -l < "$T/globalerr.vm.err")" -ne 1 ] || ! grep -Eq '^pc [0-9]+: (car/cdr of a non-pair|set-car! on a non-pair|tycho-scheme: attempt to call a non-function)$' "$T/globalerr.vm.err"; then
        bad "global-error $name: unexpected VM error"
    fi
}
globalerr "forward-value" '(define a b) (define b 1)'
globalerr "self-value" '(define a a)'
globalerr "forward-set" '(define a (begin (set! b 2) 0)) (define b 1)'
globalerr "forward-call" '(define a (f)) (define (f) 1)'
globalerr "nested-forward-call" '(define (f) (g)) (define a (f)) (define (g) 1)'
globalerr "mutated-call" '(define (f) 1) (set! f 2) (f)'
globalerr "redefined-call" '(define (f) 1) (define f 2) (f)'
globalerr "forward-call-args" '(define a (f (begin (display 9) 0))) (define (f x) x)'
globalerr "early-lambda-call" '(define early ((lambda () (f)))) (define (f) 1)'
# The prelude has 11 function tags. A forged pair must not enter an immutable
# global's body before its binding is initialized, even if the body is a leaf.
globalerr "checked-entry" '(define fake (cons 11 (quote ()))) (define early (fake 0)) (define (target n) 42)'
globalerr "alias-arity" '(define f (lambda (x) x)) (f)'
globalerr "global-alias-arity" '(define (f x) x) (define alias f) (alias)'

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
echo "tycho-scheme: green (7 programs byte-identical interpreter-vs-VM-compiled; interpreter golden locked on two runs; 6 compile-time rejections; 12 global binding error cases; 5 interpreter error cases die with empty stdout)"
