#!/bin/sh
# bench/transpile/run.sh -- how long tychoc takes to turn one .ty file into C.
#
# WHY THIS EXISTS. docs/guides/perf.md carried "~13 ms" for this and said
# "re-measure locally for a current absolute number", with no harness to do it
# with. A one-off `date +%s%N` around a single run (which is what produced the
# 36-38 ms in that page on 2026-08-14) measures process start, page faults, a
# cold file cache and the output write along with the work, and reports whichever
# of those happened to dominate. That is enough to say "same order", not enough
# to say "regression".
#
# WHAT THIS MEASURES INSTEAD
#   - N runs, the first DISCARDED as a cache warmer.
#   - the MINIMUM reported, not the mean: the fastest run is the one least
#     perturbed by whatever else the machine was doing, and that is the number
#     that reproduces. Median and max are printed beside it so a wide spread is
#     visible rather than hidden.
#   - wall time from bench/peakrss (getrusage + clock around the child), the same
#     instrument the rest of bench/ uses.
#   - output written to a tmpdir, so no repo file is touched and the write cost
#     is at least consistent between runs.
#
# It still includes process start and the C output write -- those are part of
# "how long does tychoc take" for a user. What it removes is the run-to-run
# noise that made a single sample meaningless.
#
#   sh bench/transpile/run.sh [-n RUNS] [-i INPUT] [-c COMPILER]
#
# THE DEFAULT INPUT IS GENERATED -- 600 small functions, ~4.2k lines, using only
# forms that compile across the range being compared. Do not point this at a big
# file just because it is handy: a compile that DIES is faster than one that
# works, so an unchecked input silently reports the failure path as a speed-up.
# That is why the timing loop below refuses to run until the compile succeeds.
set -u
cd "$(dirname "$0")/../.." || exit 2

runs=11
input=""
compiler=./tychoc
while [ $# -gt 0 ]; do
    case "$1" in
        -n) runs=$2; shift 2 ;;
        -i) input=$2; shift 2 ;;
        -c) compiler=$2; shift 2 ;;
        *) echo "usage: $0 [-n RUNS] [-i INPUT] [-c COMPILER]" >&2; exit 2 ;;
    esac
done

[ -x "$compiler" ] || { echo "no compiler at $compiler" >&2; exit 2; }

T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

if [ -z "$input" ]; then
    input="$T/gen.ty"
    awk 'BEGIN {
        print "fn helper0(a: int, b: int) -> int:"
        print "    return a + b"
        for (i = 1; i < 600; i++) {
            printf "fn helper%d(a: int, b: int) -> int:\n", i
            print  "    t := 0"
            print  "    for j := 0; j < 4; j += 1:"
            printf "        t = t + a * %d + b\n", (i % 7) + 1
            print  "    if t > 100:"
            print  "        t = t - 1"
            printf "    return t + helper%d(a, b)\n", i - 1
        }
        print "fn main():"
        print "    s := 0"
        print "    for k := 0; k < 3; k += 1:"
        print "        s = s + helper599(k, 2)"
        print "    println(str(s))"
    }' > "$input"
fi
[ -f "$input" ] || { echo "no input at $input" >&2; exit 2; }

# A run that DIES is not a measurement. Check once, up front, loudly.
if ! "$compiler" --emit-c -o "$T/probe" "$input" >/dev/null 2>"$T/probe.err"; then
    echo "REFUSED: $compiler does not compile $input -- timing it would measure the failure path" >&2
    sed -n "1s/.*error: /  /p" "$T/probe.err" >&2
    exit 1
fi
[ -x bench/peakrss ] || cc -O2 -o bench/peakrss bench/peakrss.c || exit 2

i=0
: > "$T/times"
while [ "$i" -lt "$runs" ]; do
    # peakrss writes its "maxrss ms" line LAST, after the child exits, so take the
    # final stderr line -- the compiler's own warnings land on stderr too and were
    # being parsed as timings (30 samples from 10 runs, and a "|" for the minimum).
    ms=$(bench/peakrss "$compiler" --emit-c -o "$T/out" "$input" 2>&1 >/dev/null | tail -1 | awk '{print $2}')
    # run 0 is the warmer: it pays for the cold file cache and is thrown away
    [ "$i" -gt 0 ] && echo "$ms" >> "$T/times"
    i=$((i + 1))
done

sort -n "$T/times" > "$T/sorted"
n=$(wc -l < "$T/sorted")
min=$(head -1 "$T/sorted")
max=$(tail -1 "$T/sorted")
med=$(awk -v n="$n" 'NR == int((n+1)/2) {print}' "$T/sorted")
lines=$(wc -l < "$input")

printf 'transpile %s (%s lines) with %s: min %s ms, median %s ms, max %s ms over %s timed runs\n' \
    "$input" "$lines" "$compiler" "$min" "$med" "$max" "$n"
