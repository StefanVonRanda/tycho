#!/bin/sh
# Regression for D19: f-string C joins and lambda local-table handling must
# remain linear in the source size (the audit cases needed multiple GiB).
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

python3 - "$tmp/holes.ty" "$tmp/lambdas.ty" <<'PY'
import sys
with open(sys.argv[1], "w") as out:
    out.write('fn main():\n    x := 1\n    s := f"' + "-".join("{x}" for _ in range(4000)) + '"\n')
    out.write("    println(str(len(s)))\n")
with open(sys.argv[2], "w") as out:
    out.write("fn main():\n    total := 0\n")
    for i in range(8000):
        name = f"local_{i:04d}"
        closure = f"closure_{i:04d}"
        out.write(f"    {name} := {i}\n")
        out.write(f"    {closure} := fn() -> int: {name}\n")
        out.write(f"    total = total + {closure}()\n")
    out.write("    println(str(total))\n")
PY

emit_limited() {
    source=$1
    output=$2
    if (ulimit -v 1200000) 2>/dev/null; then
        (ulimit -v 1200000 2>/dev/null || true; timeout 90 ./tychoc1 "$source" --emit-c -o "$output")
    else
        timeout 90 ./tychoc1 "$source" --emit-c -o "$output"
    fi
}
emit_limited "$tmp/holes.ty" "$tmp/holes"
emit_limited "$tmp/lambdas.ty" "$tmp/lambdas"
holes_bytes=$(wc -c <"$tmp/holes.c" | tr -d ' ')
lambda_bytes=$(wc -c <"$tmp/lambdas.c" | tr -d ' ')
[ "$holes_bytes" -gt 10000 ] && [ "$holes_bytes" -lt 2000000 ] || {
    echo "emitter-memory-scale: 4000-hole output size is $holes_bytes bytes"; exit 1;
}
[ "$lambda_bytes" -gt 1000000 ] && [ "$lambda_bytes" -lt 20000000 ] || {
    echo "emitter-memory-scale: 8000-lambda output size is $lambda_bytes bytes"; exit 1;
}
echo "emitter-memory-scale: 4000 f-string holes and 8000 captured lambdas emitted within 1.2 GiB"
