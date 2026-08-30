#!/bin/sh
# Score tychoc1 on the lanes that until now scored only ./tychoc.
#
# Every runner in this tree spelled its compiler `TYCHOC=./tychoc`, hardcoded,
# so the self-hosted compiler was covered by parse-check alone -- lex, parse and
# an AST census, with typecheck and emit ungated. This substitutes tychoc1 into
# the real runners rather than reimplementing their judgements.
#
# entrypoints gets its OWN warning baseline. tychoc1 misses three copy-is-live
# warnings ./tychoc emits -- exec.ty:107, exec.ty:119 and tycho-tally/main.ty:72.
# All three turn on the LIVENESS MODEL, not on a missing case: zzchan.ty scans
# for a later textual read of the name, while src/tychoc.c:10075 fires wherever
# it decided to copy, so `hostile` warns there despite never being read again.
# The baseline records what tychoc1 does today so a regression reddens; it is
# not an endorsement. Closing the gap deletes the second baseline.
#
# Runners are invoked as `sh <dir>/run.sh`, never through make: `make conc` and
# friends depend on the `tychoc` target and rebuild it, silently replacing a
# substituted binary mid-lane.
set -u
cd "$(dirname "$0")/.." || exit 2
ONLY="${1:-all}"
start=$(date +%s)

make tychoc1 >/dev/null 2>&1 || { echo "tychoc1-check: tychoc1 does not build"; exit 1; }
[ -x ./tychoc1 ] || { echo "tychoc1-check: no ./tychoc1"; exit 2; }
C="${TYCHOC1:-./tychoc1}"

fails=0; n=0
lane() {
    label=$1; shift
    case "$ONLY" in all) ;; "$label") ;; *) return ;; esac
    n=$((n + 1))
    if TYCHOC="$C" "$@" >"$T/$label.log" 2>&1; then
        printf '  %-22s ok\n' "$label"
    else
        printf '  %-22s FAIL\n' "$label"
        sed 's/^/      /' "$T/$label.log" | tail -6
        fails=$((fails + 1))
    fi
}

T=$(mktemp -d) || exit 2
trap 'rm -rf "$T"' EXIT

echo "tychoc1-check: $($C --version 2>/dev/null || echo tychoc1)"

# [1] The fixture corpus, through its own runner -- the same 755 fixtures, the
# same goldens, the same ASan leg. The pass count is asserted below against a
# literal, because "passed: 0 failed: 0" is also a zero-failure run.
if [ "$ONLY" = all ] || [ "$ONLY" = tests ]; then
    n=$((n + 1))
    TYCHOC="$C" sh tests/run.sh >"$T/tests.log" 2>&1
    got=$(sed -n 's/^passed: *\([0-9]*\).*/\1/p' "$T/tests.log" | tail -1)
    bad=$(sed -n 's/^passed: *[0-9]* *failed: *\([0-9]*\).*/\1/p' "$T/tests.log" | tail -1)
    if [ "${bad:-1}" = 0 ] && [ "${got:-0}" -ge 755 ]; then
        printf '  %-22s ok (passed=%s failed=%s)\n' tests "$got" "$bad"
    else
        printf '  %-22s FAIL (passed=%s failed=%s, expected failed=0 passed>=755)\n' \
            tests "${got:-?}" "${bad:-?}"
        grep '^failed:' "$T/tests.log" | tail -1 | cut -c1-400
        fails=$((fails + 1))
    fi
fi

lane conc        sh tests/conc/run.sh
lane ffi         sh tests/ffi/run.sh
lane recursion   sh tests/recursion/run.sh
WARNBASE=scripts/entrypoints.tychoc1.warn
export WARNBASE
lane entrypoints sh scripts/entrypoints.sh
lane corelib     sh corelib/run.sh
lane corelib-ex  sh examples/corelib/run.sh
lane server      sh server/run.sh

for d in tools/tycho-*/; do
    [ -f "$d/run.sh" ] || continue
    lane "$(basename "$d")" sh "$d/run.sh"
done

echo "tychoc1-check: $n lanes, $fails failed ($(( $(date +%s) - start ))s)"
[ "$fails" -eq 0 ] || exit 1
