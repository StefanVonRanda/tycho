#!/bin/sh
# Regression for D18: large scalar constant arrays should not create one
# optimizer-visible C store per element.
set -eu
cd "$(dirname "$0")/.."
. ./scripts/shlib.sh
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

python3 - "$tmp/table.ty" <<'PY'
import sys
n = 15625
with open(sys.argv[1], "w") as out:
    out.write("fn main():\n    xs := [" + ", ".join(str(i % 1000) for i in range(n)) + "]\n")
    out.write('    println(str(xs[0]) + " " + str(xs[15624]))\n')
PY

for compiler in tychoc tychoc1; do
    ./$compiler "$tmp/table.ty" --emit-c -o "$tmp/$compiler.emit"
    grep -Eq 'static const tycho_int +_lit' "$tmp/$compiler.emit.c" || {
        echo "array-literal-scale: $compiler did not use a static constant table"; exit 1;
    }
    TYCHO_CFLAGS=-O3 timeout 30 ./$compiler "$tmp/table.ty" -o "$tmp/$compiler.bin"
    got=$("$tmp/$compiler.bin")
    [ "$got" = "0 624" ] || { echo "array-literal-scale: $compiler printed '$got'"; exit 1; }
done
echo "array-literal-scale: 15625-element constant tables build under -O3 with both compilers"
