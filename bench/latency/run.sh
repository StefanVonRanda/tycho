#!/bin/sh
# Latency / GC-pause predictability. A steadily-churning loop (2000 rounds x 100k
# working set, discarded each round). hier reclaims with one O(1) arena reset per
# round, C frees — both with NO GC and zero stop-the-world pause. Go's GC must
# collect the garbage; it self-reports its collection count + total pause time.
# The point: hier gets C's pause-free predictability WITHOUT manual frees. Same
# checksum across langs. peak RSS + wall via bench/peakrss.
set -u
cd "$(dirname "$0")/../.." || exit 2
HIERC=./hierc
[ -x "$HIERC" ] || { echo "no ./hierc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
D=bench/latency
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
$CC -O2 -o "$T/peakrss" bench/peakrss.c || { echo "peakrss build failed"; exit 2; }
ref=""; FAIL=0
meas() {  # <label> <binary> <gc?>
    "$T/peakrss" "$2" > "$T/o" 2> "$T/m"
    rssline=$(grep -vE '^gc=' "$T/m" | tail -1)
    rss=$(echo "$rssline" | awk '{printf "%.1f", $1/1024}'); ms=$(echo "$rssline" | awk '{print $2}')
    out=$(cat "$T/o"); gc="(none — no GC)"
    [ "$3" = gc ] && gc=$(grep -E '^gc=' "$T/m" | sed 's/gc=\([0-9]*\) pause_us=\([0-9]*\)/\1 collections, \2 us pause/')
    printf '  %-5s %6s MB %6s ms   GC: %-32s out=%s\n' "$1" "$rss" "$ms" "$gc" "$out"
    if [ -z "$ref" ]; then ref="$out"; elif [ "$out" != "$ref" ]; then echo "    ^ CHECKSUM MISMATCH"; FAIL=1; fi
}
$HIERC "$D/latency.hi" --emit-c -o "$T/lh" >/dev/null 2>&1 && $CC -O3 -o "$T/lh" "$T/lh.c" -lm; meas hier "$T/lh" nogc
$CC -O3 -o "$T/lc" "$D/latency.c"; meas C "$T/lc" nogc
if command -v go >/dev/null 2>&1; then cp "$D/latency.go" "$T/lg.go" && ( cd "$T" && go build -o lgo lg.go 2>/dev/null ); meas Go "$T/lgo" gc; fi
[ "$FAIL" = 0 ] && echo "latency: ok (checksums agree; hier/C have zero GC pause)" || { echo "latency: FAIL"; exit 1; }
