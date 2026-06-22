#!/bin/sh
# Performance guard — guards the thesis's perf claims the way tests/run.sh
# guards correctness. Each bench program is built native -O2 and run under
# bench/peakrss (peak RSS + wall time). A bench asserts ONE metric against a
# bound chosen with a huge margin, so it catches a regression (a broken
# optimization) without being flaky:
#
#   append        peak RSS  — in-place string append stays linear (the naive
#                             baseline is ~828 MB at this N; bound 32 MB).
#   strarr_build  peak RSS  — a loop-local [str] built by push frees its string
#                             elements per iteration (elements live in the block
#                             arena). Before the element-residual fix the
#                             substrings were malloc'd and leaked (~368 MB at
#                             this N under tychoc0); bound 32 MB.
#   nestarr_build peak RSS  — a loop-local [[int]] built by push frees its inner
#                             buffers per iteration (nested-array elements live in
#                             the block arena, deep-copied on copy). Before the
#                             nested-element fix the inner [int] buffers were
#                             malloc'd and leaked (~108 MB at this N under tychoc0);
#                             bound 32 MB.
#   structarr_buildpeak RSS  — a loop-local [Item] (Item has a string field) built
#                             by push frees its struct elements + their string
#                             fields per iteration. Before the struct-element fix,
#                             struct construction built string fields with owner 0
#                             (malloc/immortal), leaking every iteration (~184 MB
#                             at this N under tychoc0); bound 32 MB.
#   optarr_build  peak RSS  — a loop-local [Option(int)] built by push frees its
#                             option boxes per iteration (boxes re-homed into the
#                             array's block arena). Before the option-element fix
#                             the boxes were malloc'd (owner 0) and leaked (~62 MB
#                             at this N under tychoc0); bound 32 MB.
#   inout_fill    peak RSS  — a callee pushes into an `mut [int]` whose array
#                             lives in the caller's per-iteration block arena; with
#                             the home arena threaded (the _ina_ param), the grown
#                             buffer frees each iteration. Before the threading,
#                             owner_arena_of(mut)=0 grew it via malloc and leaked
#                             (~798 MB at this N under tychoc0); bound 32 MB.
#   loop_scratch  peak RSS  — loop scratch arena resets, so memory is constant
#                             over 5M iterations (bound 32 MB).
#   map_accum     peak RSS  — map accumulator reuses its table (pure deep-copy
#                             per insert would be O(n^2); bound 64 MB).
#   memo          wall time — mut memo keeps fib O(n); an exponential
#                             regression blows the bound (instant vs seconds;
#                             bound 1000 ms).
#   transient     peak RSS  — a heap value folded into a scalar accumulator
#                             (`sum = sum + check(make(14))`) is reclaimed each
#                             iteration in the loop scratch, not retained in the
#                             accumulator's outer arena (measured ~1 MB vs
#                             ~201 MB), bound 32 MB.
#   heap_transient peak RSS — even when the accumulator is HEAP (a string), a
#                             big transient that is only a call ARGUMENT (the
#                             tree in `describe(check(make(14)))`) is reclaimed
#                             in the loop scratch, not retained in the
#                             accumulator's arena (~2 MB vs ~201 MB), bound 32 MB.
#   comb_build    peak RSS  — the loop-carried self-rebuild move keeps building
#                             a comb (`t = Pair(t, Leaf)`) O(n) memory; reverting
#                             to the per-step deep-copy makes it O(n^2) (measured
#                             ~2 MB vs ~368 MB at n=4000), bound 64 MB.
#   treewalk      wall time — match-arm payload borrow keeps a match->recurse
#                             tree pass O(n)/traversal; reverting to per-binding
#                             deep-copy makes a comb O(n^2) (measured ~57 ms
#                             borrowed vs ~89 s copied), so the 1000 ms bound
#                             catches it.
#   move          peak RSS  — move-on-last-use elides the deep copy of a dead
#                             local (`dup := big`), so only one ~64 MB buffer is
#                             live; reverting to a copy makes it ~187 MB
#                             (measured), so the 160 MB bound catches it.
#   ctor_move     peak RSS  — construction-arg move elides the deep copy of a
#                             dead local placed INTO an aggregate (`t := (big,
#                             0)`); one buffer (~126 MB) vs two when copied
#                             (~187 MB measured). Guards the constructor path
#                             the `move` bench (a decl) does not.
#
# Each program's output is also checked (a bench must compute the right answer).
# Exit status: 0 iff every bench passes. Bounds are intentionally generous;
# this catches order-of-magnitude regressions, not small fluctuations.
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
