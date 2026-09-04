#!/bin/sh
# `vector[N]T` (V3) -- the only lane whose subject is a machine INSTRUCTION.
#
# tests/vector_type.out cannot see this feature at all: every line of it prints
# the same answer through a plain [N]T, because a vector IS a fixed array in
# every rule the language already had. What moved is the lowering -- one
# `addpd` where there was a loop -- and no golden in this tree can observe an
# instruction. So this lane reads the emitted C, measures the aggregate's
# alignment against the one the arena actually guarantees, and counts packed
# instructions in the assembly at an optimisation level where gcc's own
# auto-vectoriser is OFF.
#
# That last point is the whole design of leg [3]. At -O2 gcc vectorises the
# 4-iteration [4]float loop by itself, so "the assembly contains addpd" is TRUE
# FOR BOTH SPELLINGS and proves nothing (measured 2026-09-04: vector 8, array
# 4). At -O0 and -O1 the array version emits 0 and the vector version still
# emits 4 -- which is the actual claim, that an explicit vector does not depend
# on the optimiser noticing anything.
#
# The behaviour and refusal legs run BOTH compilers. The vector legs read
# ./tychoc only, and deliberately: tychoc1 lowers every fixed form to the
# dynamic array (compiler/emit/emit.ty@TVec), so it is a second ANSWER, not a
# second copy of the emitted text.
set -e
cd "$(dirname "$0")/.."
D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
FIX=tests/vector_type.ty
fail() { echo "vector-check: FAILED -- $1"; exit 1; }

[ -x ./tychoc ]  || fail "./tychoc is not built"
[ -x ./tychoc1 ] || fail "./tychoc1 is not built"

./tychoc "$FIX" --emit-c -o "$D/e" > /dev/null || fail "./tychoc cannot compile $FIX"

# [1] the emitted C carries a GCC vector member, and the float arithmetic is a
#     WHOLE-VECTOR assignment rather than a per-lane loop.
grep -q 'v __attribute__((vector_size(' "$D/e.c" \
    || fail "the emitted C carries no vector_size member"
grep -q '__attribute__((packed, aligned(8)))' "$D/e.c" \
    || fail "the emitted vector aggregate is not pinned to the arena's 8-byte alignment"
grep -q '_ewr\.v = _ewa\.v + _ewb\.v;' "$D/e.c" \
    || fail 'vector + did not emit a whole-vector assignment'
grep -q '_ewr\.v = _ewa\.v \* _ewb;' "$D/e.c" \
    || fail 'a broadcast did not emit a whole-vector assignment'
grep -q '_ewr\.v = _ewa\.v / _ewb\.v;' "$D/e.c" \
    || fail 'float vector / did not emit a whole-vector assignment'
# int `/` and `%` must NOT be whole-vector: they route through tycho_idiv /
# tycho_imod for the divide-by-zero and LONG_MIN/-1 traps, and a guard call
# cannot be applied to a whole vector. Losing that would be a silent SIGFPE.
grep -q 'tycho_idiv(_ewa\.v\[_ewi\], _ewb\.v\[_ewi\])' "$D/e.c" \
    || fail 'int vector / no longer goes through the runtime guard'
grep -q 'tycho_imod(_ewa\.v\[_ewi\], _ewb\.v\[_ewi\])' "$D/e.c" \
    || fail 'int vector %% no longer goes through the runtime guard' 

# [2] ALIGNMENT, the question V0 put ahead of this whole phase.
#     runtime/tycho_rt.c@arena_alloc_slow rounds every arena allocation to 8
#     bytes. A bare vector_size(32) aggregate would demand 32 and be
#     UNDER-ALIGNED there. Measure what the emitted aggregate actually asks for,
#     and strip the attribute as the control -- without the strip, an 8 could
#     just as well mean the vector member never landed.
aligns() {   # $1 = "strip" to remove the pinning attribute (the control)
    awk '/^struct TychoArrC[0-9]+_ \{.*vector_size/' "$D/e.c" > "$D/defs.c"
    [ -s "$D/defs.c" ] || fail "no vector aggregate found in the emitted C"
    if [ "$1" = strip ]; then
        sed -i 's/ __attribute__((packed, aligned(8)))//' "$D/defs.c"
        ! grep -q 'packed, aligned(8)' "$D/defs.c" || fail "control: the attribute survived the strip"
    fi
    {   echo '#include <stdio.h>'
        echo 'typedef long long tycho_int;'
        cat "$D/defs.c"
        cat <<'MAIN'
int main(void){
    size_t mx = 0;
MAIN
        sed -n 's/^struct \(TychoArrC[0-9]*_\) .*/    if (_Alignof(struct \1) > mx) mx = _Alignof(struct \1);/p' "$D/defs.c"
        cat <<'MAIN'
    printf("%zu\n", mx); return 0; }
MAIN
    } > "$D/m.c"
    cc -O2 -std=c11 -o "$D/m" "$D/m.c" || fail "the extracted vector aggregates do not compile"
    "$D/m"
}
got=$(aligns)
[ "$got" = 8 ] || fail "the widest vector aggregate wants $got-byte alignment; the arena gives 8"
ctl=$(aligns strip)
[ "$ctl" != 8 ] || fail "control: stripping the attribute left the alignment at 8 -- leg [2] cannot fail"

# [3] the INSTRUCTION, at an -O level where gcc's auto-vectoriser is off.
mkdir -p "$D/pv" "$D/ps"   # each probe gets its OWN directory: tychoc compiles
                           # every .ty beside the entry file (tycho-syntax 9)
cat > "$D/pv/v.ty" <<'TY'
package main

fn vadd(a: vector[4]float, b: vector[4]float) -> vector[4]float:
    return a + b

fn vmul(a: vector[4]float, s: float) -> vector[4]float:
    return a * s

fn main():
    a: vector[4]float = [1.0, 2.0, 3.0, 4.0]
    b: vector[4]float = [5.0, 6.0, 7.0, 8.0]
    c := vmul(vadd(a, b), 0.5)
    println(str(c[0]) + " " + str(c[3]))
TY
sed 's/vector\[4\]float/[4]float/g' "$D/pv/v.ty" > "$D/ps/s.ty"
grep -q 'vector\[' "$D/ps/s.ty" && fail "the scalar-array substitution did not apply" || true
grep -q '\[4\]float' "$D/ps/s.ty" || fail "the scalar-array substitution produced no [4]float"
./tychoc "$D/pv/v.ty" --emit-c -o "$D/v" > /dev/null || fail "the vector probe does not compile"
./tychoc "$D/ps/s.ty" --emit-c -o "$D/s" > /dev/null || fail "the array control does not compile"
packed_ops() { cc "$1" -fwrapv -std=c11 -S "$2" -o - 2>/dev/null | grep -cE '\b(addp[sd]|mulp[sd]|divp[sd]|subp[sd])\b' || true; }
for O in -O0 -O1; do
    v=$(packed_ops "$O" "$D/v.c")
    s=$(packed_ops "$O" "$D/s.c")
    [ "$v" -gt 0 ] || fail "at $O the vector program emits no packed instruction"
    [ "$s" -eq 0 ] || fail "at $O the [4]float CONTROL emits $s packed instructions -- leg [3] is not discriminating"
done
# and the two programs must still agree on the answer
./tychoc "$D/pv/v.ty" -o "$D/vb" > /dev/null || fail "the vector probe does not link"
./tychoc "$D/ps/s.ty" -o "$D/sb" > /dev/null || fail "the array control does not link"
[ "$("$D/vb")" = "$("$D/sb")" ] || fail "the vector program and its scalar control disagree"

# [4] behaviour, both compilers, against the fixture's own golden
for CC in ./tychoc ./tychoc1; do
    "$CC" "$FIX" -o "$D/b" > /dev/null || fail "$CC cannot build $FIX"
    "$D/b" > "$D/b.out" || fail "$CC: $FIX died"
    cmp -s "$D/b.out" tests/vector_type.out || fail "$CC: $FIX does not match tests/vector_type.out"
done

# [5] every refusal, by its whole sentence, in both compilers
for f in vector_count_not_pow2 vector_count_one vector_no_count \
         vector_nonconst_count vector_elem_bool elementwise_vector_array; do
    want=$(sed -n 's/^# expect: //p' "tests/reject/$f.ty")
    [ -n "$want" ] || fail "tests/reject/$f.ty has no '# expect:' line"
    for CC in ./tychoc ./tychoc1; do
        rc=0
        out=$("$CC" "tests/reject/$f.ty" --emit-c -o "$D/r" 2>&1) || rc=$?
        [ "$rc" -ne 0 ] || fail "$CC accepts tests/reject/$f.ty"
        echo "$out" | grep -qF "$want" || fail "$CC: $f refused by a different rule -- $(echo "$out" | head -1)"
    done
done
echo "vector-check: ok (emitted C carries a GCC vector; the aggregate wants 8-byte alignment and $ctl without the attribute; at -O0/-O1 the vector program emits packed instructions and the [4]float control emits none; both compilers agree on the golden and on all six refusals)"
