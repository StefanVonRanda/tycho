#!/bin/sh
# Wall-time regression guard for `make ci`. CHECKSUM tests can't catch a perf
# regression: commit 6ff7aa1 (MM-9) silently 5x'd tree-alloc wall time while every
# golden/fuzz/fixpoint check stayed green, because it bloated the per-call scope
# Arena. This asserts the thesis claim directly -- on allocation-heavy tree
# workloads tycho's bump-allocate + bulk-free must comfortably BEAT hand-written
# malloc/free C. The check is RELATIVE (tycho_ms vs C_ms on the same machine), so
# it's portable: healthy tycho is ~0.23x C, the regression was ~0.93x C, and the
# 0.60x gate cleanly separates them with margin for noise.
set -eu
cd "$(dirname "$0")/.."
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
[ "$rc" -eq 0 ] && echo "bench-guard: ok (tycho beats C on tree workloads)" || echo "bench-guard: FAILED"
exit "$rc"
