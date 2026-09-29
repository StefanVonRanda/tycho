#!/bin/sh
# D13: nested generic calls and UFCS chains must not emit/recheck each receiver
# repeatedly. The old self-hosted emitter exhausted >1 GiB at a 16-deep chain.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

python3 - "$tmp/nested.ty" <<'PY'
import sys
out = open(sys.argv[1], "w")
depth = 32
out.write("struct Wrap($T):\n    value: T\n\nenum Nest($T):\n    Next(T)\n\n")
out.write("fn id(x: $T) -> $T:\n    return x\n\n")
out.write("fn hop(x: Wrap($T)) -> Wrap($T):\n    return Wrap(x.value)\n\nfn main():\n")
expr = "7"
for _ in range(depth):
    expr = f"Wrap({expr})"
out.write(f"    boxed := {expr}\n")
out.write("    println(str(boxed" + ".value" * depth + "))\n")
expr = "9"
for _ in range(depth):
    expr = f"Next({expr})"
out.write(f'    nested := {expr}\n    match nested:\n        Next(_): println("enum")\n')
expr = "11"
for _ in range(depth * 2):
    expr = f"id({expr})"
out.write(f"    println(str({expr}))\n")
expr = "Wrap(3)"
for _ in range(depth * 2):
    expr += ".hop()"
out.write(f"    hopped := {expr}\n    println(str(hopped.value))\n")
PY

printf '7\nenum\n11\n3\n' >"$tmp/expected"
run_compiler() {
    name=$1
    compiler=$2
    binary="$tmp/$name"
    if (ulimit -v 1200000) 2>/dev/null; then
        (ulimit -v 1200000 2>/dev/null || true; timeout 20s "$compiler" "$tmp/nested.ty" -o "$binary")
    else
        timeout 20s "$compiler" "$tmp/nested.ty" -o "$binary"
    fi
    "$binary" >"$tmp/$name.out"
    diff -u "$tmp/expected" "$tmp/$name.out"
}
run_compiler native ./tychoc
run_compiler selfhosted ./tychoc1
echo "emitter-nested-calls: 64-deep UFCS and 32-deep generic constructors pass under both compilers"
