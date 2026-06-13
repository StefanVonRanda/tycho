#!/bin/sh
# Differential + golden test harness — the verification standard from
# docs/thesis.md §3, plus an expected-output check.
#
# For every .hi program in examples/ and tests/, transpile it, build BOTH a
# native -O2 binary and an AddressSanitizer+UBSan binary, run both on the same
# stdin, and assert:
#   (a) both exit 0,
#   (b) the sanitizer binary reports no memory / undefined-behaviour error,
#   (c) the two outputs are byte-identical,
#   (d) the output matches the committed golden tests/<name>.out.
#
# (c) catches undefined behaviour the optimizer and the sanitizer disagree on.
# But (c) does NOT catch a miscompile that produces the SAME wrong output in
# both builds (e.g. reading a double array slot as a long) — both agree, just
# wrongly. (d) is what catches that: the golden is the recorded correct output,
# so any value regression fails the build instead of needing a human to notice.
#
# Goldens are recorded only by `make test-update` (RECORD=1), never by a normal
# run — so a regression can't silently rebake itself into the expected file.
# Review the diff before committing a re-record.
#
# Leak detection is ON: under the implicit-arena model every scope frees its
# arena at exit (including main's), so at normal process exit nothing should
# remain allocated. A LeakSanitizer report means a real bug — a missing arena
# free — most likely an early `return` that skipped a loop/if scratch arena.
#
# A program may supply fixture stdin as tests/<name>.in (else /dev/null is fed).
# Exit status: 0 iff every program passes (or, under RECORD=1, builds + runs).
set -u
cd "$(dirname "$0")/.." || exit 2          # repo root

HIERC=./hierc
[ -x "$HIERC" ] || { echo "no ./hierc — run 'make' first"; exit 2; }

CC="${CC:-cc}"
RECORD="${RECORD:-0}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
export ASAN_OPTIONS=detect_leaks=1

[ "$RECORD" = 1 ] && echo "*** RECORD MODE: rewriting tests/*.out goldens — review the diff before committing ***"

pass=0
fail=0
recorded=0
fails=""

# note a problem (prints only); the per-program tally happens once in run_one.
note() { echo "FAIL  $1  ($2)"; }

# run_one <entry.hi> <name> <golden.out> <stdin>
run_one() {
    hi="$1"; name="$2"; g="$3"; in="$4"
    c="$TMP/$name.c"
    nat="$TMP/$name.native"
    san="$TMP/$name.asan"
    ok=1

    if ! "$HIERC" "$hi" --emit-c -o "$TMP/$name" >"$TMP/$name.log" 2>&1; then
        note "$name" "transpile"; sed 's/^/      /' "$TMP/$name.log"; ok=0
    elif ! $CC -O2 -std=c11 -o "$nat" "$c" -lm 2>"$TMP/$name.log"; then
        note "$name" "native cc"; sed 's/^/      /' "$TMP/$name.log"; ok=0
    elif ! $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 \
               -std=c11 -o "$san" "$c" -lm 2>"$TMP/$name.log"; then
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

    # record mode: if the build/run was clean, (over)write the golden and stop.
    if [ "$RECORD" = 1 ]; then
        if [ "$ok" -eq 1 ]; then cp "$TMP/$name.nout" "$g"; echo "rec   $name"; recorded=$((recorded + 1))
        else fail=$((fail + 1)); fails="$fails $name"; fi
        return
    fi

    # (d) golden comparison
    if [ "$ok" -eq 1 ] && [ ! -f "$g" ]; then
        note "$name" "no golden — run 'make test-update'"; ok=0
    elif [ "$ok" -eq 1 ] && ! cmp -s "$TMP/$name.nout" "$g"; then
        note "$name" "output != golden ($g)"
        diff "$g" "$TMP/$name.nout" | head | sed 's/^/      /'; ok=0
    fi

    if [ "$ok" -eq 1 ]; then echo "ok    $name"; pass=$((pass + 1))
    else fail=$((fail + 1)); fails="$fails $name"; fi
}

for hi in examples/*.hi tests/*.hi; do
    [ -e "$hi" ] || continue
    name="$(basename "$hi" .hi)"
    in="tests/$name.in"; [ -f "$in" ] || in=/dev/null
    run_one "$hi" "$name" "tests/$name.out" "$in"
done

# Package programs: each tests/pkg/<name>/ is one multi-file package program
# whose entry is main.hi (the compiler merges the whole directory and follows
# its imports). Golden is tests/pkg/<name>.out — same native-vs-ASan + golden
# discipline as single files.
for d in tests/pkg/*/; do
    [ -d "$d" ] || continue
    name="$(basename "$d")"
    entry="$d/main.hi"
    if [ ! -f "$entry" ]; then
        note "pkg_$name" "no main.hi"; fail=$((fail + 1)); fails="$fails pkg_$name"; continue
    fi
    in="tests/pkg/$name.in"; [ -f "$in" ] || in=/dev/null
    run_one "$entry" "pkg_$name" "tests/pkg/$name.out" "$in"
done

# Negative paths. tests/reject/*.hi are invalid programs the compiler must
# REFUSE (nonzero exit + a diagnostic on stderr/stdout) — guards against
# fail-open parsing/typechecking. tests/abort/*.hi are valid programs whose
# RUN must die cleanly at runtime (nonzero exit + a 'hier:' message): OOB
# index, pop from empty, reserve out of range. Abort programs run native-only:
# a deliberate exit(1) leaves live arenas, so LeakSanitizer would (correctly,
# uselessly) report them.
# BOTH compilers must reject these (the reject path is now differential too --
# the malformed-input fuzzer found hierc0 had been fail-opening on many of these
# where hierc rejects). Build hierc0 once for the hierc0-side assertion.
# SKIP-LIST: newtype distinctness -- hierc0 stores zero-cost newtypes as their
# underlying type (identity erased at declaration), so it cannot distinguish a
# UserId from str; a documented less-strict limitation, with hierc as the
# validating oracle. See tests/reject/newtype_{agg,key}_mix.hi.
"$HIERC" compiler/hierc0.hi -o "$TMP/h0" >/dev/null 2>&1 || { echo "could not build hierc0 for reject checks"; exit 2; }
H0_REJECT_SKIP=" newtype_agg_mix newtype_key_mix "
for hi in tests/reject/*.hi; do
    [ -e "$hi" ] || continue
    base="$(basename "$hi" .hi)"
    name="reject_$base"
    skip0=0; case "$H0_REJECT_SKIP" in *" $base "*) skip0=1 ;; esac
    if "$HIERC" "$hi" --emit-c -o "$TMP/rj" >"$TMP/rj.log" 2>&1; then
        note "$name" "hierc ACCEPTED an invalid program"; fail=$((fail + 1)); fails="$fails $name"
    elif [ ! -s "$TMP/rj.log" ]; then
        note "$name" "hierc rejected but with no diagnostic"; fail=$((fail + 1)); fails="$fails $name"
    elif [ "$skip0" = 0 ] && "$TMP/h0" "$hi" --emit-c >/dev/null 2>"$TMP/rj0.log"; then
        note "$name" "hierc0 ACCEPTED an invalid program (fail-open)"; fail=$((fail + 1)); fails="$fails $name"
    else
        echo "ok    $name"; pass=$((pass + 1))
    fi
done
for hi in tests/abort/*.hi; do
    [ -e "$hi" ] || continue
    name="abort_$(basename "$hi" .hi)"
    if ! "$HIERC" "$hi" --emit-c -o "$TMP/ab" >"$TMP/ab.log" 2>&1 \
       || ! $CC -O2 -std=c11 -o "$TMP/ab.bin" "$TMP/ab.c" -lm 2>"$TMP/ab.log"; then
        note "$name" "did not build"; sed 's/^/      /' "$TMP/ab.log"
        fail=$((fail + 1)); fails="$fails $name"; continue
    fi
    "$TMP/ab.bin" </dev/null >/dev/null 2>"$TMP/ab.err"; rc=$?
    if [ "$rc" -eq 0 ]; then
        note "$name" "runtime abort did not fire (exit 0)"; fail=$((fail + 1)); fails="$fails $name"
    elif ! grep -q 'hier:' "$TMP/ab.err"; then
        note "$name" "died (exit $rc) but without a 'hier:' message"; sed 's/^/      /' "$TMP/ab.err"
        fail=$((fail + 1)); fails="$fails $name"
    else
        echo "ok    $name"; pass=$((pass + 1))
    fi
done

# Diagnostics goldens. tests/diag/<name>.hi are invalid programs whose EXACT
# compiler stderr (message + source-line snippet + caret + did-you-mean) is
# locked as tests/diag/<name>.err — so an error-quality regression fails the
# build, same discipline as the .out goldens (record with RECORD=1, review the
# diff). hierc only: hierc0's bootstrap diagnostics are deliberately simpler.
for hi in tests/diag/*.hi; do
    [ -e "$hi" ] || continue
    name="diag_$(basename "$hi" .hi)"
    g="tests/diag/$(basename "$hi" .hi).err"
    if "$HIERC" "$hi" --emit-c -o "$TMP/dg" >"$TMP/dg.log" 2>&1; then
        note "$name" "compiler ACCEPTED an invalid program"; fail=$((fail + 1)); fails="$fails $name"; continue
    fi
    if [ "$RECORD" = 1 ]; then
        cp "$TMP/dg.log" "$g"; echo "rec   $name"; recorded=$((recorded + 1)); continue
    fi
    if [ ! -f "$g" ]; then
        note "$name" "no golden — run 'make test-update'"; fail=$((fail + 1)); fails="$fails $name"
    elif ! cmp -s "$TMP/dg.log" "$g"; then
        note "$name" "diagnostic != golden ($g)"
        diff "$g" "$TMP/dg.log" | head | sed 's/^/      /'; fail=$((fail + 1)); fails="$fails $name"
    else
        echo "ok    $name"; pass=$((pass + 1))
    fi
done

echo "-----------------------------------------"
if [ "$RECORD" = 1 ]; then
    echo "recorded: $recorded   failed: $fail"
    [ "$fail" -eq 0 ] || { echo "failed:$fails"; exit 1; }
    echo "goldens written — review 'git diff tests/' before committing"
    exit 0
fi
echo "passed: $pass   failed: $fail"
[ "$fail" -eq 0 ] || { echo "failed:$fails"; exit 1; }
echo "all green"
