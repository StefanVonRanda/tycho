#!/bin/sh
# Sliding-window eviction — the arena's theoretical weak point. A stream of N
# records keeping only the last W live. malloc/GC free evicted records (peak ~ W);
# the no-individual-free arena holds them (peak ~ N) for HEAP-bearing records.
# Two record kinds: heap (string) shows the boundary; fixed-size (int) shows it's
# bounded. peak RSS via bench/peakrss; string-case checksums must match across langs.
set -u
cd "$(dirname "$0")/../.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
D=bench/window
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
$CC -O2 -o "$T/peakrss" bench/peakrss.c || { echo "peakrss build failed"; exit 2; }
# ru_maxrss is KB on Linux, bytes on macOS/BSD — normalize to KB, then /1024 for MB.
to_kb() { case "$(uname)" in Darwin) echo $(( $1 / 1024 ));; *) echo "$1";; esac; }
ref=""; FAIL=0
meas() {  # <label> <binary> <check?>
    "$T/peakrss" "$2" > "$T/o" 2> "$T/m"
    kb=$(to_kb "$(awk '{print $1}' "$T/m")"); rss=$(awk "BEGIN{printf \"%.1f\", $kb/1024}"); ms=$(awk '{print $2}' "$T/m"); out=$(cat "$T/o")
    printf '  %-14s %7s MB %6s ms   %s\n' "$1" "$rss" "$ms" "$out"
    if [ "$3" = check ]; then
        if [ -z "$ref" ]; then ref="$out"; elif [ "$out" != "$ref" ]; then echo "    ^ CHECKSUM MISMATCH"; FAIL=1; fi
    fi
}
echo "sliding window of HEAP records (string) — 2M stream, 50k window:"
$TYCHOC "$D/window_naive.ty" --emit-c -o "$T/wn" >/dev/null 2>&1 && $CC -O3 -o "$T/wn" "$T/wn.c" -lm; meas "tycho (string)" "$T/wn" check
$CC -O3 -o "$T/wc" "$D/window.c"; meas "C (string)" "$T/wc" check
if command -v go >/dev/null 2>&1; then cp "$D/window.go" "$T/wg.go" && ( cd "$T" && go build -o wgo wg.go 2>/dev/null ); meas "Go (string)" "$T/wgo" check; fi
echo "same window with FIXED-SIZE records (int) — slots reused, no per-element heap:"
$TYCHOC "$D/window_int.ty" --emit-c -o "$T/wi" >/dev/null 2>&1 && $CC -O3 -o "$T/wi" "$T/wi.c" -lm; meas "tycho (int)" "$T/wi" nocheck
[ "$FAIL" = 0 ] && echo "window: ok (string-case checksums agree)" || { echo "window: FAIL"; exit 1; }
