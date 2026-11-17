set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "no $TYCHOC -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
export TYCHO_CORELIB="$PWD/corelib"
D=bench/flat-state
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
$CC -O2 -o "$T/peakrss" bench/peakrss.c || { echo "peakrss build failed"; exit 2; }
to_kb() { case "$(uname)" in Darwin) echo $(( $1 / 1024 ));; *) echo "$1";; esac; }
RUNS="${RUNS:-9}"
FAIL=0

# kv LINE KEY [DEFAULT] -- the KEY=VALUE field of one output line
kv() { printf '%s\n' "$1" | tr ' ' '\n' | awk -F= -v k="$2" -v d="${3-}" '
    $1 == k { print $2; found = 1; exit }
    END { if (!found && d != "") print d }'; }
# ms NS -- nanoseconds to milliseconds, two decimals
ms() { awk "BEGIN { printf \"%.2f\", $1 / 1000000 }"; }
med() { sort -n | awk 'NF { a[NR] = $1 } END {
    if (NR == 0) { print "n/a"; exit }
    if (NR % 2) { print a[(NR + 1) / 2] } else { printf "%.1f\n", (a[NR / 2] + a[NR / 2 + 1]) / 2 }
}'; }
# col FILE LABEL MODE N -- one column of the matching runs
col() { awk -v L="$2" -v M="$3" -v C="$4" '$1 == L && $2 == M { print $C }' "$1"; }

printf '%-6s %-5s %8s %7s %8s %8s %8s %9s %7s %s\n' \
    layout mode 'peakRSS' 'wall' phase query save bytes teardown 'w/r'

# measure LABEL MODE BIN FILE -- one timed run, appends to $T/runs
measure() {
    if ! "$T/peakrss" "$3" "$2" "$4" > "$T/out" 2> "$T/met"; then
        echo "flat-state: RUN FAILED ($1 $2)"; cat "$T/out" "$T/met"; exit 2
    fi
    res="$(sed -n 's/^result //p' "$T/out")"
    line="$(grep -E '^(build|load) ' "$T/out")"
    if [ -z "$res" ] || [ -z "$line" ]; then
        echo "flat-state: BAD OUTPUT ($1 $2)"; cat "$T/out"; exit 2
    fi
    if [ -z "$ref" ]; then ref="$res"; elif [ "$res" != "$ref" ]; then
        echo "flat-state: RESULT MISMATCH"; echo "  got      $res"; echo "  expected $ref"; FAIL=1
    fi
    if [ "$2" = build ]; then
        sum="$(kv "$res" checksum)"; after="$(kv "$line" after_save)"
        [ "$after" = "$sum" ] || { echo "flat-state: after_save != checksum ($1)"; FAIL=1; }
        [ "$(kv "$line" stale)" = 1 ] || { echo "flat-state: stale != 1 ($1)"; FAIL=1; }
    fi
    rss="$(to_kb "$(awk '{ print $1 }' "$T/met")")"
    wall="$(awk '{ print $2 }' "$T/met")"
    phase="$(kv "$line" ns)"
    printf '%s %s %s %s %s %s %s\n' "$1" "$2" "$rss" "$wall" "$phase" \
        "$(kv "$line" query)" "$(kv "$line" save 0)" >> "$T/runs"
}

if ! "$TYCHOC" "$D/nested/main.ty" -o "$T/nested" > "$T/err" 2>&1; then
    echo "flat-state: NESTED BUILD FAILED"; cat "$T/err"; exit 2
fi
if ! "$TYCHOC" "$D/flat/main.ty" -o "$T/flat" > "$T/err" 2>&1; then
    echo "flat-state: FLAT BUILD FAILED"; cat "$T/err"; exit 2
fi

ref=""
i=0
while [ "$i" -lt "$RUNS" ]; do
    if [ $((i % 2)) -eq 0 ]; then
        measure flat build "$T/flat" "$T/flat.bin"
        measure flat load "$T/flat" "$T/flat.bin"
        measure nested build "$T/nested" "$T/nested.bin"
        measure nested load "$T/nested" "$T/nested.bin"
    else
        measure nested build "$T/nested" "$T/nested.bin"
        measure nested load "$T/nested" "$T/nested.bin"
        measure flat build "$T/flat" "$T/flat.bin"
        measure flat load "$T/flat" "$T/flat.bin"
    fi
    i=$((i + 1))
done

# TYCHO_ARENA_STATS=full, one build and one load per layout
teardown() {                              # LABEL MODE BIN FILE -> ms
    TYCHO_ARENA_STATS=full "$3" "$2" "$4" > /dev/null 2> "$T/arena" || exit 2
    awk '/ms in arena teardown/ { print $1 }' "$T/arena"
}
arena_field() {                           # FIELD MODE BIN FILE
    TYCHO_ARENA_STATS=full "$3" "$2" "$4" > /dev/null 2> "$T/arena" || exit 2
    grep "$1" "$T/arena" | head -1 | sed 's/^ *//'
}

# strace: one build or load per layout, count syscalls naming that file
syscalls() {                              # BIN MODE FILE -> "w/r"
    if ! command -v strace > /dev/null 2>&1; then echo "n/a"; return; fi
    strace -y -e trace=read,write -o "$T/trace" "$1" "$2" "$3" > /dev/null 2>&1 || true
    w="$(grep -F "$3" "$T/trace" 2>/dev/null | grep -c '^write(' || true)"
    r="$(grep -F "$3" "$T/trace" 2>/dev/null | grep -c '^read(' || true)"
    echo "$w/$r"
}

row() {                                   # LABEL MODE
    rss="$(col "$T/runs" "$1" "$2" 3 | med)"
    wall="$(col "$T/runs" "$1" "$2" 4 | med)"
    phase="$(col "$T/runs" "$1" "$2" 5 | med)"
    query="$(col "$T/runs" "$1" "$2" 6 | med)"
    save="$(col "$T/runs" "$1" "$2" 7 | med)"
    if [ "$1" = flat ]; then bin="$T/flat"; file="$T/flat.bin"; else bin="$T/nested"; file="$T/nested.bin"; fi
    td="$(teardown "$1" "$2" "$bin" "$file")"
    io="$(syscalls "$bin" "$2" "$file")"
    if [ "$2" = load ]; then save_s="      -"; else save_s="$(ms "$save")"; fi
    printf '%-6s %-5s %7sM %6sms %7sms %7sms %7sms %8sB %6sms %s\n' \
        "$1" "$2" "$(awk "BEGIN { printf \"%.1f\", $rss / 1024 }")" "$wall" \
        "$(ms "$phase")" "$(ms "$query")" "$save_s" "$(wc -c < "$file" | tr -d ' ')" \
        "$td" "$io"
}

row flat build
row flat load
row nested build
row nested load

spread() { col "$T/runs" "$1" "$2" "$3" | sort -n | awk 'NR == 1 { lo = $1 } { hi = $1 } END { printf "%s..%s", lo, hi }'; }
rss_spread() { col "$T/runs" "$1" "$2" 3 | sort -n | awk 'NR == 1 { lo = $1 } { hi = $1 } END { printf "%.1f..%.1f MiB", lo / 1024, hi / 1024 }'; }
echo
echo "checksums: $ref"
echo "peakRSS (min..max): flat build=$(rss_spread flat build) load=$(rss_spread flat load) | nested build=$(rss_spread nested build) load=$(rss_spread nested load)"
echo "spread (min..max, ns):"
echo "  flat   build=$(spread flat build 5) query=$(spread flat build 6) save=$(spread flat build 7) load=$(spread flat load 5)"
echo "  nested build=$(spread nested build 5) query=$(spread nested build 6) save=$(spread nested build 7) load=$(spread nested load 5)"
echo "arena (build):"
echo "  flat:   $(arena_field 'peak live' build "$T/flat" "$T/flat.bin")"
echo "  nested: $(arena_field 'peak live' build "$T/nested" "$T/nested.bin")"

[ "$FAIL" = 0 ] && echo "flat-state: ok (checksums agree)" || { echo "flat-state: FAIL"; exit 1; }
