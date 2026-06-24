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
# Both compilers must reject each with a nonzero exit that is NOT a signal
# (rc < 128), and must still accept the matching "valid, modestly nested" case.
# Inputs are generated here (megabytes at the cap) rather than committed.
# No `set -e`: the reject cases expect nonzero compiler exits, checked explicitly.
cd "$(dirname "$0")/../.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
"$TYCHOC" compiler/tychoc0.ty -o "$T/h0" >/dev/null 2>&1 || { echo "FAIL: could not build tychoc0"; exit 1; }

# Every compile of a pathological fixture runs under a memory + CPU cap. tychoc0
# has value semantics, so a deep type/expression deep-copies O(n^2) and, without
# a cap fired first, would exhaust host RAM (the OS OOM-killer then takes down
# the whole session). The ulimit makes the compiler's own malloc fail closed.
LIMV=1500000   # virtual-memory ceiling (KiB ~= 1.5 GB)
TMO=30         # wall-clock ceiling (s)
run() { ( ulimit -v "$LIMV"; timeout "$TMO" "$@" ); }

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
# A pathological input: BOTH compilers must reject it cleanly (nonzero, not a signal).
reject() {
    name="$1"; f="$T/$2.ty"
    run "$TYCHOC" "$f" -o "$T/c.bin" >/dev/null 2>&1; rc=$?
    if [ "$rc" -eq 0 ]; then echo "FAIL $name (tychoc ACCEPTED it)"; fail=1
    elif [ "$rc" -ge 128 ]; then echo "FAIL $name (tychoc died on signal $((rc-128)) -- stack overflow)"; fail=1
    else echo "ok    $name (tychoc rejected, rc=$rc)"; fi
    run "$T/h0" < "$f" >/dev/null 2>&1; rc=$?
    if [ "$rc" -eq 0 ]; then echo "FAIL $name (tychoc0 ACCEPTED it)"; fail=1
    elif [ "$rc" -ge 128 ]; then echo "FAIL $name (tychoc0 died on signal $((rc-128)) -- stack overflow)"; fail=1
    else echo "ok    $name (tychoc0 rejected, rc=$rc)"; fi
}
# A valid input: BOTH compilers must accept it (emit C without error).
accept() {
    name="$1"; f="$T/$2.ty"
    if ! run "$TYCHOC" "$f" -o "$T/c.bin" >/dev/null 2>&1; then echo "FAIL $name (tychoc rejected a valid program)"; fail=1
    else echo "ok    $name (tychoc accepted)"; fi
    if ! run "$T/h0" < "$f" >/dev/null 2>&1; then echo "FAIL $name (tychoc0 rejected a valid program)"; fail=1
    else echo "ok    $name (tychoc0 accepted)"; fi
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

[ "$fail" -eq 0 ] && echo "recursion-cap: all green (fail closed on deep input, no stack overflow)" || echo "recursion-cap: FAIL"
exit "$fail"
