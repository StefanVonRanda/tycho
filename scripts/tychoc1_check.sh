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

# Only TWO of these sites ever reached a shell in tychoc1, and the distinction is
# the point: --pkg and the corelib `deps` lookup go through os.run, which is popen,
# and tychoc1 had no charset guard there -- `--pkg 'sqlite3; touch X'` ran the
# touch, measured 2026-08-30. --link and `extern "Lib"` reach os.exec's argv
# (driver.ty@os.exec), never a shell, so they were not injectable here even though
# the same names ARE a shell hole in ./tychoc, which links via system()
# (src/tychoc.c@cc_safe_name). They are guarded anyway, for one message and one
# rule across both compilers, and scored below for parity rather than for safety.
# The load-bearing leg is --pkg: removing its guard reddens this, removing the
# extern one does not. --emit-c assembles no link line, so every leg links for real.
injection_leg() {
    n=$((n + 1))
    d="$T/inj"; mkdir -p "$d/a" "$d/b" "$d/c"; mark="$d/PWNED"; bad=0
    printf 'package main\n\nextern "m; touch %s" fn cbrt(x: float) -> float\n\nfn main():\n    println(str(cbrt(8.0)))\n' "$mark" > "$d/a/main.ty"
    printf 'package main\n\nfn main():\n    println("hi")\n' > "$d/b/main.ty"
    printf 'package main\n\nextern "m" fn cbrt(x: float) -> float\n\nfn main():\n    println(str(cbrt(8.0)))\n' > "$d/c/main.ty"
    TYCHOC="$C" "$C" "$d/a/main.ty"     -o "$d/a/bin"                      >/dev/null 2>&1 && bad=1
    "$C" "$d/b/main.ty" -o "$d/b/bin" --link "m; touch $mark"              >/dev/null 2>&1 && bad=1
    "$C" "$d/b/main.ty" -o "$d/b/bin" --pkg  "sqlite3; touch $mark"        >/dev/null 2>&1 && bad=1
    [ -e "$mark" ] && bad=2
    # positive control: a legal extern library must still link and run, or the
    # guard is "refuse everything" and the three refusals above prove nothing
    "$C" "$d/c/main.ty" -o "$d/c/bin" >/dev/null 2>&1 && [ "$("$d/c/bin")" = "2.0" ] || bad=3
    case $bad in
        0) printf '  %-22s ok\n' shell-injection ;;
        2) printf '  %-22s FAIL (an injected command EXECUTED)\n' shell-injection; fails=$((fails + 1)) ;;
        3) printf '  %-22s FAIL (a legal extern library stopped linking)\n' shell-injection; fails=$((fails + 1)) ;;
        *) printf '  %-22s FAIL (an injected name was accepted)\n' shell-injection; fails=$((fails + 1)) ;;
    esac
}
case "$ONLY" in all|shell-injection) injection_leg ;; esac

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
