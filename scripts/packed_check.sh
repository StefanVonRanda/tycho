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

    # [4] every refusal, by its whole sentence
    for f in packed_on_enum packed_heap_field \
             from_bytes_two_args from_bytes_two_typeargs from_bytes_not_packed \
             from_bytes_not_bytes size_of_value_args size_of_two_typeargs \
             size_of_not_packed to_bytes_not_packed_struct; do
        want=$(sed -n 's/^# expect: //p' "tests/reject/$f.ty")
        [ -n "$want" ] || fail "tests/reject/$f.ty has no '# expect:' line"
        rc=0
        out=$("$CC" "tests/reject/$f.ty" --emit-c -o "$D/r" 2>&1) || rc=$?
        [ "$rc" -ne 0 ] || fail "$CC accepts tests/reject/$f.ty"
        echo "$out" | grep -qF "$want" || fail "$CC: $f refused by a different rule -- $(echo "$out" | head -1)"
    done

    # [5] the BYTES BRIDGE (V2b). A golden cannot see this either: the round trip
    #     `from_bytes(to_bytes(v)) == v` holds for ANY self-consistent byte order,
    #     so a host-order implementation and a little-endian one print the same
    #     two lines. What pins it is the byte STRING, asserted against a literal
    #     here where RECORD= cannot reach it: u16 258 is `2 1`, u32 2^32-1 is four
    #     0xFF, i32 -2 is `254 255 255 255`, and f32 1.0 is IEEE-754 0x3F800000
    #     written low byte first. Every one of those is a different answer on a
    #     big-endian host, which is the bug no test on this machine can see.
    cat > "$D/rt.ty" <<'TY'
packed struct Rec:
    a: u8
    b: u16
    c: u32
    d: i32
    e: f32

fn main():
    r := Rec(to_u8(1), to_u16(258), to_u32(4294967295), to_i32(-2), to_f32(1.0))
    b := to_bytes(r)
    out := str(size_of$(Rec))
    for k := 0; k < len(b); k += 1:
        out = out + " " + str(b[k])
    println(out)
    q := from_bytes$(Rec)(b)
    println(str(q.a) + " " + str(q.b) + " " + str(q.c) + " " + str(q.d) + " " + str(to_int(q.e)))
TY
    "$CC" "$D/rt.ty" -o "$D/rt" > /dev/null || fail "$CC cannot build the round-trip probe"
    got=$("$D/rt") || fail "$CC: the round-trip probe died"
    want='15 1 2 1 255 255 255 255 254 255 255 255 0 0 128 63
1 258 4294967295 -2 1'
    [ "$got" = "$want" ] || fail "$CC: the packed encoding moved -- got '$got'"

    # [6] a length that does not match is refused BY NAME, with nothing of the
    #     value on stdout. Exact, not a lower bound: one byte short must fail.
    cat > "$D/short.ty" <<'TY'
packed struct Rec:
    a: u8
    c: u32

fn main():
    r := Rec(to_u8(1), to_u32(2))
    b := to_bytes(r)
    n := len(b) - NBACK
    q := from_bytes$(Rec)(to_bytes(to_str(b)[0:n]))
    println("read " + str(q.a))
TY
    sed 's/NBACK/1/' "$D/short.ty" > "$D/s1.ty"
    grep -q 'len(b) - 1' "$D/s1.ty" || fail "the short-input substitution did not apply"
    "$CC" "$D/s1.ty" -o "$D/s1" > /dev/null || fail "$CC cannot build the short-input probe"
    rc=0
    out=$("$D/s1" 2>"$D/s1.err") || rc=$?
    [ "$rc" -ne 0 ] || fail "$CC: from_bytes accepted a 4-byte buffer for a 5-byte struct"
    [ -z "$out" ] || fail "$CC: the refused read still printed '$out'"
    grep -qF 'from_bytes$(Rec): expected 5 bytes, got 4' "$D/s1.err" \
        || fail "$CC: refused by a different rule -- $(head -1 "$D/s1.err")"

    # [7] the CONTROL for [6]: the same program at the exact length must SUCCEED.
    #     Without it, "refused" and "from_bytes refuses everything" look alike.
    sed 's/NBACK/0/' "$D/short.ty" > "$D/s0.ty"
    grep -q 'len(b) - 0' "$D/s0.ty" || fail "the exact-input substitution did not apply"
    "$CC" "$D/s0.ty" -o "$D/s0" > /dev/null || fail "$CC cannot build the exact-input probe"
    [ "$("$D/s0")" = "read 1" ] || fail "$CC: the exact-length read did not succeed"
done
echo "packed-check: ok (both compilers; packed 9 vs unpacked 12, control 12 vs 12; the bridge round-trips little-endian, a short buffer is refused by name and the exact one is not)"
