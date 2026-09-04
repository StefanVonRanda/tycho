#!/bin/sh
# `packed struct` (V2) -- the only lane whose subject is a struct's LAYOUT.
#
# A golden cannot see this feature. `tests/packed_struct.ty` prints the same
# four lines whether the attribute reached the emitted C or was dropped on the
# floor: every field still reads back correctly through a natural-layout struct.
# What moved is sizeof, and nothing in the tree could observe it. So this lane
# reads the emitted C, compiles the two struct definitions on their own, and
# compares the two sizes -- with the attribute STRIPPED as the control, which is
# what proves the comparison can fail.
#
# Runs both compilers: a feature landing in one and not the other is the hole
# `scripts/diag_coverage.py` exists to keep shut.
set -e
cd "$(dirname "$0")/.."
D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
FIX=tests/packed_struct.ty
fail() { echo "packed-check: FAILED -- $1"; exit 1; }

# The two definitions out of an emitted .c, plus a main that prints both sizes.
sizes() {   # $1 = emitted .c   $2 = "strip" to delete the attribute (control)
    awk '/^struct S_(Ins|Loose)_ \{/,/^\}.*;$/' "$1" > "$D/defs.c"
    [ -s "$D/defs.c" ] || fail "no struct definitions found in $1"
    if [ "$2" = strip ]; then
        sed -i 's/ __attribute__((packed))//' "$D/defs.c"
        ! grep -q '__attribute__((packed))' "$D/defs.c" || fail "control: the attribute survived the strip"
    fi
    {   echo '#include <stdio.h>'
        cat "$D/defs.c"
        # a quoted heredoc, not echo: /bin/sh here is dash and its echo expands
        # the \n, which split the printf format across two lines (tycho-verify 3)
        cat <<'MAIN'
int main(void){printf("%zu %zu\n",sizeof(struct S_Ins_),sizeof(struct S_Loose_));return 0;}
MAIN
    } > "$D/m.c"
    cc -O2 -o "$D/m" "$D/m.c" || fail "the extracted definitions do not compile ($1)"
    "$D/m"
}

for CC in ./tychoc ./tychoc1; do
    [ -x "$CC" ] || fail "$CC is not built"
    "$CC" "$FIX" --emit-c -o "$D/e" > /dev/null || fail "$CC cannot compile $FIX"

    # [1] the attribute is on the packed struct and NOT on the control struct
    grep -q '^struct S_Ins_ {' "$D/e.c"   || fail "$CC: no S_Ins_ in the emitted C"
    awk '/^struct S_Ins_ \{/,/^\}.*;$/'   "$D/e.c" | grep -q '__attribute__((packed))' \
        || fail "$CC: the emitted packed struct carries no attribute"
    awk '/^struct S_Loose_ \{/,/^\}.*;$/' "$D/e.c" | grep -q '__attribute__((packed))' \
        && fail "$CC: the UNPACKED control struct carries the attribute" || true

    # [2] the sizes differ, and by the amount the field widths predict:
    #     u8 + i32 + i32 is 9 packed and 12 with the natural 4-byte alignment.
    got=$(sizes "$D/e.c")
    [ "$got" = "9 12" ] || fail "$CC: sizeof(packed) sizeof(loose) is '$got', expected '9 12'"

    # [3] the control: with the attribute stripped the two must AGREE, or leg [2]
    #     was measuring something other than the attribute.
    ctl=$(sizes "$D/e.c" strip)
    [ "$ctl" = "12 12" ] || fail "$CC: control (attribute stripped) is '$ctl', expected '12 12'"

    # [4] both refusals, by their whole sentence
    for f in packed_on_enum packed_heap_field; do
        want=$(sed -n 's/^# expect: //p' "tests/reject/$f.ty")
        [ -n "$want" ] || fail "tests/reject/$f.ty has no '# expect:' line"
        rc=0
        out=$("$CC" "tests/reject/$f.ty" --emit-c -o "$D/r" 2>&1) || rc=$?
        [ "$rc" -ne 0 ] || fail "$CC accepts tests/reject/$f.ty"
        echo "$out" | grep -qF "$want" || fail "$CC: $f refused by a different rule -- $(echo "$out" | head -1)"
    done
done
echo "packed-check: ok (both compilers; packed 9 vs unpacked 12, control 12 vs 12)"
