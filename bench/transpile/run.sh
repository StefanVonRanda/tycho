set -u
cd "$(dirname "$0")/../.." || exit 2

runs=11
input=""
compiler="${TYCHOC:-./tychoc1}"
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
