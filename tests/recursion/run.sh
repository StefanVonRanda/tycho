#!/bin/sh
# Recursion-cap regression: the compiler must FAIL CLOSED on pathologically
# nested / long input instead of overflowing the C stack (SIGSEGV, exit 139).
# Covers every recursion vector we hardened:
#   - deep parenthesis nesting        (recursive-descent parse stack)
#   - deep unary `not` chain          (recursive-descent parse stack)
#   - long left-leaning operator chain (deep AST tree -> resolve/clone walkers;
#                                       tychoc0 also deep-copies on construction)
#   - long chain inside a GENERIC body (clone_expr, which precedes resolve)
#   - deep statement nesting           (parse_block recursion / indent stack)
# tychoc must reject each with a nonzero exit that is NOT a signal (rc < 128),
# and must still accept the matching "valid, modestly nested" case. (Until
# 2026-07-26 the same inputs were also fed to the self-hosted tychoc0; it is
# frozen -- see compiler/tychoc0.ty -- and no gate builds it, so that half is
# gone. Every assertion about tychoc is unchanged.)
# Inputs are generated here (megabytes at the cap) rather than committed.
# No `set -e`: the reject cases expect nonzero compiler exits, checked explicitly.
cd "$(dirname "$0")/../.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
# Every compile of a pathological fixture runs under a memory + CPU cap. tychoc0
# has value semantics, so a deep type/expression deep-copies O(n^2) and, without
# a cap fired first, would exhaust host RAM (the OS OOM-killer then takes down
# the whole session). The ulimit makes the compiler's own malloc fail closed.
LIMV=1500000   # virtual-memory ceiling (KiB ~= 1.5 GB), Linux only
TMO=30         # wall-clock / CPU ceiling (s)
# GNU `timeout` and `ulimit -v` (RLIMIT_AS) are Linux-only — macOS ships
# neither. `ulimit -t` (CPU seconds) is portable and, with the compilers'
# recursion cap firing at <100 MB here, bounds the runaway O(n^2) copy on macOS
# too. Add a real wall-clock timeout only where one exists.
if command -v timeout >/dev/null 2>&1; then TO="timeout $TMO"
elif command -v gtimeout >/dev/null 2>&1; then TO="gtimeout $TMO"
else TO=""; fi
if ( ulimit -v "$LIMV" ) 2>/dev/null; then AS_CAP="ulimit -v $LIMV"; else AS_CAP=":"; fi
run() { ( ulimit -t "$TMO"; $AS_CAP; $TO "$@" ); }

py() { python3 - "$@"; }

# Generate the fixtures (depth 6000 / chains 200k are well past both the 2000
# expression cap and the 256 indentation cap, and past the observed SIGSEGV
# depth, so a surviving stack guard is the only way to exit cleanly).
py >"$T/paren.ty"   <<'P'
import sys; print("fn main():\n    x := " + "("*6000 + "1" + ")"*6000 + "\n    print(str(x))")
P
py >"$T/unary.ty"   <<'P'
import sys; print("fn main():\n    b := " + "not "*6000 + "true\n    print(str(b))")
P
py >"$T/chain.ty"   <<'P'
import sys; print("fn main():\n    x := 1" + "+1"*200000 + "\n    print(str(x))")
P
py >"$T/generic.ty" <<'P'
import sys; print("fn gdeep(x: $T) -> $T:\n    y := 1" + "+1"*200000 + "\n    return x\nfn main():\n    print(str(gdeep(5)))")
P
py >"$T/stmt.ty"    <<'P'
import sys
out=["fn main():"]
for i in range(4000): out.append("    "*(i+1)+"if true:")
out.append("    "*4001+"print(str(1))")
print("\n".join(out))
P
# Valid, modestly nested counterparts -- must COMPILE (guard must not be trigger-happy).
py >"$T/ok_expr.ty" <<'P'
import sys; print("fn main():\n    x := " + "("*100 + "1" + ")"*100 + " + 2\n    print(str(x))")
P
py >"$T/ok_chain.ty" <<'P'
import sys; print("fn main():\n    x := 1" + "+1"*500 + "\n    print(str(x))")
P
py >"$T/ok_stmt.ty" <<'P'
import sys
out=["fn main():"]
for i in range(100): out.append("    "*(i+1)+"if true:")
out.append("    "*101+"print(str(7))")
print("\n".join(out))
P
# Deeply nested TYPE annotations -- parse_type recursion. tychoc SIGSEGVs and
# tychoc0 OOMs the host here without a cap. Array nesting AND Option(...) nesting
# (the latter recurses through the named-type branch) must both be bounded.
py >"$T/type_arr.ty" <<'P'
import sys; print("fn f(a: " + "["*9000 + "int" + "]"*9000 + "):\n    return\nfn main():\n    return")
P
py >"$T/type_opt.ty" <<'P'
import sys; print("fn f(a: " + "Option("*9000 + "int" + ")"*9000 + "):\n    return\nfn main():\n    return")
P
py >"$T/ok_type.ty" <<'P'
import sys; print("fn g(a: [[[int]]], b: Option(int)) -> int:\n    return 0\nfn main():\n    print(str(g([[[1]]], None)))")
P

fail=0
# A pathological input: tychoc must reject it cleanly (nonzero, not a signal).
reject() {
    name="$1"; f="$T/$2.ty"
    run "$TYCHOC" "$f" -o "$T/c.bin" >/dev/null 2>&1; rc=$?
    if [ "$rc" -eq 0 ]; then echo "FAIL $name (tychoc ACCEPTED it)"; fail=1
    elif [ "$rc" -ge 128 ]; then echo "FAIL $name (tychoc died on signal $((rc-128)) -- stack overflow)"; fail=1
    else echo "ok    $name (tychoc rejected, rc=$rc)"; fi
}
# A valid input: tychoc must accept it (emit C without error).
accept() {
    name="$1"; f="$T/$2.ty"
    if ! run "$TYCHOC" "$f" -o "$T/c.bin" >/dev/null 2>&1; then echo "FAIL $name (tychoc rejected a valid program)"; fail=1
    else echo "ok    $name (tychoc accepted)"; fi
}

reject "paren-nest"      paren
reject "unary-chain"     unary
reject "operator-chain"  chain
reject "generic-body"    generic
reject "stmt-nest"       stmt
reject "type-arr-nest"   type_arr
reject "type-opt-nest"   type_opt
accept "valid-expr"      ok_expr
accept "valid-chain"     ok_chain
accept "valid-stmt"      ok_stmt
accept "valid-type"      ok_type

# ---- generated-code side (`docs/internals/plan-tycho-scheme-DONE.md` phase 1): deep recursion in a PROGRAM -------
# The reject/accept above guard the COMPILER's own recursion. Until the
# stack-overflow guard landed in the runtime (tycho_rt.c), deep recursion in
# emitted code died with SIGSEGV -- no diagnostic, no cleanup. Two measured
# victims: the Scheme interpreter at ~5k levels (eval-apply chain, big frames)
# and the json walker at ~100k nests (parse_value, small frames). Each deep
# program must COMPILE (it is valid Tycho), then DIE CLEANLY at runtime:
# exit 1-127 (NOT a signal), empty stdout, "stack overflow" on stderr. The
# modestly-nested counterpart must run and print the right answer.
py >"$T/prog_big.ty"   <<'P'
import sys; print("fn f(n: int) -> int:\n    if n <= 0:\n        return 1\n    return n + f(n - 1)\nfn main():\n    print(str(f(2000000)))")
P
py >"$T/prog_small.ty" <<'P'
import sys; print("fn f(n: int) -> int:\n    if n <= 0:\n        return 0\n    return f(n - 1)\nfn main():\n    print(str(f(2000000)))")
P
py >"$T/prog_spawn.ty" <<'P'
import sys; print("fn deep(n: int) -> int:\n    if n <= 0:\n        return 0\n    return deep(n - 1)\nfn tm() -> int:\n    return deep(2000000)\nfn main():\n    t := spawn tm()\n    print(str(t.wait()))")
P
py >"$T/prog_ok_big.ty" <<'P'
import sys; print("fn f(n: int) -> int:\n    if n <= 0:\n        return 1\n    return n + f(n - 1)\nfn main():\n    print(str(f(1000)))")
P
py >"$T/prog_ok_spawn.ty" <<'P'
import sys; print("fn deep(n: int) -> int:\n    if n <= 0:\n        return 0\n    return deep(n - 1)\nfn tm() -> int:\n    return deep(1000)\nfn main():\n    t := spawn tm()\n    print(str(t.wait()))")
P

# A deep program: compiles, then fails CLOSED at runtime.
progdie() {
    name="$1"; f="$T/$2.ty"
    if ! run "$TYCHOC" "$f" --emit-c -o "$T/$2" >"$T/$2.log" 2>&1; then
        echo "FAIL $name (compile)"; sed 's/^/      /' "$T/$2.log"; fail=1; return
    fi
    if ! $CC -O2 -fwrapv -std=c11 -o "$T/$2.bin" "$T/$2.c" -lm >"$T/$2.cc.log" 2>&1; then
        echo "FAIL $name (cc)"; sed 's/^/      /' "$T/$2.cc.log"; fail=1; return
    fi
    out=$("$T/$2.bin" 2>"$T/$2.err"); rc=$?
    if [ "$rc" -eq 0 ] || [ "$rc" -ge 128 ]; then
        echo "FAIL $name (exit $rc -- must fail closed, 1-127, never a signal)"; fail=1; return
    fi
    if [ -n "$out" ]; then echo "FAIL $name (stdout not empty: '$out')"; fail=1; return; fi
    if ! grep -q "stack overflow" "$T/$2.err"; then
        echo "FAIL $name (stderr lacks the diagnostic)"; sed 's/^/      /' "$T/$2.err"; fail=1; return
    fi
    echo "ok    $name (program died cleanly, rc=$rc)"
}
# The modestly-nested counterpart: must compile AND run, printing its answer.
progrun() {
    name="$1"; f="$T/$2.ty"; expect="$3"
    if ! run "$TYCHOC" "$f" --emit-c -o "$T/$2" >"$T/$2.log" 2>&1; then
        echo "FAIL $name (compile)"; sed 's/^/      /' "$T/$2.log"; fail=1; return
    fi
    if ! $CC -O2 -fwrapv -std=c11 -o "$T/$2.bin" "$T/$2.c" -lm >"$T/$2.cc.log" 2>&1; then
        echo "FAIL $name (cc)"; sed 's/^/      /' "$T/$2.cc.log"; fail=1; return
    fi
    got=$("$T/$2.bin" 2>&1); rc=$?
    if [ "$rc" -ne 0 ]; then echo "FAIL $name (exit $rc)"; fail=1; return; fi
    if [ "$got" != "$expect" ]; then echo "FAIL $name (got '$got', want '$expect')"; fail=1; return; fi
    echo "ok    $name (ran, output '$got')"
}

progdie "prog-deep-big"    prog_big
progdie "prog-deep-small"  prog_small
progdie "prog-deep-spawn"  prog_spawn
progrun "prog-ok-big"      prog_ok_big    "500501"
progrun "prog-ok-spawn"    prog_ok_spawn  "0"

[ "$fail" -eq 0 ] && echo "recursion-cap: all green (fail closed on deep input, no stack overflow)" || echo "recursion-cap: FAIL"
exit "$fail"
