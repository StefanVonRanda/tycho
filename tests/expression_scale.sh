#!/bin/sh
# Regressions for deep left-associated sums and f-string C bracket depth (D16/D17).
set -eu
cd "$(dirname "$0")/.."
# Run at the platform's default stack, not the parent's. `make` raises
# RLIMIT_STACK, and under it Apple clang survived a 1,402-deep paren nest the
# reference compiler emitted until 2026-10-16; the same pin failed from a plain
# shell. A pin that passes only under make's limits is not pinning the compiler.
ulimit -s 8192 2>/dev/null || true
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

python3 - "$tmp" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1])
for count, name in ((1400, "sum1400"), (256, "sum256"), (2101, "sum2101")):
    (p / f"{name}.ty").write_text(
        "fn main():\n    x := 1\n    println(str(" + "+".join(["x"] * count) + "))\n"
    )
(p / "sub2101.ty").write_text(
    "fn main():\n    x := 1\n    println(str(" + "-".join(["x"] * 2101) + "))\n"
)
parts = "-".join(["{x}"] * 130)
(p / "fstr130.ty").write_text(
    'fn main():\n    x := 1\n    s := f"' + parts + '"\n    println(str(len(s)))\n'
)
call = "1"
for _ in range(257):
    call = "f(" + call + ")"
(p / "deep257.ty").write_text(
    "fn f(x: int) -> int:\n    return x\nfn main():\n    println(str(" + call + "))\n"
)
(p / "unary257.ty").write_text(
    "fn main():\n    println(str(" + "-" * 257 + "1))\n"
)
PY

for compiler in ./tychoc ./tychoc1; do
    exe="$tmp/$(basename "$compiler")-sum1400"
    "$compiler" "$tmp/sum1400.ty" --cc cc -o "$exe"
    got=$("$exe")
    [ "$got" = 1400 ] || { echo "$compiler sum1400 printed '$got'"; exit 1; }
    if "$compiler" "$tmp/sum2101.ty" --emit-c -o "$tmp/too-deep" >"$tmp/deep.err" 2>&1; then
        echo "$compiler accepted a 2101-term integer expression"; exit 1
    fi
    grep -q "expression too deeply nested to type-check (max 2000)" "$tmp/deep.err" || {
        echo "$compiler did not give the expression-depth diagnostic"; cat "$tmp/deep.err"; exit 1;
    }
    if "$compiler" "$tmp/sub2101.ty" --emit-c -o "$tmp/sub-too-deep" >"$tmp/sub.err" 2>&1; then
        echo "$compiler accepted a 2101-term subtraction expression"; exit 1
    fi
    grep -q "expression too deeply nested to type-check (max 1024)" "$tmp/sub.err" || {
        echo "$compiler did not guard the deep subtraction spine"; cat "$tmp/sub.err"; exit 1;
    }
    if "$compiler" "$tmp/deep257.ty" --emit-c -o "$tmp/nested" >"$tmp/nested.err" 2>&1; then
        echo "$compiler accepted 257 nested calls"; exit 1
    fi
    grep -q "expression nesting too deep" "$tmp/nested.err" || {
        echo "$compiler did not give the parser nesting diagnostic"; cat "$tmp/nested.err"; exit 1;
    }
    if "$compiler" "$tmp/unary257.ty" --emit-c -o "$tmp/unary" >"$tmp/unary.err" 2>&1; then
        echo "$compiler accepted a 257-operator unary chain"; exit 1
    fi
    grep -q "expression nesting too deep" "$tmp/unary.err" || {
        echo "$compiler did not guard unary nesting"; cat "$tmp/unary.err"; exit 1;
    }
    if command -v clang >/dev/null 2>&1; then
        exe="$tmp/$(basename "$compiler")-sum256"
        "$compiler" "$tmp/sum256.ty" --cc clang -o "$exe"
        got=$("$exe")
        [ "$got" = 256 ] || { echo "$compiler sum256 printed '$got'"; exit 1; }
        exe="$tmp/$(basename "$compiler")-fstr130"
        "$compiler" "$tmp/fstr130.ty" --cc clang -o "$exe"
        got=$("$exe")
        [ "$got" = 259 ] || { echo "$compiler fstr130 length '$got'"; exit 1; }
    fi
done
echo "expression-scale: 1400-term sums and clang 256-sum/130-hole f-strings passed"
