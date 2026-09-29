#!/bin/sh
# Regression for D15: program_in must not retain every growing struct header.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

python3 - "$tmp/structs.ty" <<'PY'
import sys
with open(sys.argv[1], "w") as out:
    for i in range(900):
        out.write(f"struct S{i:04d}:\n")
        for j in range(20):
            out.write(f"    f{j:02d}: int\n")
    out.write("fn main():\n    pass\n")
PY

# On hosts with a virtual-memory limit, keep the regression below 1.5 GiB. The
# audit repro used >4 GiB before the struct-definition builder was made linear.
if (ulimit -v 1500000) 2>/dev/null; then
    (ulimit -v 1500000 2>/dev/null || true; ./tychoc1 "$tmp/structs.ty" --emit-c -o "$tmp/out")
else
    ./tychoc1 "$tmp/structs.ty" --emit-c -o "$tmp/out"
fi
bytes=$(wc -c <"$tmp/out.c" | tr -d ' ')
[ "$bytes" -lt 5000000 ] || { echo "struct-defs: emitted $bytes bytes (expected under 5 MB)"; exit 1; }
echo "struct-defs: 900 x 20-field declarations emitted in $bytes bytes"
