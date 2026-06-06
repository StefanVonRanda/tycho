#!/bin/sh
# Large held set (2M small strings) + steady churn. Two findings:
#  (#2 per-object overhead) hier holds the set most compactly — arena bump-alloc
#     has no per-object malloc header (C) or GC metadata (Go).
#  (#1 GC scan)  a tracing GC must re-scan the live set each collection. At default
#     GOGC, Go's pacer keeps that cheap (GC cost ~ allocation traffic, NOT live size
#     — the naive "big heap kills GC" is false for modern Go). Under memory pressure
#     (GOGC=10) Go can match hier's footprint, but only by GC'ing ~10x more often,
#     re-scanning the whole live set each time. hier/C never scan. Same checksum.
set -u
cd "$(dirname "$0")/../.." || exit 2
HIERC=./hierc; [ -x "$HIERC" ] || { echo "no ./hierc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"; D=bench/gcscan
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
$CC -O2 -o "$T/peakrss" bench/peakrss.c || { echo "peakrss build failed"; exit 2; }
ref=""; FAIL=0
row() { # <label> <binary> <env>
    env $3 "$T/peakrss" "$2" > "$T/o" 2> "$T/m"
    rl=$(grep -vE '^gc=' "$T/m" | tail -1); gl=$(grep -E '^gc=' "$T/m")
    rss=$(echo "$rl" | awk '{printf "%.1f",$1/1024}'); ms=$(echo "$rl" | awk '{print $2}'); out=$(cat "$T/o")
    gc="no GC"; [ -n "$gl" ] && gc=$(echo "$gl" | sed 's/gc=\([0-9]*\) pause_us=\([0-9]*\)/\1 cycles, \2us pause/')
    printf '  %-16s %6s MB %6s ms   %s\n' "$1" "$rss" "$ms" "$gc"
    if [ -z "$ref" ]; then ref="$out"; elif [ "$out" != "$ref" ]; then echo "    ^ CHECKSUM MISMATCH ($out vs $ref)"; FAIL=1; fi
}
$HIERC "$D/gcscan.hi" --emit-c -o "$T/gh" >/dev/null 2>&1 && $CC -O2 -o "$T/gh" "$T/gh.c" -lm; row "hier" "$T/gh" ""
$CC -O2 -o "$T/gc" "$D/gcscan.c"; row "C" "$T/gc" ""
if command -v go >/dev/null 2>&1; then
    cp "$D/gcscan.go" "$T/gg.go" && ( cd "$T" && go build -o ggo gg.go 2>/dev/null )
    row "Go (GOGC=100)" "$T/ggo" ""
    row "Go (GOGC=10)"  "$T/ggo" "GOGC=10"
fi
[ "$FAIL" = 0 ] && echo "gcscan: ok (checksums agree)" || { echo "gcscan: FAIL"; exit 1; }
