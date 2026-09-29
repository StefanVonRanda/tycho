#!/bin/sh
# Keep native and self-hosted codegen aligned on deeply nested loop scopes (D03 addendum).
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
python3 - "$tmp/nested.ty" <<'PY'
import sys
from pathlib import Path
n = 125
lines = ["fn main():"]
for i in range(n):
    var = f"loop{i}"
    lines.append(" " * (4 * (i + 1)) + f"for {var} := 0; {var} < 1; {var} += 1:")
lines.append(" " * (4 * (n + 1)) + 'println("nested-blocks-ok")')
Path(sys.argv[1]).write_text("\n".join(lines) + "\n")
PY
for compiler in ./tychoc ./tychoc1; do
    exe="$tmp/$(basename "$compiler")"
    (ulimit -v 1228800; "$compiler" "$tmp/nested.ty" -o "$exe" >/dev/null)
    got=$("$exe")
    [ "$got" = nested-blocks-ok ] || {
        echo "$compiler nested-block output mismatch: '$got'" >&2
        exit 1
    }
done
echo "nested-blocks-scale: 125 nested loop scopes compile and run under both compilers"
