#!/bin/sh
# Emitted-C parity: how many fixtures does ./tychoc1 compile to BYTE-IDENTICAL
# C against ./tychoc? src/tychoc.c is the golden reference; tychoc1 moves to it.
# Prints "N/M identical" and the per-class census of what still differs, so a
# change that fixes a class can be seen doing it. Exit 1 only if the count DROPS
# below scripts/emit_parity.count, which is the ratchet.
set -u
cd "$(dirname "$0")/.." || exit 2
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
[ -x ./tychoc ] || { echo "no ./tychoc -- run 'make'"; exit 2; }
[ -x ./tychoc1 ] || { echo "no ./tychoc1 -- run 'make tychoc1'"; exit 2; }
same=0; total=0
: > "$T/differ"
for f in tests/*.ty; do
    ./tychoc  --emit-c "$f" -o "$T/a" >/dev/null 2>&1 || continue
    ./tychoc1 --emit-c "$f" -o "$T/b" >/dev/null 2>&1 || continue
    [ -f "$T/a.c" ] && [ -f "$T/b.c" ] || continue
    total=$((total + 1))
    if cmp -s "$T/a.c" "$T/b.c"; then same=$((same + 1)); else echo "$f" >> "$T/differ"; fi
    rm -f "$T/a.c" "$T/b.c"
done
echo "emit parity: $same/$total fixtures byte-identical"
base=0
[ -f scripts/emit_parity.count ] && base=$(cat scripts/emit_parity.count)
if [ "$same" -lt "$base" ]; then
    echo "REGRESSION: was $base, now $same"; exit 1
fi
[ "$same" -gt "$base" ] && echo "improved: was $base (RECORD=1 to ratchet)"
[ "${RECORD:-0}" = "1" ] && { echo "$same" > scripts/emit_parity.count; echo "recorded $same"; }
exit 0
