#!/bin/sh
# Differential + golden test harness — the verification standard from
# docs/thesis.md §3, plus an expected-output check.
#
# For every .ty program in examples/ and tests/, transpile it, build BOTH a
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

TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc — run 'make' first"; exit 2; }

CC="${CC:-cc}"
RECORD="${RECORD:-0}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
# Leak detection requires LeakSanitizer, which Apple's arm64/x86_64 ASan does
# not ship — there, detect_leaks=1 aborts every sanitizer binary at exit
# regardless of correctness. Gate it by OS: full leak checking on Linux (where
# a leak is a real arena-free bug), ASan+UBSan only on macOS.
case "$(uname -s)" in Darwin) TYCHO_LSAN=0 ;; *) TYCHO_LSAN=1 ;; esac
export ASAN_OPTIONS=detect_leaks=$TYCHO_LSAN

[ "$RECORD" = 1 ] && echo "*** RECORD MODE: rewriting tests/*.out goldens — review the diff before committing ***"

pass=0
fail=0
recorded=0
fails=""

# note a problem (prints only); the per-program tally happens once in run_one.
note() { echo "FAIL  $1  ($2)"; }

# run_one <entry.ty> <name> <golden.out> <stdin>
run_one() {
    hi="$1"; name="$2"; g="$3"; in="$4"
    c="$TMP/$name.c"
    nat="$TMP/$name.native"
    san="$TMP/$name.asan"
    ok=1

    if ! "$TYCHOC" "$hi" --emit-c -o "$TMP/$name" >"$TMP/$name.log" 2>&1; then
        note "$name" "transpile"; sed 's/^/      /' "$TMP/$name.log"; ok=0
    elif ! $CC -O2 -fwrapv -std=c11 -o "$nat" "$c" -lm 2>"$TMP/$name.log"; then
        note "$name" "native cc"; sed 's/^/      /' "$TMP/$name.log"; ok=0
    elif ! $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 -fwrapv \
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

for hi in examples/*.ty tests/*.ty; do
    [ -e "$hi" ] || continue
    name="$(basename "$hi" .ty)"
    in="tests/$name.in"; [ -f "$in" ] || in=/dev/null
    run_one "$hi" "$name" "tests/$name.out" "$in"
done

# Package programs: each tests/pkg/<name>/ is one multi-file package program
# whose entry is main.ty (the compiler merges the whole directory and follows
# its imports). Golden is tests/pkg/<name>.out — same native-vs-ASan + golden
# discipline as single files.
for d in tests/pkg/*/; do
    [ -d "$d" ] || continue
    name="$(basename "$d")"
    entry="$d/main.ty"
    if [ ! -f "$entry" ]; then
        note "pkg_$name" "no main.ty"; fail=$((fail + 1)); fails="$fails pkg_$name"; continue
    fi
    in="tests/pkg/$name.in"; [ -f "$in" ] || in=/dev/null
    run_one "$entry" "pkg_$name" "tests/pkg/$name.out" "$in"
done

# Negative paths. tests/reject/*.ty are invalid programs the compiler must
# REFUSE (nonzero exit + a diagnostic on stderr/stdout) — guards against
# fail-open parsing/typechecking. tests/abort/*.ty are valid programs whose
# RUN must die cleanly at runtime (nonzero exit + a 'tycho:' message): OOB
# index, pop from empty, reserve out of range. Abort programs run native-only:
# a deliberate exit(1) leaves live arenas, so LeakSanitizer would (correctly,
# uselessly) report them.
# BOTH compilers must reject these (the reject path is now differential too --
# the malformed-input fuzzer found tychoc0 had been fail-opening on many of these
# where tychoc rejects). Build tychoc0 once for the tychoc0-side assertion.
# (newtype distinctness used to be skip-listed here -- tychoc0 erased newtype
# identity. As of the newtype-identity parity work, tychoc0 tracks identity through
# the checker and rejects the mismatches too, so the skip-list is empty.)
"$TYCHOC" compiler/tychoc0.ty -o "$TMP/h0" >/dev/null 2>&1 || { echo "could not build tychoc0 for reject checks"; exit 2; }
H0_REJECT_SKIP=" "
for hi in tests/reject/*.ty; do
    [ -e "$hi" ] || continue
    base="$(basename "$hi" .ty)"
    name="reject_$base"
    skip0=0; case "$H0_REJECT_SKIP" in *" $base "*) skip0=1 ;; esac
    if "$TYCHOC" "$hi" --emit-c -o "$TMP/rj" >"$TMP/rj.log" 2>&1; then
        note "$name" "tychoc ACCEPTED an invalid program"; fail=$((fail + 1)); fails="$fails $name"
    elif [ ! -s "$TMP/rj.log" ]; then
        note "$name" "tychoc rejected but with no diagnostic"; fail=$((fail + 1)); fails="$fails $name"
    elif [ "$skip0" = 0 ] && "$TMP/h0" "$hi" --emit-c >/dev/null 2>"$TMP/rj0.log"; then
        note "$name" "tychoc0 ACCEPTED an invalid program (fail-open)"; fail=$((fail + 1)); fails="$fails $name"
    else
        echo "ok    $name"; pass=$((pass + 1))
    fi
done
# Package-level reject tests: tests/reject/pkg/<name>/ is a multi-file package
# program the compiler must REFUSE (e.g. a cross-package access to a
# package-private `_name`). Its own directory keeps the entry's package-merge
# isolated from the single-file rejects above. Both compilers must reject it.
for d in tests/reject/pkg/*/; do
    [ -d "$d" ] || continue
    name="rejectpkg_$(basename "$d")"
    entry="${d}main.ty"
    [ -f "$entry" ] || continue
    if "$TYCHOC" "$entry" --emit-c -o "$TMP/rjp" >"$TMP/rjp.log" 2>&1; then
        note "$name" "tychoc ACCEPTED an invalid package program"; fail=$((fail + 1)); fails="$fails $name"
    elif [ ! -s "$TMP/rjp.log" ]; then
        note "$name" "tychoc rejected but with no diagnostic"; fail=$((fail + 1)); fails="$fails $name"
    elif "$TMP/h0" "$entry" --emit-c >/dev/null 2>"$TMP/rjp0.log"; then
        note "$name" "tychoc0 ACCEPTED an invalid package program (fail-open)"; fail=$((fail + 1)); fails="$fails $name"
    else
        echo "ok    $name"; pass=$((pass + 1))
    fi
done
# Differential: BOTH compilers must die identically on the same fixture. The
# reference (tychoc) side asserts the abort fires at all (nonzero exit + a
# 'tycho:' message); the self-hosted (tychoc0) side then asserts it dies with
# the SAME stderr byte-for-byte and the SAME exit status. Before this, tychoc0's
# runtime traps were locked only by rtparity, which proves the trap TEXT exists
# in the emitted C, not that it fires on the same input. Reuses the $TMP/h0
# tychoc0 built above; it emits C to stdout.
for hi in tests/abort/*.ty; do
    [ -e "$hi" ] || continue
    name="abort_$(basename "$hi" .ty)"
    if ! "$TYCHOC" "$hi" --emit-c -o "$TMP/ab" >"$TMP/ab.log" 2>&1 \
       || ! $CC -O2 -fwrapv -std=c11 -o "$TMP/ab.bin" "$TMP/ab.c" -lm 2>"$TMP/ab.log"; then
        note "$name" "tychoc did not build"; sed 's/^/      /' "$TMP/ab.log"
        fail=$((fail + 1)); fails="$fails $name"; continue
    fi
    if ! "$TMP/h0" "$hi" >"$TMP/ab0.c" 2>"$TMP/ab.log" \
       || ! $CC -O2 -fwrapv -std=c11 -o "$TMP/ab0.bin" "$TMP/ab0.c" -lm 2>"$TMP/ab.log"; then
        note "$name" "tychoc0 did not build"; sed 's/^/      /' "$TMP/ab.log"
        fail=$((fail + 1)); fails="$fails $name"; continue
    fi
    "$TMP/ab.bin"  </dev/null >/dev/null 2>"$TMP/ab.err";  rc=$?
    "$TMP/ab0.bin" </dev/null >/dev/null 2>"$TMP/ab0.err"; rc0=$?
    if [ "$rc" -eq 0 ]; then
        note "$name" "runtime abort did not fire (exit 0)"; fail=$((fail + 1)); fails="$fails $name"
    elif ! grep -q 'tycho:' "$TMP/ab.err"; then
        note "$name" "died (exit $rc) but without a 'tycho:' message"; sed 's/^/      /' "$TMP/ab.err"
        fail=$((fail + 1)); fails="$fails $name"
    elif [ "$rc0" -ne "$rc" ]; then
        note "$name" "compilers diverge on exit status (tychoc $rc, tychoc0 $rc0)"
        fail=$((fail + 1)); fails="$fails $name"
    elif ! cmp -s "$TMP/ab.err" "$TMP/ab0.err"; then
        note "$name" "compilers diverge on abort message"
        diff "$TMP/ab.err" "$TMP/ab0.err" | head | sed 's/^/      /'
        fail=$((fail + 1)); fails="$fails $name"
    else
        echo "ok    $name"; pass=$((pass + 1))
    fi
done

# Diagnostics goldens. tests/diag/<name>.ty are invalid programs whose EXACT
# compiler stderr (message + source-line snippet + caret + did-you-mean) is
# locked as tests/diag/<name>.err — so an error-quality regression fails the
# build, same discipline as the .out goldens (record with RECORD=1, review the
# diff). tychoc only: tychoc0's bootstrap diagnostics are deliberately simpler.
for hi in tests/diag/*.ty; do
    [ -e "$hi" ] || continue
    name="diag_$(basename "$hi" .ty)"
    g="tests/diag/$(basename "$hi" .ty).err"
    if "$TYCHOC" "$hi" --emit-c -o "$TMP/dg" >"$TMP/dg.log" 2>&1; then
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

# The SAME fixtures through tychoc0, locked separately as tests/diag/<name>.h0err.
# The reject lane already asserts tychoc0 refuses invalid programs; this asserts
# WHAT IT SAYS, which nothing did before — a refactor could have degraded every
# self-hosted message to a bare "type error" with no line number and the build
# would still have gone green. The goldens are deliberately a SEPARATE file, not
# a shared one: tychoc0's format ("line N: ...") and its wording are behind the C
# compiler's on purpose (no did-you-mean, fewer hints), so holding them to one
# golden would either block this lane or force a premature rewrite. Convergence
# work now shows up as a diff in these files. Reuses the $TMP/h0 built above.
for hi in tests/diag/*.ty; do
    [ -e "$hi" ] || continue
    base="$(basename "$hi" .ty)"
    name="diag0_$base"
    g="tests/diag/$base.h0err"
    if "$TMP/h0" "$hi" --emit-c >/dev/null 2>"$TMP/dg0.log"; then
        note "$name" "tychoc0 ACCEPTED an invalid program"; fail=$((fail + 1)); fails="$fails $name"; continue
    fi
    if [ "$RECORD" = 1 ]; then
        cp "$TMP/dg0.log" "$g"; echo "rec   $name"; recorded=$((recorded + 1)); continue
    fi
    if [ ! -f "$g" ]; then
        note "$name" "no golden — run 'make test-update'"; fail=$((fail + 1)); fails="$fails $name"
    elif ! cmp -s "$TMP/dg0.log" "$g"; then
        note "$name" "tychoc0 diagnostic != golden ($g)"
        diff "$g" "$TMP/dg0.log" | head | sed 's/^/      /'; fail=$((fail + 1)); fails="$fails $name"
    else
        echo "ok    $name"; pass=$((pass + 1))
    fi
done

# Warning goldens. tests/warn/<name>.ty are VALID programs (the compiler must
# ACCEPT them) whose exact warning output is locked as tests/warn/<name>.err.
# The diag loop above cannot cover these: it asserts a NONZERO exit, so every
# warning the compiler emits on a program it accepts — the channel-liveness
# lints (CC-6), a discarded Result, a non-advancing loop condition, a
# fall-off-the-end — had nothing holding it, and could stop firing unnoticed.
#
# COMPILE ONLY, never run: a deadlock fixture hangs by construction and a
# no-progress loop spins forever. That is also why they live in tests/warn/
# rather than tests/ — the main loop above builds AND runs everything it globs.
# Only stderr is captured: the "wrote <path>" line goes to stdout and would bake
# a $TMP path into the golden. tychoc only, like the diag lane (tychoc0's
# diagnostic format differs by design, though CC-6's text is identical in both).
for hi in tests/warn/*.ty; do
    [ -e "$hi" ] || continue
    name="warn_$(basename "$hi" .ty)"
    g="tests/warn/$(basename "$hi" .ty).err"
    if ! "$TYCHOC" "$hi" --emit-c -o "$TMP/wn" >"$TMP/wn.out" 2>"$TMP/wn.err"; then
        note "$name" "compiler REJECTED a valid program"; sed 's/^/      /' "$TMP/wn.err"
        fail=$((fail + 1)); fails="$fails $name"; continue
    fi
    if ! grep -q 'warning:' "$TMP/wn.err"; then
        note "$name" "no warning emitted (the lint this fixture locks stopped firing)"
        fail=$((fail + 1)); fails="$fails $name"; continue
    fi
    if [ "$RECORD" = 1 ]; then
        cp "$TMP/wn.err" "$g"; echo "rec   $name"; recorded=$((recorded + 1)); continue
    fi
    if [ ! -f "$g" ]; then
        note "$name" "no golden — run 'make test-update'"; fail=$((fail + 1)); fails="$fails $name"
    elif ! cmp -s "$TMP/wn.err" "$g"; then
        note "$name" "warning != golden ($g)"
        diff "$g" "$TMP/wn.err" | head | sed 's/^/      /'; fail=$((fail + 1)); fails="$fails $name"
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
