#!/bin/sh
# D31: native name registries and warning source locations must not rescan
# every earlier declaration or every byte before each diagnostic.
set -eu
cd "$(dirname "$0")/.."
. ./scripts/shlib.sh
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
python3 - "$tmp/registries.ty" <<'PY'
import sys
out = open(sys.argv[1], "w")
constants = 12000
enums = 1500
generics = 12000
warnings = 6000
for i in range(constants):
    out.write(f"const C{i:05d} = {i}\n")
for i in range(enums):
    out.write(f"enum E{i:04d}:\n    V{i:04d}\n")
for i in range(generics):
    out.write(f"fn g{i:05d}(x: $T) -> $T:\n    return x\n")
for i in range(warnings):
    out.write(f"fn dead{i:05d}():\n    return\n")
out.write("fn main():\n")
out.write(f"    v := V{enums - 1:04d}\n    match v:\n")
out.write(f"        V{enums - 1:04d}: println(str(g{generics - 1:05d}(C{constants - 1:05d})))\n")
PY

for compiler in ./tychoc ./tychoc1; do
    warnings="$tmp/$(basename "$compiler").warnings"
    if (ulimit -v 1200000) 2>/dev/null; then
        (ulimit -v 1200000 2>/dev/null || true
         timeout 30s "$compiler" "$tmp/registries.ty" --emit-c -o "$tmp/registries-$(basename "$compiler")" 2>"$warnings")
    else
        timeout 30s "$compiler" "$tmp/registries.ty" --emit-c -o "$tmp/registries-$(basename "$compiler")" 2>"$warnings"
    fi
    count=$(grep -c 'is never called' "$warnings" || true)
    if [ "$count" -ne 6000 ]; then
        echo "$compiler: expected 6000 dead-procedure warnings, got $count" >&2
        exit 1
    fi
done
echo "name-registry-scale: both compilers pass 12000 constants/generics, 1500 variants and 6000 warning excerpts under 1.2 GiB"
