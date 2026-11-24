#!/bin/sh
# The typed IR (compiler/tir, `tychoc --emit-tir`):
#   - tests/tir/NAME/main.ty with NAME.tir: the dump must match;
#   - with a `# expect: TEXT` first line: lowering must refuse with TEXT;
#   - every accepting program in tests/*.ty lowers or is refused BY NAME, and
#     re-typing never raises an error -- the IR and the checker agree.
set -u
cd "$(dirname "$0")/.."
bad=0
for d in tests/tir/*/; do
    n=$(basename "$d")
    f="$d/main.ty"
    exp=$(sed -n '1s/^# expect: //p' "$f")
    if [ -n "$exp" ]; then
        if ./tychoc --emit-tir "$f" > /dev/null 2> tests/tir/.err; then
            echo "FAIL tir $n: lowered, expected a refusal"; bad=1
        elif ! grep -qF "$exp" tests/tir/.err; then
            echo "FAIL tir $n: expected '$exp', got:"; cat tests/tir/.err; bad=1
        fi
    else
        ./tychoc --emit-tir "$f" > tests/tir/.out 2>&1
        if ! cmp -s tests/tir/.out "tests/tir/$n.tir"; then
            echo "FAIL tir $n: dump differs"; diff "tests/tir/$n.tir" tests/tir/.out | head -20; bad=1
        fi
    fi
done
lowered=0
total=0
for f in tests/*.ty; do
    [ -f "${f%.ty}.out" ] || continue
    total=$((total + 1))
    if ./tychoc --emit-tir "$f" > /dev/null 2> tests/tir/.err; then
        lowered=$((lowered + 1))
    elif grep -q "tir: internal" tests/tir/.err; then
        echo "FAIL tir sweep $f:"; grep "tir: internal" tests/tir/.err | head -3; bad=1
    elif ! grep -q "not in the script subset" tests/tir/.err; then
        echo "FAIL tir sweep $f: refused without naming why:"; head -3 tests/tir/.err; bad=1
    fi
done
rm -f tests/tir/.err tests/tir/.out
[ "$bad" -eq 0 ] || exit 1
echo "tir: goldens and refusals pass; corpus: $lowered of $total programs lower, the rest are refused by name, none re-types differently"
