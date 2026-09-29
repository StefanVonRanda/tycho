#!/bin/sh
# Exercise the sequencing regression with both emitters and both C compilers.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
ccs="cc"
if command -v clang >/dev/null 2>&1; then ccs="$ccs clang"; fi
for compiler in tychoc tychoc1; do
    for cc in $ccs; do
        exe="$tmp/$compiler-$cc"
        ./$compiler tests/place_store_order.ty --cc "$cc" -o "$exe"
        "$exe" >"$tmp/out"
        cmp tests/place_store_order.out "$tmp/out"
    done
done
echo "place-store-order: both compilers preserve sequencing with gcc/clang"
