#!/bin/sh
# Gate for tycho-sheet, the spreadsheet engine in tools/tycho-sheet/ -- cell/
# (values, the float renderer, the formula parser and the evaluator) and sheet/
# (the grid, the dependency graph and the recalculation order).
#
# Re-record the golden with:  RECORD=1 sh tools/tycho-sheet/run.sh
#
# WHY THIS IS NOT A GOLDEN LANE WITH EXTRA STEPS. The subject is FLOAT TEXT, and
# a recorded transcript is the one instrument that cannot see a float bug: if
# render() drops a digit, the golden recorded from that same build agrees with
# it and `cmp` is green by construction. So the round trip is asserted HERE, by
# a probe that parses every rendered string back and compares DOUBLES, over a
# corpus the runner generates -- a place RECORD=1 cannot reach. The golden is
# leg [1] of eight and the weakest of them.
#
# WHAT IT ASSERTS
#   [1] THE DEMO TRANSCRIPT, twice. `--script=demo.sheet` is run twice; both runs
#       must be cmp-identical to each other and the first to the golden. The
#       program takes no paths, reads no clock and spawns nothing, so a
#       difference between two runs is uninitialised state or map iteration
#       order leaking into output, not scheduling.
#   [2] THE FLOAT ROUND TRIP, over a corpus this runner generates and against
#       literals. 98410 values -- every i/j for i,j in 1..120, 40000
#       pseudo-random mantissas swept across the exponent range with both signs,
#       and 4000 pushed out to 10^+-300 -- are rendered, parsed back with
#       strtod, and compared as doubles. Exactly ONE may fail, and it must be
#       the min subnormal: strings.parse_float refuses every subnormal as
#       Underflow, so no text round-trips one. That count is a literal below,
#       so a renderer that regressed to 15 digits moves it by thousands.
#   [3] `str(float)` ROUND-TRIPS, asserted directly. This leg used to assert the
#       opposite -- that `str(0.1 + 0.2)` was the lossy "0.3" -- because that was
#       the whole reason cell/ carries a renderer. It went red on 2026-08-12 when
#       the runtime was fixed (FRICTION #22), which is what the leg was there to
#       detect. It is INVERTED, not deleted: it now catches a revert.
#   [4] THE PARSE / VALUE SPLIT, which is the design claim of cell/cell.ty.
#       `=1/0` is ACCEPTED and shows #DIV/0!; `=A1+`, `=(A1`, `=1 2`, `=@A1` and
#       `=SUM(A1:A9` are REFUSED with their own messages and store nothing --
#       E1 still holds the 7 it held before. A collapse of the two in either
#       direction moves one of these lines.
#   [5] EVERY CellErr AND ParseErr VARIANT IS REACHED. Both variant lists are
#       READ OUT OF THE SOURCE and each must appear in the transcript, so a
#       variant added tomorrow arrives with a test instead of ungated.
#   [6] THE CYCLE IS NAMED, not merely detected. `#CYCLE! F1 -> F2 -> F3 -> F1`
#       and the self-reference `G1 -> G1` are literals here. A cycle detector
#       that hung or overflowed would never reach these; one that printed a bare
#       "#CYCLE!" would fail them.
#   [7] DEPTH FAILS CLOSED OR EVALUATES -- never a silent crash. A 10000-deep
#       chain is in the golden and must come out as exactly 10000. A 100000-deep
#       one is run here (too big for a golden) and must also be exact. Past the
#       edges, four different limits must each exit 0 with a NAMED error and no
#       crash: a reference past the last row, a reference with too many digits,
#       parentheses past MAX_DEPTH, and an operator spine past EVAL_DEPTH.
#   [8] BAD INVOCATION EXITS NON-ZERO with an empty stdout.
#
# WHAT IT DELIBERATELY DOES NOT ASSERT
#   Timing. Nothing here is a benchmark; the 100000-chain is bounded by $TO
#   only so a hang reports instead of sitting there.
#   Which of several equally short decimals render() picks. Two 17-digit strings
#   can name the same double, and the contract is "reads back equal", not "is
#   the canonical shortest". [2] tests the contract that matters.
#
# NO HOST DETAIL REACHES THE GOLDEN -- the program prints no paths. Every run is
# bounded by $TO where a timeout(1) exists.
set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "tycho-sheet: no ./tychoc -- run 'make' first"; exit 2; }
TYCHOC="$PWD/tychoc"          # absolute: the probe in [2] is built after a cd
export TYCHO_CORELIB="$PWD/corelib"
RECORD="${RECORD:-0}"
golden="$PWD/tools/tycho-sheet/expected.out"
src="$PWD/tools/tycho-sheet"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT INT TERM
fail=0
bad() { echo "FAIL: $*"; fail=1; }

if command -v timeout >/dev/null 2>&1; then TO="timeout 300"
elif command -v gtimeout >/dev/null 2>&1; then TO="gtimeout 300"
else TO=""; fi

SH="$T/tycho-sheet"
if ! "$TYCHOC" "$src/main.ty" -o "$SH" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/build.log"
    echo "tycho-sheet: FAIL"; exit 1
fi

out="$T/all.out"
: > "$out"

shrun() {
    _lbl=$1; _s=$2; _f=$3
    $TO "$SH" "--script=$_s" > "$_f" 2> "$T/e.err"
    _rc=$?
    [ "$_rc" -eq 0 ] || { bad "$_lbl: exited $_rc, expected 0"; sed 's/^/      /' "$T/e.err"; }
    [ -s "$T/e.err" ] && { bad "$_lbl: wrote to stderr"; sed 's/^/      /' "$T/e.err"; }
    return 0
}

# ---------------------------------------------------------------------------
# [1] the demo, twice
# ---------------------------------------------------------------------------
[ -f "$src/demo.sheet" ] || bad "demo: $src/demo.sheet is gone -- legs [1], [4], [5], [6] and [7] assert NOTHING"
shrun "demo run 1" "$src/demo.sheet" "$T/d.1"
shrun "demo run 2" "$src/demo.sheet" "$T/d.2"
cmp -s "$T/d.1" "$T/d.2" || {
    bad "the demo transcript is not deterministic (run 1 vs run 2)"
    diff "$T/d.1" "$T/d.2" | sed 's/^/      /'
}
printf '=== demo\n' >> "$out"
cat "$T/d.1" >> "$out"

ln_() {
    grep -qxF "$1" "$T/d.1" || bad "expected line missing from the transcript -- '$1'"
}

# ---------------------------------------------------------------------------
# [2] the float round trip, over a generated corpus
#
# Asserted HERE and not left to the golden, for the reason in the header: a
# lossy renderer and a golden recorded from it agree with each other.
# cell/ is COPIED into the temp dir so nothing is written into the repo, and a
# renamed package reddens here.
# ---------------------------------------------------------------------------
P="$T/pkg"; mkdir -p "$P"
[ -d "$src/cell" ] || bad "probe: $src/cell is gone -- leg [2] asserts NOTHING"
cp -R "$src/cell" "$P/" 2>/dev/null
cat > "$P/probe.ty" <<'EOF'
package main

import "core:strings"
import "cell"

# Render, parse back, compare DOUBLES. Anything that fails is printed with the
# value's str() form so a failure is diagnosable from the log alone.
fn chk(v: float, bad: inout int, n: inout int):
    n += 1
    s := cell.render(v)
    ok := false
    match strings.parse_float(s):
        Ok(b):
            if b == v and cell.is_neg_zero(b) == cell.is_neg_zero(v):
                ok = true
        Err(e): ok = false
    if not ok:
        bad += 1
        if bad < 8:
            println("  FAIL " + strings.pad_right(s, 26, " ") + " str()=" + str(v))

fn main():
    bad := 0
    n := 0
    # Named values, including the ones the header quotes.
    chk(0.1 + 0.2, &bad, &n)
    chk(1.0 / 3.0, &bad, &n)
    chk(0.0, &bad, &n)
    chk(-0.0, &bad, &n)
    chk(1.0, &bad, &n)
    chk(1e300, &bad, &n)
    chk(1e-300, &bad, &n)
    chk(1.7976931348623157e308, &bad, &n)
    chk(2.2250738585072014e-308, &bad, &n)
    chk(9007199254740992.0, &bad, &n)
    # Ratios: where the awkward repeating expansions live.
    for i := 1; i <= 120; i += 1:
        for j := 1; j <= 120; j += 1:
            chk(to_float(i) / to_float(j), &bad, &n)
    # Wide magnitudes, both signs.
    seed := 12345
    for k := 0; k < 40000; k += 1:
        seed = (seed * 1103515245 + 12345) % 2147483648
        m := seed
        seed = (seed * 1103515245 + 12345) % 2147483648
        m = m * 4194304 + (seed % 4194304)
        v := to_float(m)
        seed = (seed * 1103515245 + 12345) % 2147483648
        e := seed % 61 - 30
        for q := 0; q < e; q += 1:
            v *= 10.0
        for q := 0; q > e; q -= 1:
            v /= 10.0
        chk(v, &bad, &n)
        chk(-v, &bad, &n)
    # The extremes.
    seed2 := 999
    for k := 0; k < 4000; k += 1:
        seed2 = (seed2 * 1103515245 + 12345) % 2147483648
        v := to_float(seed2 % 1000000 + 1)
        e := seed2 % 600 - 300
        for q := 0; q < e; q += 1:
            v *= 10.0
        for q := 0; q > e; q -= 1:
            v /= 10.0
        chk(v, &bad, &n)
    # The one value that CANNOT round-trip: strings.parse_float refuses every
    # subnormal as Underflow, so there is no text for it. render() says #NUM!.
    sub := 5e-324
    println("subnormal render " + cell.render(sub))
    println("checked " + str(n) + " values, " + str(bad) + " failed to round-trip")
EOF
if ! "$TYCHOC" "$P/probe.ty" -o "$T/probe" >"$T/probe.log" 2>&1; then
    bad "probe: tychoc could not build the round-trip probe"
    sed 's/^/      /' "$T/probe.log" | head -8
else
    $TO "$T/probe" > "$T/probe.out" 2>&1 || bad "probe: the round-trip probe did not exit 0"
    grep -qxF "checked 98410 values, 0 failed to round-trip" "$T/probe.out" || {
        bad "the float round trip is not exact over the corpus"
        sed 's/^/      /' "$T/probe.out" | head -12
    }
    grep -qxF "subnormal render #NUM!" "$T/probe.out" || {
        bad "5e-324 no longer renders #NUM! -- either parse_float learned subnormals (good: update this leg) or render() is claiming text that does not read back"
        sed 's/^/      /' "$T/probe.out" | head -4
    }
    printf '=== float round trip over a generated corpus\n' >> "$out"
    grep -e '^checked ' -e '^subnormal ' "$T/probe.out" >> "$out"
fi

# ---------------------------------------------------------------------------
# [3] str(float) round-trips -- INVERTED 2026-08-12, and that is the point
# ---------------------------------------------------------------------------
L="$T/lossypkg"; mkdir -p "$L"
cat > "$L/lossy.ty" <<'EOF'
package main

import "core:strings"

# This probe used to assert the OPPOSITE: that str(0.1+0.2) was the lossy "0.3",
# which was the premise cell/dtoa.ty existed to work around. That premise was
# the bug report, and the runtime was fixed on 2026-08-12 (FRICTION #22) --
# str now emits the shortest decimal that reads back unchanged. So the same
# probe stays, pointing the other way: it now fails if the fix is ever reverted.
fn main():
    v := 0.1 + 0.2
    s := str(v)
    verdict := "str is LOSSY"
    match strings.parse_float(s):
        Ok(b):
            if b == v:
                verdict = "str round-trips"
        Err(e): verdict = "str is UNREADABLE"
    println("str(0.1+0.2)=" + s + " " + verdict)
EOF
if ! "$TYCHOC" "$L/lossy.ty" -o "$T/lossy" >"$T/lossy.log" 2>&1; then
    bad "lossy: tychoc could not build the str() probe"
else
    $TO "$T/lossy" > "$T/lossy.out" 2>&1
    grep -qxF "str(0.1+0.2)=0.30000000000000004 str round-trips" "$T/lossy.out" || {
        bad "str(0.1+0.2) does not round-trip -- runtime/tycho_rt.c@tycho_float_to_str regressed to a fixed precision"
        sed 's/^/      /' "$T/lossy.out"
    }
    printf '=== str(float) round-trips\n' >> "$out"
    cat "$T/lossy.out" >> "$out"
fi

# ---------------------------------------------------------------------------
# [4] the parse / value split
# ---------------------------------------------------------------------------
# ACCEPTED, and the failure is a VALUE that travels downstream:
ln_ '  D2      =C1/D1                    #DIV/0! division by zero'
ln_ '  D3      =D2+1                     #DIV/0! division by zero'
ln_ '  D4      =SUM(D1:D3)               #DIV/0! division by zero'
# ...and IF does not evaluate the branch it did not take:
ln_ '  D5      =IF(D1=0,0,C1/D1)         0'
# REFUSED, each with its own message, and nothing stored:
ln_ '  ERR E1: expected a value but found end of formula at 3'
ln_ "  ERR E1: unclosed '(' opened at 0"
ln_ "  ERR E1: trailing input '2' at 2"
ln_ "  ERR E1: unexpected character '@' at 0"
ln_ "  ERR E1: expected ',' or ')' but found end of formula at 9"
ln_ '  E1 = 7'

# ---------------------------------------------------------------------------
# [5] every error variant is reached
#
# The lists are READ out of the source, so this cannot go stale by omission.
# ---------------------------------------------------------------------------
enum_variants() {
    awk -v want="$2" '
        $0 == "enum " want ":" { on = 1; next }
        on && /^[^ \t]/ { on = 0 }
        on && $1 ~ /^#/ { next }
        on && NF { v = $1; sub(/\(.*/, "", v); print v }
    ' "$1"
}
# Each variant maps to a string the transcript must contain.
variant_probe() {
    case "$1" in
        DivZero)  echo '#DIV/0!' ;;
        BadRef)   echo '#REF!' ;;
        Cycle)    echo '#CYCLE!' ;;
        NotNum)   echo '#VALUE!' ;;
        NoText)   echo '#NUM!' ;;
        BadName)  echo '#NAME?' ;;
        NoInput)      echo 'empty formula' ;;
        BadChar)      echo 'unexpected character' ;;
        BadNumber)    echo 'malformed number' ;;
        Expected)     echo 'expected ' ;;
        Unclosed)     echo "unclosed '('" ;;
        Trailing)     echo 'trailing input' ;;
        TooDeep)      echo 'nested deeper than' ;;
        HugeRef)      echo 'outside the grid' ;;
        *) echo "" ;;
    esac
}
# The demo does not exercise every one; the ones it cannot reach are provoked
# here, and the two transcripts are searched together.
cat > "$T/variants.sheet" <<'EOF'
set A1 =
set A2 =1e999999+1
set A3 =A1048577+1
set A4 =((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((1))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))
set B1 1
set B2 2
set B3 3
set C1 =B1:B3+1
set C2 =1/0
set C3 =1e-300/1e10
recalc
dump
EOF
shrun "variants" "$T/variants.sheet" "$T/v.out"
cat "$T/d.1" "$T/v.out" > "$T/searched"
nvar=0
for src_file in "$src/cell/cell.ty:CellErr" "$src/cell/expr.ty:ParseErr"; do
    f=${src_file%:*}; en=${src_file#*:}
    got=0
    for v in $(enum_variants "$f" "$en"); do
        got=$((got + 1)); nvar=$((nvar + 1))
        probe=$(variant_probe "$v")
        if [ -z "$probe" ]; then
            bad "$en variant $v has no probe in this runner -- it is UNGATED"
        elif ! grep -qF "$probe" "$T/searched"; then
            bad "$en variant $v is never reached by any script here -- it is UNGATED"
        fi
    done
    [ "$got" -ge 6 ] || bad "found only $got variant(s) of $en -- the scan is broken and [5] asserts nothing"
done
[ "$nvar" -ge 14 ] || bad "scanned only $nvar error variants in total -- expected at least 14"
printf '=== error variants reached: %s\n' "$nvar" >> "$out"

# ---------------------------------------------------------------------------
# [6] the cycle is NAMED
# ---------------------------------------------------------------------------
ln_ '  F1      =F2+1                     #CYCLE! F1 -> F2 -> F3 -> F1'
ln_ '  F2      =F3+1                     #CYCLE! F2 -> F3 -> F1 -> F2'
ln_ '  F3      =F1+1                     #CYCLE! F3 -> F1 -> F2 -> F3'
# F4 is not IN the cycle, it reads it -- and it must name the cycle it reaches.
ln_ '  F4      =F1+100                   #CYCLE! F1 -> F2 -> F3 -> F1'
ln_ '  G1 = #CYCLE! G1 -> G1'
ln_ '  recalculated; 4 cell(s) on or below a cycle'

# ---------------------------------------------------------------------------
# [7] depth: evaluates, or fails closed by name
# ---------------------------------------------------------------------------
ln_ '  built a chain 10000 deep: A1 = A2 + 1 ... A10000 = 1'
ln_ '  A1 = 10000'
ln_ '  A9999 = 2'

# A chain an order of magnitude deeper, run here rather than recorded: the
# claim is that iterative recalculation has no depth limit but memory, and a
# golden of 100000 lines would assert it no better.
printf 'chain 100000\nrecalc\nget A1\nget A50000\n' > "$T/deep.sheet"
shrun "100000-deep chain" "$T/deep.sheet" "$T/deep.out"
grep -qxF '  A1 = 100000' "$T/deep.out" || {
    bad "a 100000-deep chain did not evaluate to exactly 100000 -- recursion is back, or the order is wrong"
    sed 's/^/      /' "$T/deep.out" | head -6
}
grep -qxF '  A50000 = 50001' "$T/deep.out" || bad "the middle of the 100000-deep chain is wrong"

# Past every edge: a named error, exit 0, no crash. Each of these four hits a
# DIFFERENT limit, and a crash in any of them is the failure this leg exists for.
{
    echo 'set B1 =A1048577+1'
    echo 'set B2 =A99999999+1'
    printf 'set B3 ='; printf '(%.0s' $(seq 1 200); printf '1'; printf ')%.0s' $(seq 1 200); echo
    printf 'set B4 ='; printf '1+%.0s' $(seq 1 5000); echo '1'
    echo 'recalc'
    echo 'get B4'
} > "$T/edge.sheet"
shrun "edge limits" "$T/edge.sheet" "$T/edge.out"
grep -qF "reference 'A1048577' is outside the grid" "$T/edge.out" || bad "a reference past the last row was not refused by name"
grep -qF "expected a cell reference or a function call but found 'A99999999'" "$T/edge.out" || bad "an over-long reference was not refused by name"
grep -qF "formula nested deeper than 64" "$T/edge.out" || bad "200 nested parens did not hit the parser's depth limit by name"
grep -qF "expression nested deeper than 4096 to evaluate" "$T/edge.out" || bad "a 5000-term operator spine did not hit the evaluator's depth limit by name"
printf '=== depth limits\n' >> "$out"
grep -e 'outside the grid' -e 'cell reference or a function' -e 'nested deeper than' "$T/edge.out" >> "$out"

# ---------------------------------------------------------------------------
# [8] a bad invocation fails, loudly
# ---------------------------------------------------------------------------
badrun() {
    $TO "$SH" $1 > "$T/b.out" 2> "$T/b.err"
    _rc=$?
    [ "$_rc" -eq 0 ] && bad "'$1': EXITED 0 -- a bad invocation must fail"
    [ -s "$T/b.out" ] && bad "'$1': wrote to STDOUT"
    [ -s "$T/b.err" ] || bad "'$1': failed silently, with nothing on stderr"
}
badrun ""
badrun "--nonesuch"
badrun "--script=/nonexistent/path.sheet"

# ---------------------------------------------------------------------------
# the golden
# ---------------------------------------------------------------------------
if [ "$RECORD" = 1 ]; then
    if [ "$fail" -ne 0 ]; then echo "FAIL: refusing to RECORD from a failed run"; exit 1; fi
    cp "$out" "$golden"; echo "rec  tycho-sheet"
fi
if [ ! -f "$golden" ]; then
    bad "no golden -- run RECORD=1 sh tools/tycho-sheet/run.sh"
elif ! cmp -s "$out" "$golden"; then
    bad "transcript != golden"; diff "$golden" "$out" | sed 's/^/      /'
fi

if [ "$fail" -eq 0 ]; then
    echo "tycho-sheet: green (demo transcript byte-identical over 2 runs and equal to the golden; 98410 rendered floats all read back bit-equal and the one subnormal that cannot is refused as #NUM!; str(0.1+0.2) round-trips through the fixed builtin; =1/0 is a #DIV/0! VALUE that reaches its readers while five malformed formulas are refused and store nothing; $nvar error variants all reached; a cycle is named F1 -> F2 -> F3 -> F1 and a self-reference G1 -> G1; a 10000- and a 100000-deep chain evaluate exactly and four different depth limits past them fail closed by name; three bad invocations exit non-zero)"
else
    echo "tycho-sheet: FAIL"; exit 1
fi
