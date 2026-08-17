set -u
cd "$(dirname "$0")/.." || exit 2

TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc — run 'make' first"; exit 2; }
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
    if ! "$TYCHOC" "bench/$name.ty" --emit-c -o "$TMP/$name" >"$TMP/$name.log" 2>&1; then
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

run_bench append       40000          rss  32768  KB
run_bench strarr_build 3              rss  32768  KB
run_bench nestarr_build 11            rss  32768  KB
run_bench structarr_build 3           rss  32768  KB
run_bench optarr_build  3             rss  32768  KB
run_bench optstr_build  3             rss  32768  KB
run_bench inout_fill   200            rss  32768  KB
run_bench instruct_fill 200           rss  32768  KB
run_bench loop_scratch 8              rss  32768  KB
run_bench map_accum    40000          rss  65536  KB
run_bench memo         1134903170     time 1000   ms
run_bench move         31999996000000 rss  163840 KB
run_bench ctor_move    31999996000000 rss  163840 KB
run_bench comb_build   4001           rss  65536  KB
run_bench transient    13106800       rss  32768  KB
run_bench heap_transient t32767        rss  32768  KB
run_bench treewalk     3603000        time 1000   ms

echo "-----------------------------------------------------------"
[ "$fail" -eq 0 ] && { echo "all benchmarks within bounds"; exit 0; }
echo "BENCHMARK REGRESSION — a perf bound was exceeded"; exit 1
