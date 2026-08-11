#!/bin/sh
# Wall-time regression guard for `make ci`. CHECKSUM tests can't catch a perf
# regression: commit 4a5c64c (MM-9) silently 5x'd tree-alloc wall time while every
# golden/fuzz/fixpoint check stayed green, because it bloated the per-call scope
# Arena. This asserts the thesis claim directly -- on allocation-heavy tree
# workloads tycho's bump-allocate + bulk-free must comfortably BEAT hand-written
# malloc/free C. The check is RELATIVE (tycho_ms vs C_ms on the same machine), so
# it's portable: healthy tycho is ~0.23x C, the regression was ~0.93x C, and the
# 0.60x gate cleanly separates them with margin for noise.
set -eu
cd "$(dirname "$0")/.."
# bench/peakrss.c, which every bench lane times through, is built on fork(2) +
# wait4(2) + getrusage(2) and <sys/resource.h>, none of which mingw has: the
# build stops at "fatal error: sys/resource.h: No such file or directory"
# (measured 2026-08-08). A Windows peakrss wants CreateProcess +
# GetProcessMemoryInfo + WaitForSingleObject -- worth writing if Windows perf
# numbers are ever wanted, but this lane is a wall-clock RATIO gate (tycho must
# come in under 0.60x C) and a ratio measured inside a VM is noise, not a
# regression signal. Correctness is what the port claims; this asserts speed.
case "$(uname -s)" in *MSYS*|*MINGW*|*CYGWIN*)
    echo "bench-guard: SKIP (Windows: bench/peakrss.c is fork/wait4/getrusage-based, and a wall-clock ratio gate does not survive a VM anyway)"
    exit 0 ;;
esac
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cc -O2 -o "$T/pk" bench/peakrss.c
GATE_NUM=60   # tycho must be < GATE_NUM/100 of C's wall (i.e. < 0.60x C)

best() { # binary -> best-of-3 wall ms
  bms=9999999
  for _ in 1 2 3; do
    ms=$("$T/pk" "$1" 2>&1 | tail -1 | awk '{print $NF}')
    case "$ms" in *[!0-9]*|"") ms=9999999 ;; esac
    [ "$ms" -lt "$bms" ] && bms=$ms
  done
  echo "$bms"
}

rc=0
for w in binary_trees maptree; do
  ./tychoc "bench/prongB/$w.ty" --emit-c -o "$T/h" >/dev/null 2>&1
  cc -O3 -o "$T/h" "$T/h.c" -lm
  cc -O3 -o "$T/c" "bench/prongB/$w.c" -lm
  h=$(best "$T/h"); c=$(best "$T/c")
  # pass if 100*h < GATE_NUM*c  (h/c < 0.60)
  if [ $((100 * h)) -lt $((GATE_NUM * c)) ]; then
    printf '  ok    %-14s tycho=%sms  C=%sms  (%d%% of C, gate <%d%%)\n' "$w" "$h" "$c" $((100 * h / c)) "$GATE_NUM"
  else
    printf '  FAIL  %-14s tycho=%sms  C=%sms  (%d%% of C, gate <%d%%) -- tree-alloc perf regressed\n' "$w" "$h" "$c" $((100 * h / c)) "$GATE_NUM"
    rc=1
  fi
done

# --- bounds-check elision -------------------------------------------------
# The workload above cannot see this class at all: neither binary_trees nor maptree
# contains a loop of the ELIDABLE shape (`for i := 0; i < len(A); i += 1:` over a body
# that neither rebinds A nor passes it whole to a call), which is how phase 6 of
# the loops-cleanup plan turned elision off for 223 sites tree-wide
# with every gate staying green. bench/prongB/arr_pipeline.ty does contain it -- its
# two scan loops, bench/prongB/arr_pipeline.ty:16 and :20.
#
# The assertion is STRUCTURAL, on the emitted C, and that is a MEASURED
# decision rather than a preference. Wall time cannot see this class at -O3 --
# the level this script builds with, and the level `tychoc` itself hands to cc
# (src/tychoc.c:13081) -- because the three-clause form emits its bound into the
# C `while` header, so gcc already knows `i < xs.len` and folds the accessor's
# own `i >= xs.len` test away. Best-of-3 on arr_pipeline, the same program built
# with and without TYCHOC_NO_BOUNDS_ELISION=1, measured while writing this:
#     -O3   29 vs 30 ms,  46 vs 46 ms,  47 vs 45 ms   (noise; C_ms moved 24->35
#                                                      across the same three runs)
#     -O2   2358 vs 2684 ms  (1.14x)
#     -O1   2517 vs 4740 ms  (1.88x)
# A ratio gate at -O3 would therefore be theatre; the emitted C is what actually
# reddens. Counted, not eyeballed: elision on -> 2 raw `.data[h_i]` and 0
# `tycho_arr_int_get(h_`; elision off -> 0 and 2.
./tychoc bench/prongB/arr_pipeline.ty --emit-c -o "$T/ap" >/dev/null 2>&1
el=$(grep -c '\.data\[h_i\]' "$T/ap.c" || true)
ck=$(grep -c 'tycho_arr_int_get(h_' "$T/ap.c" || true)
if [ "$el" -ge 2 ] && [ "$ck" -eq 0 ]; then
  printf '  ok    %-14s %s raw .data[i], %s checked calls (bounds-check elision live)\n' "arr_pipeline" "$el" "$ck"
else
  printf '  FAIL  %-14s %s raw .data[i], %s checked calls -- bounds-check elision no longer reaches `for i := 0; i < len(A); i += 1:`\n' "arr_pipeline" "$el" "$ck"
  rc=1
fi

[ "$rc" -eq 0 ] && echo "bench-guard: ok (tycho beats C on tree workloads; elision live on array scans)" || echo "bench-guard: FAILED"
exit "$rc"
