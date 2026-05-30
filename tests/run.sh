#!/bin/sh
# Differential test harness — the verification standard from docs/thesis.md §3.
#
# For every .hi program in examples/ and tests/, transpile it, build BOTH a
# native -O2 binary and an AddressSanitizer+UBSan binary, run both on the same
# stdin, and assert:
#   (a) both exit 0,
#   (b) the sanitizer binary reports no memory / undefined-behaviour error,
#   (c) the two outputs are byte-identical.
#
# (c) is the core check: any undefined behaviour the optimizer and the
# sanitizer disagree on shows up as an output diff. UB aborts the run
# (-fno-sanitize-recover=all), so (a) catches it too.
#
# Arena retention is BY DESIGN (the runtime is a pool allocator; blocks are
# freed at scope exit, and whatever is still live at process exit is reclaimed
# by the OS). LeakSanitizer's "still reachable at exit" is therefore not a bug
# here, so leak detection is disabled — this harness guards CORRECTNESS
# (use-after-free, out-of-bounds, UB), not pool-reclamation timing.
#
# A program may supply fixture stdin as tests/<name>.in (else /dev/null is fed).
# Exit status: 0 iff every program passes.
set -u
cd "$(dirname "$0")/.." || exit 2          # repo root

HIERC=./hierc
[ -x "$HIERC" ] || { echo "no ./hierc — run 'make' first"; exit 2; }

CC="${CC:-cc}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
export ASAN_OPTIONS=detect_leaks=0

pass=0
fail=0
fails=""

# note a problem (prints only); the per-program tally happens once in run_one.
note() { echo "FAIL  $1  ($2)"; }

run_one() {
    hi="$1"
    name="$(basename "$hi" .hi)"
    c="$TMP/$name.c"
    nat="$TMP/$name.native"
    san="$TMP/$name.asan"
    in="tests/$name.in"; [ -f "$in" ] || in=/dev/null
    ok=1

    if ! "$HIERC" "$hi" --emit-c -o "$TMP/$name" >"$TMP/$name.log" 2>&1; then
        note "$name" "transpile"; sed 's/^/      /' "$TMP/$name.log"; ok=0
    elif ! $CC -O2 -std=c11 -o "$nat" "$c" 2>"$TMP/$name.log"; then
        note "$name" "native cc"; sed 's/^/      /' "$TMP/$name.log"; ok=0
    elif ! $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 \
               -std=c11 -o "$san" "$c" 2>"$TMP/$name.log"; then
        note "$name" "sanitizer cc"; sed 's/^/      /' "$TMP/$name.log"; ok=0
    else
        "$nat" <"$in" >"$TMP/$name.nout" 2>/dev/null; nrc=$?
        "$san" <"$in" >"$TMP/$name.sout" 2>"$TMP/$name.serr"; src=$?
        [ "$nrc" -eq 0 ] || { note "$name" "native exit $nrc"; ok=0; }
        if [ "$src" -ne 0 ]; then
            note "$name" "sanitizer exit $src"; sed 's/^/      /' "$TMP/$name.serr"; ok=0
        elif grep -qiE 'runtime error|AddressSanitizer|Sanitizer|ERROR: ' "$TMP/$name.serr"; then
            note "$name" "sanitizer report"; sed 's/^/      /' "$TMP/$name.serr"; ok=0
        fi
        if [ "$ok" -eq 1 ] && ! cmp -s "$TMP/$name.nout" "$TMP/$name.sout"; then
            note "$name" "native vs sanitizer output differ"
            diff "$TMP/$name.nout" "$TMP/$name.sout" | head | sed 's/^/      /'; ok=0
        fi
    fi

    if [ "$ok" -eq 1 ]; then echo "ok    $name"; pass=$((pass + 1))
    else fail=$((fail + 1)); fails="$fails $name"; fi
}

for hi in examples/*.hi tests/*.hi; do
    [ -e "$hi" ] || continue
    run_one "$hi"
done

echo "-----------------------------------------"
echo "passed: $pass   failed: $fail"
[ "$fail" -eq 0 ] || { echo "failed:$fails"; exit 1; }
echo "all green"
