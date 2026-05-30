#!/bin/sh
# Performance guard — guards the thesis's perf claims the way tests/run.sh
# guards correctness. Each bench program is built native -O2 and run under
# bench/peakrss (peak RSS + wall time). A bench asserts ONE metric against a
# bound chosen with a huge margin, so it catches a regression (a broken
# optimization) without being flaky:
#
#   append        peak RSS  — in-place string append stays linear (the naive
#                             baseline is ~828 MB at this N; bound 32 MB).
#   loop_scratch  peak RSS  — loop scratch arena resets, so memory is constant
#                             over 5M iterations (bound 32 MB).
#   map_accum     peak RSS  — map accumulator reuses its table (pure deep-copy
#                             per insert would be O(n^2); bound 64 MB).
#   memo          wall time — inout memo keeps fib O(n); an exponential
#                             regression blows the bound (instant vs seconds;
#                             bound 1000 ms).
#
# Each program's output is also checked (a bench must compute the right answer).
# Exit status: 0 iff every bench passes. Bounds are intentionally generous;
# this catches order-of-magnitude regressions, not small fluctuations.
set -u
cd "$(dirname "$0")/.." || exit 2

HIERC=./hierc
[ -x "$HIERC" ] || { echo "no ./hierc — run 'make' first"; exit 2; }
CC="${CC:-cc}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
$CC -O2 -o "$TMP/peakrss" bench/peakrss.c || { echo "could not build bench/peakrss.c"; exit 2; }

# ru_maxrss is KB on Linux, bytes on macOS/BSD — normalize to KB.
to_kb() { case "$(uname)" in Darwin) echo $(( $1 / 1024 ));; *) echo "$1";; esac; }

fail=0
printf '%-14s %10s %8s   %s\n' "bench" "peakRSS" "time" "result"

# run_bench <name> <expected-output> <metric: rss|time> <limit> <unit-label>
run_bench() {
    name="$1"; exp="$2"; metric="$3"; limit="$4"; ulabel="$5"
    if ! "$HIERC" "bench/$name.hi" --emit-c -o "$TMP/$name" >"$TMP/$name.log" 2>&1; then
        printf '%-14s %10s %8s   FAIL (transpile)\n' "$name" "-" "-"; fail=1; return
    fi
    if ! $CC -O2 -std=c11 -o "$TMP/$name" "$TMP/$name.c" 2>"$TMP/$name.log"; then
        printf '%-14s %10s %8s   FAIL (cc)\n' "$name" "-" "-"; fail=1; return
    fi
    out="$("$TMP/peakrss" "$TMP/$name" 2>"$TMP/$name.m")"; rc=$?
    read rss ms < "$TMP/$name.m"
    kb="$(to_kb "$rss")"
    rssmb=$(( kb / 1024 ))

    if [ "$rc" -ne 0 ]; then
        printf '%-14s %8sMB %6sms   FAIL (exit)\n' "$name" "$rssmb" "$ms"; fail=1; return
    fi
    if [ "$out" != "$exp" ]; then
        printf '%-14s %8sMB %6sms   FAIL (output %s != %s)\n' "$name" "$rssmb" "$ms" "$out" "$exp"; fail=1; return
    fi
    if [ "$metric" = rss ]; then val="$kb"; else val="$ms"; fi
    if [ "$val" -le "$limit" ]; then verdict="ok  ($metric $val <= $limit $ulabel)"
    else verdict="FAIL ($metric $val > $limit $ulabel)"; fail=1; fi
    printf '%-14s %8sMB %6sms   %s\n' "$name" "$rssmb" "$ms" "$verdict"
}

run_bench append       40000       rss  32768 KB
run_bench loop_scratch 8           rss  32768 KB
run_bench map_accum    40000       rss  65536 KB
run_bench memo         1134903170  time 1000  ms

echo "-----------------------------------------------------------"
[ "$fail" -eq 0 ] && { echo "all benchmarks within bounds"; exit 0; }
echo "BENCHMARK REGRESSION — a perf bound was exceeded"; exit 1
