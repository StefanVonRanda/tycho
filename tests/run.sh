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

# The `passed:` line below counts CASES, not fixtures, so neither it nor
# `make status`'s file count can catch a fixture DIRECTORY silently falling out
# of the globs: both figures would simply move, and neither says which class.
# This walks the class globs the loops below use -- the same variables, so a
# narrowed glob moves both -- prints the total per class, and asserts two things
# against literals -- no class may be empty, and every tracked directory under
# tests/ holding a .ty must be claimed by a class glob or named in ELSEWHERE as
# some other runner's corpus. `sh tests/run.sh --count` prints it; a normal run
# checks it silently and fails the lane.
CORPUS_ELSEWHERE="tests/conc tests/ffi tests/rtparity"   # roots owned by another runner
G_positive="examples/*.ty tests/*.ty"
G_pkg="tests/pkg/*/"
G_reject="tests/reject/*.ty"
G_rejectpkg="tests/reject/pkg/*/"
G_abort="tests/abort/*.ty"
G_diag="tests/diag/*.ty"
G_warn="tests/warn/*.ty"
G_warnpkg="tests/warn/pkg/*/"

corpus_census() {
    cc_show="$1"; cc_bad=''; cc_total=0; cc_classes=0
    for cc_name in positive pkg reject rejectpkg abort diag warn warnpkg; do
        eval "cc_pat=\$G_$cc_name"
        cc_n=0
        for cc_e in $cc_pat; do [ -e "$cc_e" ] && cc_n=$((cc_n + 1)); done
        cc_total=$((cc_total + cc_n)); cc_classes=$((cc_classes + 1))
        [ "$cc_show" = print ] && printf 'count %-10s %d\n' "$cc_name" "$cc_n"
        [ "$cc_n" -gt 0 ] || cc_bad="$cc_bad empty-class:$cc_name"
    done
    # An `# expect:` line is OPT-IN, so a fixture that loses one silently falls
    # back to scoring on the verdict alone -- refused for ANY reason, including a
    # rule other than the one it was written for. That is invisible: the lane
    # still prints `ok`. So the count is pinned to a literal here. 209 = the 82 at
    # R16c-4, plus 27 of the 29 R21d wrote for bootstrap rules no fixture reached,
    # plus 2 of the 3 R21d-2 added, plus 16 of the 18 R21e added, plus 23 of the
    # 25 R21f added for the builtin-call block at src/tychoc.c:7092-7320, plus all
    # 21 R21f-2 added for src/tychoc.c:7327-7929, plus all 11 R21f-3 added for
    # the parallel-for block and the 9k/12k tail, plus all 27 R21f-4 added for the
    # 8k match/destructuring/select block and the three infinite-type arms,
    # plus 23 of the 24 R21f-5 added for the 6k expression block
    # (src/tychoc.c:6334-6907), plus all 15 R21f-7 added for the 6k struct and
    # enum CONSTRUCTION block (src/tychoc.c:6969-7081), plus all 6 flat ones
    # R21f-8 added for the 6k field and variant ACCESS block
    # (src/tychoc.c:6415-6728; its other 3 are package fixtures and are not in
    # this count, which walks the flat lane only), plus all 6 R21f-9 added for
    # the 2k soa element bans, the fn-returns-a-handle rule, the closure handle
    # capture and the two spawn rules, plus 5 of the 10 R21f-10 added for the 8k
    # value-if/match, map compound-assign, nested-pattern and place block.
    # R21f-10's other 4 carry none for the same reason as the 1 below -- each
    # says in its own header which tychoc1 rule words the refusal differently.
    # (`pass_as_value` was the fifth and carries one now: R21a-3 moved tychoc1 to
    # src/tychoc.c:6452's rule.)
    # `parfor_not_collection` carries one now: giving `ast.ForIn` the parser's
    # `parallel` flag let tychoc1 word the parallel refusal, so the two agree.
    # Its three former siblings -- `for3_two_clauses`, `match_range_no_high` and
    # `const_div_overflow` -- were closed by R21a-3 and are pinned the same way.
    # Plus all 5 flat ones R21f-11 added for the 5k const-fold and pending-decl
    # block and the two lambda ceilings; its 6th is a package fixture and is not
    # in this count, which walks the flat lane only.
    # Plus the 14 flat ones R21a added: its 16 SYNTAX and NAME wording ports to
    # compiler/ mean these fixtures' diagnostics now agree word for word, so each
    # is pinned on the WHOLE message instead of a shared prefix.
    # Plus the 18 flat ones R21c added, for the same reason: its 21 SEMANTIC
    # wording ports mean those fixtures' diagnostics now agree word for word.
    cc_exp=0
    for cc_e in $G_reject; do
        [ -e "$cc_e" ] || continue
        grep -q '^# expect: ' "$cc_e" && cc_exp=$((cc_exp + 1))
    done
    [ "$cc_show" = print ] && printf 'count %-10s %d\n' reject-expect "$cc_exp"
    # Plus the 2 `packed struct` fixtures (V2, 2026-09-04): packed_on_enum and
    # packed_heap_field, both SYNTAX and both agreeing word for word.
    # Plus the 6 `vector[N]T` fixtures (V3, 2026-09-04): three SYNTAX (the two
    # power-of-two refusals and the absent count), two SEMANTIC (the non-const
    # count and the element rule) and one for the vector/array mix -- all six
    # agreeing word for word in both compilers.
    # Plus the 4 inline-array fixtures (V3a, 2026-09-04): slice and push on a
    # [N]T and on a vector[N]T, all four SEMANTIC and word-for-word in both.
    # Plus fstring_hole_name (2026-09-04): the NAME rule inside an f-string hole.
    # Plus the 177 R16c-2 added (2026-09-04), every substring taken from
    # ./tychoc1's own diagnostic and naming the RULE -- never an instance name,
    # a line number or a path. Plus R16c-6's last 21 (2026-09-04): 15 of them
    # needed the DIAGNOSTIC reworded first, since `argument 1 of 'f' is string,
    # expected int` has no fragment that is not an instance.
    # Plus the 2 struct/enum `$Name` typaram fixtures (2026-09-05), both ported
    # into tychoc1 and word-for-word in both compilers.
    # Plus fstr_unterminated_brace (2026-09-05): the fourth malformed f-string
    # body, whose minimal witness is nine characters and was missed by a
    # length-<=6 search that filed the rule as a DEAD candidate.
    # Plus the 4 `align(N) struct` fixtures (L1, 2026-09-05): the >8 ceiling, the
    # power-of-two rule, the packed/align contradiction and align on a non-struct
    # -- all four SYNTAX and word-for-word in both compilers. Plus the 3 that
    # scripts/diag_coverage.py demanded: each of align's remaining refusals
    # (a repeated attribute, either spelling, and an empty `align()`) had no
    # fixture anywhere in the tree, which is a rule nothing could redden for.
    # Plus the 3 multi-assign fixtures (L2, 2026-09-05): the duplicate target,
    # the non-place target and the `for` clause, all three SYNTAX (the parser
    # settles them) and word-for-word in both compilers.
    # Plus the 3 swizzle fixtures (L3, 2026-09-05): the repeated component on the
    # left, the one-component swizzle and the base that is not a place -- all
    # three SYNTAX and word-for-word in both compilers.
    [ "$cc_exp" -eq 587 ] || cc_bad="$cc_bad reject-expect:$cc_exp!=587"

    cc_dirs=0
    for cc_d in $(git ls-files tests 2>/dev/null | grep '\.ty$' | sed 's|/[^/]*$||' | sort -u); do
        case "$cc_d" in
            tests|tests/reject|tests/abort|tests/diag|tests/warn) cc_dirs=$((cc_dirs + 1)); continue ;;
            tests/pkg/*|tests/reject/pkg/*|tests/warn/pkg/*) cc_dirs=$((cc_dirs + 1)); continue ;;
        esac
        cc_seen=0
        for cc_k in $CORPUS_ELSEWHERE; do
            case "$cc_d" in "$cc_k"|"$cc_k"/*) cc_seen=1 ;; esac
        done
        if [ "$cc_seen" = 1 ]; then cc_dirs=$((cc_dirs + 1)); continue; fi
        cc_bad="$cc_bad unclaimed-dir:$cc_d"
    done
    if [ "$cc_show" = print ]; then
        printf 'count %-10s %d\n' total "$cc_total"
        printf 'census: %d classes, %d files, %d directories claimed\n' \
            "$cc_classes" "$cc_total" "$cc_dirs"
    fi
    [ -z "$cc_bad" ] || { printf 'census FAILED:%s\n' "$cc_bad"; return 1; }
    return 0
}

if [ "${1:-}" = "--count" ]; then
    corpus_census print; exit $?
fi

TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "no $TYCHOC — run 'make' first"; exit 2; }

CC="${CC:-cc}"
RECORD="${RECORD:-0}"
# ILP32 lane (make ilp32): rebuild the emitted C under `gcc -m32` and golden-
# compare, to prove Tycho `int` is width-fixed (int64) and does not truncate on a
# 32-bit-`long` data model. The 32-bit ASan runtime is not shipped under multilib,
# so that lane is skipped here; ASan coverage stays in the 64-bit `make test`.
NO_ASAN="${TYCHO_NO_ASAN:-0}"
# Windows/MSYS2: mingw gcc ships no ASan/UBSan runtime (docs/internals/windows-port.md phase 2
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

    # tychoc's own link path appends the companion shim of every corelib package
    # an import pulled in; this lane rolls its own cc line, so it has to ask.
    # `--print-shims` answers with the transitive closure, and is empty for a
    # program that imports no corelib (scripts/release.sh:90 splices it too).
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
    # version -- docs/internals/windows-port.md phase 3); a `<golden>.win` sibling records the
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
    for hi in $G_positive; do
        [ -e "$hi" ] || continue
        run_fixture "$hi"
    done
    for d in $G_pkg; do
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

    for hi in $G_positive; do
        [ -e "$hi" ] || continue
        name="$(basename "$hi" .ty)"
        gold="tests/$name.out"
        if [ "$IS_WINDOWS" = 1 ] && [ -f "tests/$name.out.win" ]; then gold="tests/$name.out.win"; fi
        queue_positive "$hi" "$name" "$gold" "$(if [ -f "tests/$name.in" ]; then echo "tests/$name.in"; else echo /dev/null; fi)"
    done
    # Package programs: each directory is one multi-file program whose entry is
    # main.ty and whose golden lives beside the package directory.
    for d in $G_pkg; do
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


for hi in $G_reject; do
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
        # OPT-IN: a fixture may pin WHY it is refused with a `# expect: <text>`
        # line, asserted as a literal substring of the diagnostic. A fixture
        # without one scores exactly as before. Substring, not an exact golden:
        # exact is what the tests/diag/ lane below already does, and a caret
        # column or line number shifts whenever a fixture gains a line -- churn
        # carrying no signal about the REASON for the refusal.
        # EVERY `# expect:` line is asserted, not just the first: a diagnostic
        # that names two locations needs two substrings to pin both.
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
# Package-level reject tests: tests/reject/pkg/<name>/ is a multi-file package
# program the compiler must REFUSE (e.g. a cross-package access to a
# package-private `_name`). Its own directory keeps the entry's package-merge
# isolated from the single-file rejects above. Same discipline as above: tychoc
# must exit nonzero AND print a diagnostic.
for d in $G_rejectpkg; do
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
for hi in $G_abort; do
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

# The intermediate .c must never destroy a file tychoc did not write. `tychoc
# foo.ty` derives foo.c, wrote it unconditionally and remove()d it on success, so
# a hand-written foo.c beside foo.ty was gone with no warning and no flags
# involved. No .ty fixture can reach this -- it is driver behaviour, and the
# subject is a file that must still EXIST afterwards.
CW="$TMP/clobber"; rm -rf "$CW"; mkdir -p "$CW"
printf 'fn main():\n    println("hi")\n' > "$CW/foo.ty"
: > "$CW/canary"; echo '/* USER FILE */' > "$CW/foo.c"
for _cwname in bare out shim; do
    case "$_cwname" in
        bare) _cw="" ;;
        out)  _cw="-o $CW/foo" ;;
        shim) _cw="--shim $CW/foo.c -o $CW/foo" ;;
    esac
    name="clobber_refused_$_cwname"
    # shellcheck disable=SC2086
    if "$TYCHOC" "$CW/foo.ty" $_cw >"$TMP/cw.log" 2>&1; then
        note "$name" "tychoc SUCCEEDED with a foreign foo.c in the way"; fail=$((fail + 1)); fails="$fails $name"
    elif [ ! -f "$CW/foo.c" ] || ! grep -q 'USER FILE' "$CW/foo.c"; then
        note "$name" "the user's foo.c was destroyed"; fail=$((fail + 1)); fails="$fails $name"
    else
        echo "ok    $name"; pass=$((pass + 1))
    fi
done
# The other half: a .c WE left behind (cc failed last run, kept as evidence) and
# --emit-c's named output must still be writable, or the guard has broken both
# the rebuild path and the documented debugging path.
rm -f "$CW/foo.c"
if ! "$TYCHOC" "$CW/foo.ty" --emit-c -o "$CW/foo" >"$TMP/cw.log" 2>&1 || [ ! -f "$CW/foo.c" ]; then
    note "clobber_emit_c" "--emit-c no longer writes its .c"; fail=$((fail + 1)); fails="$fails clobber_emit_c"
elif ! "$TYCHOC" "$CW/foo.ty" -o "$CW/foo" >"$TMP/cw.log" 2>&1; then
    note "clobber_regen" "a tycho-written .c was refused instead of overwritten"
    sed 's/^/      /' "$TMP/cw.log"; fail=$((fail + 1)); fails="$fails clobber_regen"
else
    echo "ok    clobber_emit_c"; pass=$((pass + 1))
fi

# --bundle had NO lane at all, which is how a wrong-file diagnostic shipped in it:
# bundle_pkg lexed each package file without pointing g_srcname at it, so a lexer
# error in ANY sibling was reported against the entry file, at line 1 of a file that
# was fine. The plain build was always right, which is what makes the pair below the
# test -- the two modes must name the SAME file.
BW="$TMP/bundle"; rm -rf "$BW"; mkdir -p "$BW/app"
printf 'package app\nfn main():\n    println("ok")\n' > "$BW/app/main.ty"
if ! "$TYCHOC" "$BW/app/main.ty" --bundle > "$BW/out.ty" 2>"$TMP/bw.log" || [ ! -s "$BW/out.ty" ]; then
    note "bundle_clean" "--bundle failed on a well-formed package"
    sed 's/^/      /' "$TMP/bw.log"; fail=$((fail + 1)); fails="$fails bundle_clean"
elif ! "$TYCHOC" "$BW/out.ty" --emit-c -o "$BW/rt" >"$TMP/bw.log" 2>&1; then
    note "bundle_clean" "the bundled source does not compile"
    sed 's/^/      /' "$TMP/bw.log"; fail=$((fail + 1)); fails="$fails bundle_clean"
else
    echo "ok    bundle_clean"; pass=$((pass + 1))
fi
printf 'this is not tycho {{{ garbage\n' > "$BW/app/junk.ty"
"$TYCHOC" "$BW/app/main.ty" --bundle >/dev/null 2>"$TMP/bw.bundle"
"$TYCHOC" "$BW/app/main.ty" --emit-c -o "$BW/plain" >/dev/null 2>"$TMP/bw.plain"
_bf=$(sed -n '1s/:.*//p' "$TMP/bw.bundle"); _pf=$(sed -n '1s/:.*//p' "$TMP/bw.plain")
if [ -z "$_bf" ] || [ -z "$_pf" ]; then
    note "bundle_blames_right_file" "one of the two modes accepted a broken sibling"
    fail=$((fail + 1)); fails="$fails bundle_blames_right_file"
elif [ "$_bf" != "$_pf" ]; then
    note "bundle_blames_right_file" "--bundle blamed $_bf, the plain build blamed $_pf"
    fail=$((fail + 1)); fails="$fails bundle_blames_right_file"
elif ! grep -q 'junk\.ty' "$TMP/bw.bundle"; then
    note "bundle_blames_right_file" "neither mode named junk.ty, the file that is actually broken"
    fail=$((fail + 1)); fails="$fails bundle_blames_right_file"
else
    echo "ok    bundle_blames_right_file"; pass=$((pass + 1))
fi

# `--` ends the flags, so a source whose name starts with '-' can be compiled at
# all. Without it the name is read as a flag and only `./-name.ty` works. The
# second leg is what keeps the first honest: WITHOUT `--` the name must still be
# refused, or the fix would have quietly made every stray argument a source.
DW="$TMP/dashname"; rm -rf "$DW"; mkdir -p "$DW"
printf 'fn main():\n    println("dash ok")\n' > "$DW/-weird.ty"
( cd "$DW" && "$OLDPWD/$TYCHOC" -o "$DW/dw" -- -weird.ty ) >"$TMP/dw.log" 2>&1
if [ ! -x "$DW/dw" ] || [ "$("$DW/dw" 2>/dev/null)" != "dash ok" ]; then
    note "dashname_after_ddash" "a '-' source did not build through \`--\`"
    sed 's/^/      /' "$TMP/dw.log"; fail=$((fail + 1)); fails="$fails dashname_after_ddash"
elif ( cd "$DW" && "$OLDPWD/$TYCHOC" -weird.ty ) >/dev/null 2>&1; then
    note "dashname_after_ddash" "a '-' source was accepted WITHOUT \`--\` -- flags are no longer flags"
    fail=$((fail + 1)); fails="$fails dashname_after_ddash"
else
    echo "ok    dashname_after_ddash"; pass=$((pass + 1))
fi

for hi in $G_diag; do
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

for hi in $G_warn; do
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

# Package-mode warning goldens: tests/warn/pkg/<name>/ is a VALID package program
# whose WHOLE compiler stderr is locked as tests/warn/pkg/<name>.err. Its own
# directory for the reason tests/reject/pkg/ has one -- a `package` header
# compiles the whole directory, so a flat sibling would drag the others in.
# Deliberately no "a warning must be present" rule, unlike the flat lane above:
# what these fixtures assert is WHICH warnings a build prints, and "none from the
# corelib package I merely imported" is the assertion (commit 8f4367d). An empty
# golden is a legal answer here; in the flat lane it is a failure.
for d in $G_warnpkg; do
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

# Driver behaviour, like the clobber cases above: the corpus this runner walks
# must still be the corpus on disk.
if corpus_census quiet; then
    echo "ok    corpus_census"; pass=$((pass + 1))
else
    note "corpus_census" "a tests/ directory is not claimed by any class glob"
    fail=$((fail + 1)); fails="$fails corpus_census"
fi

# The pass count against the corpus THIS run just walked. `passed: 0 failed: 0`
# is also a zero-failure run, and nothing above reddens when a whole class stops
# being globbed. Moved here from scripts/tychoc1_check.sh 2026-09-05: that leg
# scored the same run this file does, because TYCHOC defaults to ./tychoc1 here.
# 8 = the standalone cases this runner adds on top of the corpus --
# clobber_refused_{bare,out,shim}, clobber_emit_c, bundle_clean,
# bundle_blames_right_file, dashname_after_ddash, corpus_census. It moves when a
# CASE is added, never when a fixture is.
STANDALONE=8
cc_want=$((cc_total + STANDALONE))
if [ "$fail" -eq 0 ] && [ "$pass" -ne "$cc_want" ]; then
    note "pass_count" "passed $pass, but the corpus is $cc_total + $STANDALONE standalone = $cc_want"
    fail=$((fail + 1)); fails="$fails pass_count"
fi

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
