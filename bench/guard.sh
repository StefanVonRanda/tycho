set -eu
cd "$(dirname "$0")/.."
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
