#!/bin/sh
# Score tychoc1 on the lanes that until now scored only ./tychoc.
#
# Every runner in this tree spelled its compiler `TYCHOC=./tychoc`, hardcoded,
# so the self-hosted compiler was covered by parse-check alone -- lex, parse and
# an AST census, with typecheck and emit ungated. This substitutes tychoc1 into
# the real runners rather than reimplementing their judgements.
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

# [1] The fixture corpus, through its own runner -- the same fixtures, the same
# goldens, the same ASan leg. The pass count is asserted, because "passed: 0
# failed: 0" is also a zero-failure run -- but NOT against a corpus literal: this
# said `-ge 755` while the corpus was 1001, so a quarter of it could vanish and
# this lane stayed green. `tests/run.sh --count` walks the same class globs the
# runner does, so the corpus half tracks the tree. The literal is only the 8
# STANDALONE cases the runner adds on top of it -- clobber_refused_{bare,out,shim},
# clobber_emit_c, bundle_clean, bundle_blames_right_file, dashname_after_ddash and
# corpus_census -- which move when a CASE is added, never when a fixture is.
STANDALONE=8
if [ "$ONLY" = all ] || [ "$ONLY" = tests ]; then
    n=$((n + 1))
    census=$(sh tests/run.sh --count 2>&1); crc=$?
    corpus=$(printf '%s\n' "$census" | sed -n 's/^count total *\([0-9]*\).*/\1/p' | tail -1)
    want=$(( ${corpus:-0} + STANDALONE ))
    TYCHOC="$C" sh tests/run.sh >"$T/tests.log" 2>&1
    got=$(sed -n 's/^passed: *\([0-9]*\).*/\1/p' "$T/tests.log" | tail -1)
    bad=$(sed -n 's/^passed: *[0-9]* *failed: *\([0-9]*\).*/\1/p' "$T/tests.log" | tail -1)
    if [ "$crc" = 0 ] && [ -n "${corpus:-}" ] && [ "${bad:-1}" = 0 ] && [ "${got:-0}" = "$want" ]; then
        printf '  %-22s ok (passed=%s failed=%s = corpus %s + %s standalone)\n' \
            tests "$got" "$bad" "$corpus" "$STANDALONE"
    else
        printf '  %-22s FAIL (passed=%s failed=%s, expected failed=0 passed=%s from corpus %s + %s)\n' \
            tests "${got:-?}" "${bad:-?}" "$want" "${corpus:-?}" "$STANDALONE"
        [ "$crc" = 0 ] || printf '%s\n' "$census" | tail -2 | sed 's/^/      /'
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

# NOT scored here, deliberately: the STRING-element copy. tycho_arr_str_push
# copies the bytes into the owner arena on its way in and the fused form must
# too; dropping it dangles a string built in the loop scratch. Three synthetic
# fixtures failed to reproduce it -- the emit-side copy already fires for a bare
# local, and a call result did not land in a reset scratch -- so a leg here would
# be decoration. It is held by the corelib, tycho-ledger and tycho-snap lanes
# below, measured: with the copy removed those three FAIL and the rest pass.
#
# Push fusion hoists an array's data/len/cap into locals so -O3 can register them
# (prongB iter-transform: 248 ms -> 214 against ./tychoc's 215). It is a CODEGEN
# SHAPE -- both forms print the same thing, so no golden can see it, and the
# unsafe case is silent too: an array READ in the same body would see a stale
# descriptor. Both directions are asserted.
fusion_leg() {
    n=$((n + 1)); d="$T/fuse"; mkdir -p "$d/y" "$d/n"; bad=0
    printf 'package main\n\nfn main():\n    xs := []int\n    for i := 0; i < 5; i += 1:\n        push(xs, i * 2)\n    for i := 0; i < len(xs); i += 1:\n        println(str(xs[i]))\n' > "$d/y/main.ty"
    printf 'package main\n\nfn main():\n    xs := []int\n    for i := 0; i < 5; i += 1:\n        push(xs, i)\n        println(str(len(xs)))\n' > "$d/n/main.ty"
    "$C" "$d/y/main.ty" --emit-c -o "$d/y/c" >/dev/null 2>&1 || bad=1
    "$C" "$d/n/main.ty" --emit-c -o "$d/n/c" >/dev/null 2>&1 || bad=1
    [ "$(grep -c '_fd[0-9]*l' "$d/y/c.c" 2>/dev/null)" -gt 0 ] || bad=2   # must fuse
    [ "$(grep -c '_fd[0-9]*l' "$d/n/c.c" 2>/dev/null)" -eq 0 ] || bad=3   # must NOT
    for k in y n; do
        cc -O2 -o "$d/$k/bin" "$d/$k/c.c" -lm 2>/dev/null || bad=4
        ./tychoc "$d/$k/main.ty" --emit-c -o "$d/$k/r" >/dev/null 2>&1
        cc -O2 -o "$d/$k/rbin" "$d/$k/r.c" -lm 2>/dev/null || bad=4
        [ "$("$d/$k/bin")" = "$("$d/$k/rbin")" ] || bad=5   # answers must agree
    done
    case $bad in
        0) printf '  %-22s ok\n' push-fusion ;;
        2) printf '  %-22s FAIL (fusion stopped firing)\n' push-fusion; fails=$((fails + 1)) ;;
        3) printf '  %-22s FAIL (fused an array READ in the same body)\n' push-fusion; fails=$((fails + 1)) ;;
        5) printf '  %-22s FAIL (fused code disagrees with ./tychoc)\n' push-fusion; fails=$((fails + 1)) ;;
        *) printf '  %-22s FAIL (probe did not build)\n' push-fusion; fails=$((fails + 1)) ;;
    esac
}
case "$ONLY" in all|push-fusion) fusion_leg ;; esac

lane conc        sh tests/conc/run.sh
lane ffi         sh tests/ffi/run.sh
lane recursion   sh tests/recursion/run.sh
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
