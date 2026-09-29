#!/bin/sh
# Regression for the elif-chain scaling failures D03/D14/D23. Keep the large
# source synthetic: a committed 20,000-line fixture would add noise, not signal.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

gen_chain() {
    path=$1
    n=$2
    python3 - "$path" "$n" <<'PY'
import sys
path, n = sys.argv[1], int(sys.argv[2])
with open(path, "w") as out:
    out.write("fn main():\n")
    for i in range(n):
        out.write("    " + ("if" if i == 0 else "elif") + " false:\n        pass\n")
    out.write("    else:\n        println(str(" + str(n) + "))\n")
PY
}

check() {
    compiler=$1
    n=$2
    cc=${3:-}
    src="$tmp/elif$n.ty"
    exe="$tmp/$(basename "$compiler")-$n-${cc:-default}"
    gen_chain "$src" "$n"
    cc=${cc:-cc}
    "$compiler" "$src" --emit-c -o "$exe"
    cbytes=$(wc -c < "$exe.c" | tr -d ' ')
    # Runtime/preamble dominates the file; quadratic nested indentation would
    # already be hundreds of MB at this arm count.
    [ "$cbytes" -lt 1000000 ] || { echo "$compiler: elif$n emitted $cbytes bytes (expected linear output)"; exit 1; }
    "$cc" -O2 -fwrapv -ffp-contract=off -pthread -Icorelib -o "$exe" "$exe.c" -lm
    got=$("$exe")
    [ "$got" = "$n" ] || { echo "$compiler: elif$n printed '$got', expected $n"; exit 1; }
}

for compiler in ./tychoc ./tychoc1; do
    check "$compiler" 10000
    if command -v clang >/dev/null 2>&1; then
        check "$compiler" 300 clang
    else
        echo "elif-chain: clang unavailable; skipping clang bracket-depth check"
        break
    fi
done
echo "elif-chain: 10000-arm gcc and 300-arm clang cases passed for both compilers"
