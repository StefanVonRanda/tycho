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
    d="$TMP/$name"
    mkdir -p "$d"
    c="$d/$name.c"
    nat="$d/$name.native$EXE"
    san="$d/$name.asan$EXE"
    ok=1

    shims="$("$TYCHOC" "$hi" --print-shims 2>/dev/null | tr '\n' ' ')"

    if ! "$TYCHOC" "$hi" --emit-c -o "$d/$name" >"$d/$name.log" 2>&1; then
        note "$name" "transpile"; sed 's/^/      /' "$d/$name.log"; ok=0
    elif ! $CC -O2 -fwrapv -std=c11 -o "$nat" "$c" $shims -lm 2>"$d/$name.log"; then
        note "$name" "native cc"; sed 's/^/      /' "$d/$name.log"; ok=0
    elif [ "$NO_ASAN" = 0 ] && ! $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 -fwrapv \
               -std=c11 -o "$san" "$c" $shims -lm 2>"$d/$name.log"; then
        note "$name" "sanitizer cc"; sed 's/^/      /' "$d/$name.log"; ok=0
    else
        "$nat" <"$in" >"$d/$name.nout" 2>/dev/null; nrc=$?
        if [ "$nrc" -eq 127 ] && [ "$IS_WINDOWS" = 1 ]; then
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
        miss=''; sed -n 's/^# expect: //p' "$hi" > "$TMP/rj.exp"
        while IFS= read -r exp; do
            if [ -n "$exp" ] && ! grep -qF -- "$exp" "$TMP/rj.log"; then miss="$exp"; break; fi
        done < "$TMP/rj.exp"
        if [ -n "$miss" ]; then
            note "$name" "diagnostic does not contain the expected text: $miss"
            head -5 "$TMP/rj.log" | sed 's/^/      /'
            fail=$((fail + 1)); fails="$fails $name"; continue
        fi
        echo "ok    $name"; pass=$((pass + 1))
    fi
done
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
        # Same OPT-IN as the flat lane above: a `# expect: <text>` line in main.ty
        # pins WHY, as a literal substring. Here it is also how a fixture pins
        # WHICH FILE a diagnostic names -- the reason this lane grew the option.
        # Every `# expect:` line is asserted, as in the flat lane -- which is how
        # generic_inst_callsite pins BOTH the template's line and the call's.
        miss=''; sed -n 's/^# expect: //p' "$entry" > "$TMP/rjp.exp"
        while IFS= read -r exp; do
            if [ -n "$exp" ] && ! grep -qF -- "$exp" "$TMP/rjp.log"; then miss="$exp"; break; fi
        done < "$TMP/rjp.exp"
        if [ -n "$miss" ]; then
            note "$name" "diagnostic does not contain the expected text: $miss"
            head -5 "$TMP/rjp.log" | sed 's/^/      /'
            fail=$((fail + 1)); fails="$fails $name"; continue
        fi
        echo "ok    $name"; pass=$((pass + 1))
    fi
done
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

for d in tests/warn/pkg/*/; do
    [ -d "$d" ] || continue
    base="$(basename "$d")"
    name="warnpkg_$base"
    g="tests/warn/pkg/$base.err"
    entry="${d}main.ty"
    [ -f "$entry" ] || continue
    if ! "$TYCHOC" "$entry" --emit-c -o "$TMP/wnp" >"$TMP/wnp.out" 2>"$TMP/wnp.err"; then
        note "$name" "compiler REJECTED a valid package program"; sed 's/^/      /' "$TMP/wnp.err"
        fail=$((fail + 1)); fails="$fails $name"; continue
    fi
    if [ "$RECORD" = 1 ]; then
        cp "$TMP/wnp.err" "$g"; echo "rec   $name"; recorded=$((recorded + 1)); continue
    fi
    if [ ! -f "$g" ]; then
        note "$name" "no golden — run 'make test-update'"; fail=$((fail + 1)); fails="$fails $name"
    elif ! cmp -s "$TMP/wnp.err" "$g"; then
        note "$name" "stderr != golden ($g)"
        diff "$g" "$TMP/wnp.err" | head | sed 's/^/      /'; fail=$((fail + 1)); fails="$fails $name"
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
