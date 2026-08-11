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
# ILP32 lane (make ilp32): rebuild the emitted C under `gcc -m32` and golden-
# compare, to prove Tycho `int` is width-fixed (int64) and does not truncate on a
# 32-bit-`long` data model. The 32-bit ASan runtime is not shipped under multilib,
# so that lane is skipped here; ASan coverage stays in the 64-bit `make test`.
NO_ASAN="${TYCHO_NO_ASAN:-0}"
# Windows/MSYS2: mingw gcc ships no ASan/UBSan runtime (plan_windows.md phase 2
# -- "mingw ASan is experimental"), so the sanitizer legs cannot link
# (-lasan/-lubsan absent) and every fixture would redden on a leg that is a SKIP
# by design. Force TYCHO_NO_ASAN=1 unless the caller explicitly set it to 0, and
# name native binaries with .exe (MSYS2's exec machinery resolves extensionless
# names unreliably for winpthread-linked PE files).
case "$(uname -s)" in
    *MSYS*|*MINGW*|*CYGWIN*)
        IS_WINDOWS=1
        if [ "$NO_ASAN" = 0 ]; then
            echo "run.sh: Windows -- forcing TYCHO_NO_ASAN=1 (no mingw -lasan/-lubsan; sanitizer legs are the CI's job)"
            NO_ASAN=1
        fi
        EXE=".exe" ;;
    *) IS_WINDOWS=0; EXE="" ;;
esac
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
    # Per-fixture temp dir: Windows/MSYS2 exec fails (127) once a directory
    # holds ~500+ entries (observed under Prism/Defender; the whole-corpus run
    # crosses the threshold mid-way). Each fixture's ~8 files stay in their own
    # subdir; the trap still clears the base $TMP at exit.
    d="$TMP/$name"
    mkdir -p "$d"
    c="$d/$name.c"
    nat="$d/$name.native$EXE"
    san="$d/$name.asan$EXE"
    ok=1

    if ! "$TYCHOC" "$hi" --emit-c -o "$d/$name" >"$d/$name.log" 2>&1; then
        note "$name" "transpile"; sed 's/^/      /' "$d/$name.log"; ok=0
    elif ! $CC -O2 -fwrapv -std=c11 -o "$nat" "$c" -lm 2>"$d/$name.log"; then
        note "$name" "native cc"; sed 's/^/      /' "$d/$name.log"; ok=0
    elif [ "$NO_ASAN" = 0 ] && ! $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 -fwrapv \
               -std=c11 -o "$san" "$c" -lm 2>"$d/$name.log"; then
        note "$name" "sanitizer cc"; sed 's/^/      /' "$d/$name.log"; ok=0
    else
        "$nat" <"$in" >"$d/$name.nout" 2>/dev/null; nrc=$?
        # Windows/MSYS2 flake (observed under Prism emulation): under sustained
        # process churn exec of a PE intermittently fails with 127 -- transient,
        # never a real exit code. Retry once; on Linux this never triggers.
        if [ "$nrc" -eq 127 ] && [ "$IS_WINDOWS" = 1 ]; then
            # the emulator's startup heap-corruption crash (0xC0000374, observed
            # under Prism on ARM64 Windows) is a per-attempt race; retry with
            # backoff.
            for _try in 1 2; do
                sleep 2
                "$nat" <"$in" >"$d/$name.nout" 2>/dev/null; nrc=$?
                [ "$nrc" -eq 0 ] && break
            done
        fi
        [ "$nrc" -eq 0 ] || { note "$name" "native exit $nrc"; ok=0; }
        if [ "$NO_ASAN" = 0 ]; then
            "$san" <"$in" >"$d/$name.sout" 2>"$d/$name.serr"; src=$?
            if [ "$src" -ne 0 ]; then
                note "$name" "sanitizer exit $src"; sed 's/^/      /' "$d/$name.serr"; ok=0
            elif grep -qiE 'runtime error|AddressSanitizer|Sanitizer|ERROR: ' "$d/$name.serr"; then
                note "$name" "sanitizer report"; sed 's/^/      /' "$d/$name.serr"; ok=0
            fi
            if [ "$ok" -eq 1 ] && ! cmp -s "$d/$name.nout" "$d/$name.sout"; then
                note "$name" "native vs sanitizer output differ"
                diff "$d/$name.nout" "$d/$name.sout" | head | sed 's/^/      /'; ok=0
            fi
        fi
    fi

    # record mode: if the build/run was clean, (over)write the golden and stop.
    if [ "$RECORD" = 1 ]; then
        if [ "$ok" -eq 1 ]; then cp "$d/$name.nout" "$g"; echo "rec   $name"; recorded=$((recorded + 1))
        else fail=$((fail + 1)); fails="$fails $name"; fi
        return
    fi

    # (d) golden comparison
    if [ "$ok" -eq 1 ] && [ ! -f "$g" ]; then
        note "$name" "no golden — run 'make test-update'"; ok=0
    elif [ "$ok" -eq 1 ] && ! cmp -s "$d/$name.nout" "$g"; then
        note "$name" "output != golden ($g)"
        diff "$g" "$d/$name.nout" | head | sed 's/^/      /'; ok=0
    fi

    if [ "$ok" -eq 1 ]; then echo "ok    $name"; pass=$((pass + 1))
    else fail=$((fail + 1)); fails="$fails $name"; fi
}

# A single fixture: resolve name/stdin/golden and run the full check.
run_fixture() {
    hi="$1"; name="$(basename "$hi" .ty)"
    in="tests/$name.in"; [ -f "$in" ] || in=/dev/null
    gold="tests/$name.out"
    # Windows-aware golden: the float_str_locale rt= column is untestable on
    # Windows (the roundtrip hook needs newlocale, absent from mingw at every
    # version -- plan_windows.md phase 3); a `<golden>.win` sibling records the
    # rt=-1 rendering. Only ever selected on Windows.
    if [ "$IS_WINDOWS" = 1 ] && [ -f "tests/$name.out.win" ]; then gold="tests/$name.out.win"; fi
    run_one "$hi" "$name" "$gold" "$in"
}

# Worker mode: the pool below spawns a fresh shell per fixture; this mode runs
# exactly one and exits with its verdict. The fixture's own ok/FAIL line has
# already printed.
# Explicit name/gold/in may be passed (the pkg loop); the default derivation
# handles tests/*.ty paths.
if [ "${1:-}" = "--one" ]; then
    if [ "$#" -ge 5 ]; then
        run_one "$2" "$3" "$4" "$5"
    else
        run_fixture "$2"
    fi
    exit $((fail > 0))
fi

# RECORD mode stays sequential because it writes the goldens. Normal runs keep
# the shell/cmp/grep oracle but spread positive fixtures over bounded workers;
# each worker owns its temp directory, and reports are replayed in fixture order.
if [ "$RECORD" = 1 ]; then
    for hi in examples/*.ty tests/*.ty; do
        [ -e "$hi" ] || continue
        run_fixture "$hi"
    done
    for d in tests/pkg/*/; do
        [ -d "$d" ] || continue
        name="$(basename "$d")"
        entry="$d/main.ty"
        if [ ! -f "$entry" ]; then
            note "pkg_$name" "no main.ty"; fail=$((fail + 1)); fails="$fails pkg_$name"; continue
        fi
        in="tests/pkg/$name.in"; [ -f "$in" ] || in=/dev/null
        gold="tests/pkg/$name.out"
        run_one "$entry" "pkg_$name" "$gold" "$in"
    done
else
    test_jobs="${TYCHO_THREADS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"
    case "$test_jobs" in ''|*[!0-9]*|0) echo "tests: TYCHO_THREADS must be a positive integer" >&2; exit 2 ;; esac

    job_count=0
    job_file="$TMP/jobs"
    : >"$job_file"
    queue_positive() {
        job_count=$((job_count + 1))
        index="$job_count"; name="$2"
        printf '%s\n' "$name" >"$TMP/job.$index.name"
        printf '%s\0%s\0%s\0%s\0%s\0' "$index" "$1" "$2" "$3" "$4" >>"$job_file"
    }

    for hi in examples/*.ty tests/*.ty; do
        [ -e "$hi" ] || continue
        name="$(basename "$hi" .ty)"
        gold="tests/$name.out"
        if [ "$IS_WINDOWS" = 1 ] && [ -f "tests/$name.out.win" ]; then gold="tests/$name.out.win"; fi
        queue_positive "$hi" "$name" "$gold" "$(if [ -f "tests/$name.in" ]; then echo "tests/$name.in"; else echo /dev/null; fi)"
    done
    # Package programs: each directory is one multi-file program whose entry is
    # main.ty and whose golden lives beside the package directory.
    for d in tests/pkg/*/; do
        [ -d "$d" ] || continue
        name="$(basename "$d")"
        entry="$d/main.ty"
        if [ ! -f "$entry" ]; then
            job_count=$((job_count + 1)); index="$job_count"
            printf 'FAIL  pkg_%s  (no main.ty)\n' "$name" >"$TMP/job.$index.out"
            printf '1\n' >"$TMP/job.$index.status"
            printf 'pkg_%s\n' "$name" >"$TMP/job.$index.name"
            continue
        fi
        in="tests/pkg/$name.in"; [ -f "$in" ] || in=/dev/null
        gold="tests/pkg/$name.out"
        if [ "$IS_WINDOWS" = 1 ] && [ -f "tests/pkg/$name.out.win" ]; then gold="tests/pkg/$name.out.win"; fi
        queue_positive "$entry" "pkg_$name" "$gold" "$in"
    done
    if [ -s "$job_file" ]; then
        xargs -0 -n 5 -P "$test_jobs" sh -c '
            driver=$1; tmp=$2; index=$3; shift 3
            {
                if sh "$driver" --one "$@"; then code=0; else code=$?; fi
                printf "%s\n" "$code" >"$tmp/job.$index.status"
            } >"$tmp/job.$index.out" 2>&1
        ' sh "$0" "$TMP" <"$job_file"
    fi

    index=1
    while [ "$index" -le "$job_count" ]; do
        cat "$TMP/job.$index.out"
        name="$(cat "$TMP/job.$index.name")"
        if [ "$(cat "$TMP/job.$index.status")" -eq 0 ]; then
            pass=$((pass + 1))
        else
            fail=$((fail + 1)); fails="$fails $name"
        fi
        index=$((index + 1))
    done
fi

# HISTORY: two "post-freeze" loops used to sit here, one for tests/postfreeze/*.ty
# and one for tests/postfreeze/abort/*.ty. That directory existed only because the
# two tychoc0-derived lanes (`compiler/fixpoint.sh`, `scripts/frontparity.sh`)
# globbed `tests/*.ty` and `tests/abort/*.ty` without descending, so a fixture
# using syntax added after the 2026-07-26 freeze reddened them by construction
# unless it lived one directory deeper. Both lanes were RETIRED on 2026-07-29
# (see CLAUDE.md, ROADMAP.md, docs/architecture.md), so the constraint is void:
# the fixtures moved back to tests/ and tests/abort/ on the same day and are now
# scored by the main golden loop above and the tests/abort/ loop below. Nothing
# was lost -- the pass count was 537 before the move and 537 after.

# Negative paths. tests/reject/*.ty are invalid programs the compiler must
# REFUSE (nonzero exit + a diagnostic on stderr/stdout) — guards against
# fail-open parsing/typechecking. tests/abort/*.ty are valid programs whose
# RUN must die cleanly at runtime (nonzero exit + a 'tycho:' message): OOB
# index, pop from empty, reserve out of range. Abort programs run native-only:
# a deliberate exit(1) leaves live arenas, so LeakSanitizer would (correctly,
# uselessly) report them.
# tychoc must reject each with a NONZERO exit AND a non-empty diagnostic (a silent
# refusal is as bad as an acceptance -- the user gets nothing to act on). Until
# 2026-07-26 this lane also built the self-hosted tychoc0 and asserted it refused
# them too; tychoc0 is frozen (see compiler/tychoc0.ty) and no gate builds it, so
# that half is gone. The tychoc assertions below are unchanged.
#
# A FLAT fixture here must NOT declare a `package` header. detect_package
# (src/tychoc.c:12496-12502) turns the entry file's leading `package <name>` into
# a whole-DIRECTORY compile (src/tychoc.c:12893), so such a fixture would be
# scored against all 249 of its siblings: it would be "refused" for the FIRST
# error in sort order rather than for its own defect, and this lane -- which
# asserts only "nonzero exit + non-empty diagnostic" -- cannot tell the two
# apart. A deliberately VALID program dropped in here was measured scoring `ok`
# on a sibling's error. Checked, not assumed: 0 of the 249 flat fixtures declare
# one today, so this guard is enforcement of the existing arrangement rather
# than a repair, and it moves no count. The package-mode reject case belongs in
# the tests/reject/pkg/<name>/ lane below, which gives it its own directory.
for hi in tests/reject/*.ty; do
    [ -e "$hi" ] || continue
    base="$(basename "$hi" .ty)"
    name="reject_$base"
    if grep -q '^package [A-Za-z_]' "$hi"; then
        note "$name" "declares a package header -- it would be compiled against every sibling in tests/reject/; move it to tests/reject/pkg/<name>/"
        fail=$((fail + 1)); fails="$fails $name"; continue
    fi
    if "$TYCHOC" "$hi" --emit-c -o "$TMP/rj" >"$TMP/rj.log" 2>&1; then
        note "$name" "tychoc ACCEPTED an invalid program"; fail=$((fail + 1)); fails="$fails $name"
    elif [ ! -s "$TMP/rj.log" ]; then
        note "$name" "tychoc rejected but with no diagnostic"; fail=$((fail + 1)); fails="$fails $name"
    else
        echo "ok    $name"; pass=$((pass + 1))
    fi
done
# Package-level reject tests: tests/reject/pkg/<name>/ is a multi-file package
# program the compiler must REFUSE (e.g. a cross-package access to a
# package-private `_name`). Its own directory keeps the entry's package-merge
# isolated from the single-file rejects above. Same discipline as above: tychoc
# must exit nonzero AND print a diagnostic.
for d in tests/reject/pkg/*/; do
    [ -d "$d" ] || continue
    name="rejectpkg_$(basename "$d")"
    entry="${d}main.ty"
    [ -f "$entry" ] || continue
    if "$TYCHOC" "$entry" --emit-c -o "$TMP/rjp" >"$TMP/rjp.log" 2>&1; then
        note "$name" "tychoc ACCEPTED an invalid package program"; fail=$((fail + 1)); fails="$fails $name"
    elif [ ! -s "$TMP/rjp.log" ]; then
        note "$name" "tychoc rejected but with no diagnostic"; fail=$((fail + 1)); fails="$fails $name"
    else
        echo "ok    $name"; pass=$((pass + 1))
    fi
done
# Runtime aborts: the fixture must BUILD with tychoc and then DIE cleanly --
# nonzero exit plus a 'tycho:' message on stderr, never a silent exit 0 and never
# a bare signal. Until 2026-07-26 this lane also built each fixture with tychoc0
# and required byte-identical stderr and the same exit status from both; tychoc0
# is frozen and no gate builds it, so that comparison is gone. The tychoc
# assertions -- builds, fires, names itself -- are unchanged.
for hi in tests/abort/*.ty; do
    [ -e "$hi" ] || continue
    name="abort_$(basename "$hi" .ty)"
    if ! "$TYCHOC" "$hi" --emit-c -o "$TMP/ab" >"$TMP/ab.log" 2>&1 \
       || ! $CC -O2 -fwrapv -std=c11 -o "$TMP/ab.bin" "$TMP/ab.c" -lm 2>"$TMP/ab.log"; then
        note "$name" "tychoc did not build"; sed 's/^/      /' "$TMP/ab.log"
        fail=$((fail + 1)); fails="$fails $name"; continue
    fi
    "$TMP/ab.bin"  </dev/null >/dev/null 2>"$TMP/ab.err";  rc=$?
    if [ "$rc" -eq 0 ]; then
        note "$name" "runtime abort did not fire (exit 0)"; fail=$((fail + 1)); fails="$fails $name"
    elif [ -f "tests/abort/$(basename "$hi" .ty).err" ]; then
        # A fixture MAY lock its exact stderr instead of the `tycho:` grep: an
        # `Err` out of `main` prints the PROGRAM's message, not a runtime trap,
        # so there is no `tycho:` to look for. Record with RECORD=1.
        g="tests/abort/$(basename "$hi" .ty).err"
        if [ "$RECORD" = 1 ]; then cp "$TMP/ab.err" "$g"; echo "rec   $name"; recorded=$((recorded + 1))
        elif cmp -s "$TMP/ab.err" "$g"; then echo "ok    $name"; pass=$((pass + 1))
        else
            note "$name" "stderr differs from $g"; diff "$g" "$TMP/ab.err" | head | sed 's/^/      /'
            fail=$((fail + 1)); fails="$fails $name"
        fi
    elif ! grep -q 'tycho:' "$TMP/ab.err"; then
        note "$name" "died (exit $rc) but without a 'tycho:' message"; sed 's/^/      /' "$TMP/ab.err"
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
