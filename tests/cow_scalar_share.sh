#!/bin/sh
# Regression: a scalar-map read inside an `inout` helper must not use the O6
# shallow-read path. Before the fix, `idx[key]` emitted `get_share`, marked the
# whole [int:int] map shared, and the next `idx[key] = ...` copied all of it.
# bench/lru grew from 21 MB to 121 GB that way. The output alone cannot catch
# this (it is correct, just quadratic), so assert on the emitted C.
set -eu
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cat > "$tmp/scalar.ty" <<'TY'
fn touch(idx: inout [int: int], key: int) -> int:
    if key in idx:
        return idx[key]
    idx[key] = key
    return 0

fn main():
    m := []int: int
    reserve(m, 4)
    s := 0
    for i := 0; i < 8; i += 1:
        s = s + touch(&m, i % 4)
    println(str(len(m)) + " " + str(s))
TY

for compiler in tychoc tychoc1; do
    ./$compiler "$tmp/scalar.ty" --emit-c -o "$tmp/$compiler.emit" >/dev/null
    if grep -q 'get_share(&' "$tmp/$compiler.emit.c"; then
        echo "cow-scalar-share: $compiler emitted a shared read for [int:int]" >&2
        exit 1
    fi
    $CC -O2 -o "$tmp/$compiler.bin" "$tmp/$compiler.emit.c"
    got=$("$tmp/$compiler.bin")
    [ "$got" = "4 6" ] || {
        echo "cow-scalar-share: $compiler printed '$got' (expected '4 6')" >&2
        exit 1
    }
done
echo "cow-scalar-share: both compilers keep scalar-map reads non-sharing"
