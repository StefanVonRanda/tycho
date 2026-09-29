#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
python3 - "$tmp/depth_recovery.ty" <<'PY'
import sys
with open(sys.argv[1], "w") as f:
    f.write("fn main():\n")
    f.write("    println(missing)\n" * 2105)
PY
for compiler in tychoc tychoc1; do
    out="$tmp/$compiler.log"
    if "$root/$compiler" "$tmp/depth_recovery.ty" --emit-c >"$out" 2>&1; then
        echo "depth-recovery: $compiler unexpectedly accepted unresolved names" >&2
        exit 1
    fi
    if grep -q 'expression too deeply nested' "$out"; then
        echo "depth-recovery: $compiler leaked recursion depth across recovered errors" >&2
        exit 1
    fi
    count=$(grep -c "unknown variable 'missing'" "$out" || true)
    if [ "$count" -lt 2000 ]; then
        echo "depth-recovery: $compiler reported only $count of 2105 independent errors" >&2
        exit 1
    fi
done
echo "depth-recovery: both compilers recovered all 2105 independent errors"
