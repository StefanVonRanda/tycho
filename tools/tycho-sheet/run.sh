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
# leg [1] of nine and the weakest of them.
#
# WHAT IT ASSERTS
#   [1] THE DEMO TRANSCRIPT, twice. `--script=demo.sheet` is run twice; both runs
#       must be cmp-identical to each other and the first to the golden. The
#       program takes no paths, reads no clock and spawns nothing, so a
#       difference between two runs is uninitialised state or map iteration
#       order leaking into output, not scheduling.
#   [2] THE FLOAT ROUND TRIP, over a corpus this runner generates and against
#       literals. 98411 values -- every i/j for i,j in 1..120, 40000
#       pseudo-random mantissas swept across the exponent range with both signs,
#       and 4000 pushed out to 10^+-300 -- are rendered, parsed back with
#       strtod, and compared as doubles. NONE may fail. Until 2026-08-12 one
#       was allowed to -- the min subnormal, because strings.parse_float refused
#       every subnormal as Underflow and no text round-tripped one. corelib was
#       fixed (FRICTION #23) and the exception went with it. That count is a
#       literal below, so a renderer that regressed to 15 digits moves it by
#       thousands.
#       THE FOUR VALUES THAT MOTIVATED cell/dtoa.ty ARE ALSO ASSERTED ONE BY ONE
#       -- 0.1+0.2, 2^53, DBL_MAX and the min subnormal. The corpus count proves
#       98411 values round-trip but not WHICH: a generator that stopped emitting
#       the extremes would keep the count green. What is asserted per value is
#       the VERDICT ("reads back equal"), not the digits, because which of
#       several equally valid decimals render() picks is deliberately not this
#       lane's business (see below). The digits go into the golden, where a
#       change in them shows in a diff without being a hard failure. Two
#       exceptions are literals: 2^53, whose shortest form is unique because
#       every integer below it is exact, and the min subnormal, whose "5e-324"
#       likewise names exactly one double.
#       THE #NUM! ARM IS COUNTED, NOT INFERRED. render() falls back to "#NUM!"
#       when nothing it can print reads back as the value held. Since bc51c069
#       taught corelib subnormals, nothing reaches it -- so the probe counts how
#       many of the 98411 landed there and this runner asserts ZERO. Its sibling
#       is CellErr.NoText, which nothing constructs at all; [5] asserts that
#       statically, by counting the token's occurrences in the source.
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
#       variant added tomorrow arrives with a test instead of ungated. The one
#       variant no input can reach -- CellErr.NoText -- is declared unreachable
#       and the check INVERTS for it, so a script that did reach it fails too.
#       That declaration is backed by a static count: `NoText` occurs exactly
#       three times in cell/ and sheet/ -- the enum line and the two match arms
#       in err_code and err_detail -- and nowhere constructs one. A fourth
#       occurrence means something now can, and the arm needs a leg in [9].
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
#   [9] EVERY ERROR VARIANT EXITS NON-ZERO WITH ITS OWN WHOLE MESSAGE and an
#       empty stdout. [5] proves each variant is REACHED; a substring in a
#       transcript is not the same claim as a caller dying by it, and the
#       --script driver deliberately does not die -- it prints `ERR ...` and
#       keeps going, because a script is a test and an error is an observation.
#       So this needs a different caller, and it is the same shape tycho-ed uses:
#       cell/ and sheet/ are COPIED into the temp dir and a probe whose
#       `main() -> Result(void, string)` returns Err(<the message>) is built
#       against the copies. Nothing is written into the repo, and a renamed
#       package reddens here. Each arm calls the API that OWNS the variant --
#       cell.parse, cell.eval, cell.to_num, sheet.recalc -- rather than
#       constructing the enum, which would assert the err_str function and
#       nothing else. Both variant lists are read out of the enums again as the
#       coverage floor, so a variant added tomorrow is UNGATED here too and says
#       so. Cycle gets two arms, the three-cell ring and the self-reference, and
#       every arm is bounded by $TO: a cycle detector that recursed forever is
#       exactly what this leg exists to catch, and an unbounded gate would sit
#       there until CI's own timeout killed it with no verdict.
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
fn chk(v: float, bad: inout int, n: inout int, nnum: inout int):
    n += 1
    s := cell.render(v)
    # The fail-closed arm, counted rather than inferred. render() prints this
    # when no decimal it can produce reads back as the value held; since corelib
    # learned subnormals nothing should land here, and the runner asserts zero.
    if s == "#NUM!":
        nnum += 1
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

# The values cell/dtoa.ty was written for, each reported on its own. The corpus
# count says 98411 values round-trip; it does not say these four were among
# them. Two lines per value: the VERDICT, which the runner asserts as a literal,
# and the DIGITS, which only go to the golden -- which of several equally valid
# decimals render() picks is not this lane's contract, and pinning it here would
# redden for an improvement to the renderer.
fn show(label: string, v: float):
    s := cell.render(v)
    verdict := "UNREADABLE"
    match strings.parse_float(s):
        Ok(b):
            if b == v and cell.is_neg_zero(b) == cell.is_neg_zero(v):
                verdict = "reads back equal"
            else:
                verdict = "READS BACK A DIFFERENT DOUBLE"
        Err(e): verdict = "UNREADABLE"
    println("rt " + strings.pad_right(label, 10, " ") + verdict)
    println("render " + strings.pad_right(label, 10, " ") + s)

fn main():
    bad := 0
    n := 0
    nnum := 0
    # Named values, including the ones the header quotes.
    chk(0.1 + 0.2, &bad, &n, &nnum)
    chk(1.0 / 3.0, &bad, &n, &nnum)
    chk(0.0, &bad, &n, &nnum)
    chk(-0.0, &bad, &n, &nnum)
    chk(1.0, &bad, &n, &nnum)
    chk(1e300, &bad, &n, &nnum)
    chk(1e-300, &bad, &n, &nnum)
    chk(1.7976931348623157e308, &bad, &n, &nnum)
    chk(2.2250738585072014e-308, &bad, &n, &nnum)
    chk(9007199254740992.0, &bad, &n, &nnum)
    # Ratios: where the awkward repeating expansions live.
    for i := 1; i <= 120; i += 1:
        for j := 1; j <= 120; j += 1:
            chk(to_float(i) / to_float(j), &bad, &n, &nnum)
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
        chk(v, &bad, &n, &nnum)
        chk(-v, &bad, &n, &nnum)
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
        chk(v, &bad, &n, &nnum)
    # The min subnormal. This used to be the ONE value in the corpus that could
    # not round-trip, and not because render() failed: strings.parse_float
    # refused every subnormal as Underflow, so no text for it existed and
    # render() said #NUM!. corelib was fixed on 2026-08-12 (FRICTION #23), so it
    # is now an ordinary value and goes through chk() like every other. Its
    # rendering is still printed, because it is the one the header quotes.
    sub := 5e-324
    chk(sub, &bad, &n, &nnum)
    println("subnormal render " + cell.render(sub))
    # The four the header names, one line of verdict each.
    show("0.1+0.2", 0.1 + 0.2)
    show("2^53", 9007199254740992.0)
    show("DBL_MAX", 1.7976931348623157e308)
    show("subnormal", sub)
    println("checked " + str(n) + " values, " + str(bad) + " failed to round-trip")
    println("rendered #NUM! for " + str(nnum) + " of them")
EOF
if ! "$TYCHOC" "$P/probe.ty" -o "$T/probe" >"$T/probe.log" 2>&1; then
    bad "probe: tychoc could not build the round-trip probe"
    sed 's/^/      /' "$T/probe.log" | head -8
else
    $TO "$T/probe" > "$T/probe.out" 2>&1 || bad "probe: the round-trip probe did not exit 0"
    grep -qxF "checked 98411 values, 0 failed to round-trip" "$T/probe.out" || {
        bad "the float round trip is not exact over the corpus"
        sed 's/^/      /' "$T/probe.out" | head -12
    }
    grep -qxF "subnormal render 5e-324" "$T/probe.out" || {
        bad "5e-324 does not render as itself -- strings.parse_float stopped accepting subnormals, or render() regressed"
        sed 's/^/      /' "$T/probe.out" | head -4
    }
    # The #NUM! arm, counted. Anything but zero means a float in the corpus has
    # no decimal that reads back as itself -- which is the honest answer, and
    # also the one bc51c069 was supposed to have made impossible.
    grep -qxF "rendered #NUM! for 0 of them" "$T/probe.out" || {
        bad "render() fell back to #NUM! for at least one corpus value -- the arm bc51c069 made unreachable is reachable again"
        grep -e '^rendered #NUM' "$T/probe.out" | sed 's/^/      /'
    }
    # The four motivating values, by verdict. Not by digits: see the header.
    rt_() {
        grep -qxF "rt $1" "$T/probe.out" || {
            bad "float: $1 -- the value cell/dtoa.ty exists for does not survive render/parse"
            grep -e '^rt ' -e '^render ' "$T/probe.out" | sed 's/^/      /'
        }
    }
    rt_ '0.1+0.2   reads back equal'
    rt_ '2^53      reads back equal'
    rt_ 'DBL_MAX   reads back equal'
    rt_ 'subnormal reads back equal'
    # Two whose shortest decimal is unique, so the digits ARE the contract:
    # every integer below 2^53 is exact, and 5e-324 names exactly one double.
    grep -qxF 'render 2^53      9007199254740992' "$T/probe.out" || {
        bad "2^53 does not render as the exact integer 9007199254740992"
        grep -e '^render 2\^53' "$T/probe.out" | sed 's/^/      /'
    }
    grep -qxF 'render subnormal 5e-324' "$T/probe.out" || \
        bad "the min subnormal does not render as 5e-324"
    printf '=== float round trip over a generated corpus\n' >> "$out"
    grep -e '^checked ' -e '^rendered #NUM' -e '^subnormal ' -e '^render ' "$T/probe.out" >> "$out"
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
# A variant no INPUT can reach, with the reason. This is not an exemption from
# [5] -- the loop below inverts the check for these, so a declared-unreachable
# variant that a script DOES reach is a failure too. Both directions, or the
# list rots.
variant_unreachable() {
    case "$1" in
        # render() falls back to #NUM! when no decimal reads back as the value
        # held. A subnormal used to land there -- not because render() failed
        # but because strings.parse_float refused every subnormal as Underflow,
        # so no text for one existed. corelib was fixed on 2026-08-12 (FRICTION
        # #23) and [2] above now round-trips all 98411 corpus values including
        # the min subnormal, so nothing reaches this arm. THE ARM STAYS: it is
        # the fail-closed answer to "these digits do not name this value", and
        # printing a wrong number instead is the failure cell/dtoa.ty exists to
        # prevent. If anything ever reaches it again, this leg says so.
        NoText) echo 'no float fails to round-trip since corelib learned subnormals' ;;
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
nunreach=0
for src_file in "$src/cell/cell.ty:CellErr" "$src/cell/expr.ty:ParseErr"; do
    f=${src_file%:*}; en=${src_file#*:}
    got=0
    for v in $(enum_variants "$f" "$en"); do
        got=$((got + 1)); nvar=$((nvar + 1))
        probe=$(variant_probe "$v")
        why=$(variant_unreachable "$v")
        if [ -z "$probe" ]; then
            bad "$en variant $v has no probe in this runner -- it is UNGATED"
        elif [ -n "$why" ]; then
            nunreach=$((nunreach + 1))
            grep -qF "$probe" "$T/searched" && bad "$en variant $v is declared unreachable ($why) but a script reached it -- the declaration is stale"
        elif ! grep -qF "$probe" "$T/searched"; then
            bad "$en variant $v is never reached by any script here -- it is UNGATED"
        fi
    done
    [ "$got" -ge 6 ] || bad "found only $got variant(s) of $en -- the scan is broken and [5] asserts nothing"
done
[ "$nvar" -ge 14 ] || bad "scanned only $nvar error variants in total -- expected at least 14"

# The declaration that NoText is unreachable, backed statically rather than by
# "no script happened to reach it". Nothing anywhere CONSTRUCTS one: the token
# occurs exactly three times in cell/ and sheet/ -- the enum line, and the arms
# in err_code and err_detail that name it. A fourth occurrence is a construction
# site, which makes the arm reachable and means it needs a leg in [9].
notext=$(cat "$src"/cell/*.ty "$src"/sheet/*.ty | grep -c 'NoText')
[ "$notext" -eq 3 ] || bad "NoText occurs $notext time(s) in cell/ and sheet/, expected 3 (the enum line and the two match arms) -- something now CONSTRUCTS it, so the #NUM! arm is reachable and [5] declares it unreachable wrongly"

printf '=== error variants reached: %s (%s declared unreachable)\n' "$((nvar - nunreach))" "$nunreach" >> "$out"

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
# [9] every error variant, exiting non-zero with its own whole message
#
# [5] proves each variant is REACHED somewhere in a transcript, by substring.
# That is a weaker claim than a caller dying by one with the whole message
# intact, and the --script driver cannot make the stronger one: it prints
# `ERR ...` and exits 0 by design, because a script is a test and an error is an
# observation. So this builds a different caller, the same shape tycho-ed's [4]
# uses -- `main() -> Result(void, string)` returning Err(the message), which
# puts the text on stderr and the failure in the exit status. cell/ and sheet/
# are COPIED into the temp dir; nothing is written into the repo, and a renamed
# package reddens here. Each arm calls the API that OWNS the variant rather than
# constructing the enum, which would assert err_detail and nothing else.
# ---------------------------------------------------------------------------
E="$T/errpkg"; mkdir -p "$E"
[ -d "$src/sheet" ] || bad "probe: $src/sheet is gone -- leg [9]'s cycle arms assert NOTHING"
cp -R "$src/cell" "$src/sheet" "$E/" 2>/dev/null
cat > "$E/errprobe.ty" <<'EOF'
package main

import "cell"
import "sheet"

# Parse something that must not parse, and die by the parser's own message.
fn refuse(src: string) -> Result(void, string):
    match cell.parse(src):
        Err(e): return Err(cell.parse_err_str(e))
        Ok(a): return Err("PARSED a formula that must be refused: " + src)

# Evaluate something that parses but cannot produce a value, and die by the
# value error's own message. A formula that yields a NUMBER here is the
# parse/value split collapsing in the other direction.
fn evalbad(src: string) -> Result(void, string):
    match cell.parse(src):
        Err(e): return Err("REFUSED at parse time, expected a value error: " +
                           cell.parse_err_str(e))
        Ok(a):
            env := cell.env_new()
            v := cell.eval(a, &env)
            if cell.is_bad(v):
                return Err(cell.detail_value(v))
            return Err("EVALUATED to " + cell.render_value(v) +
                       ", expected a value error: " + src)

# A cycle, through the grid that owns cycle detection. `names` is the cells to
# define, `src` the formula each gets, and the first one is read back. A
# detector that recursed instead of detecting never returns from recalc, which
# is why every run of this probe is bounded by timeout(1) in the runner.
fn cycle(names: [string], srcs: [string]) -> Result(void, string):
    sh := sheet.new()
    for i := 0; i < len(names); i += 1:
        ok, row, col := cell.ref_of(names[i])
        if not ok:
            return Err("the probe's own reference is malformed: " + names[i])
        match sheet.set(&sh, row, col, srcs[i]):
            Ok(): pass
            Err(m): return Err("set " + names[i] + " was refused: " + m)
    n := sheet.recalc(&sh)
    ok0, r0, c0 := cell.ref_of(names[0])
    v := sheet.get(&sh, r0, c0)
    if cell.is_bad(v):
        return Err(cell.detail_value(v))
    return Err("recalc gave " + names[0] + " the value " + cell.render_value(v) +
               ", expected a cycle")

fn main() -> Result(void, string):
    a := args()
    if len(a) < 2:
        return Err("usage: errprobe <case>")
    c := a[1]
    # ParseErr, through cell.parse.
    if c == "NoInput":
        return refuse("")
    if c == "BadChar":
        return refuse("@A1")
    if c == "BadNumber":
        return refuse("1e999999")
    if c == "Expected":
        return refuse("1+")
    if c == "Unclosed":
        return refuse("(1")
    if c == "Trailing":
        return refuse("1 2")
    if c == "TooDeep":
        s := ""
        for i := 0; i < 200; i += 1:
            s += "("
        s += "1"
        for i := 0; i < 200; i += 1:
            s += ")"
        return refuse(s)
    if c == "HugeRef":
        return refuse("A1048577")
    # CellErr, through cell.eval and cell.to_num.
    if c == "DivZero":
        return evalbad("1/0")
    if c == "BadName":
        return evalbad("NOPE(1)")
    if c == "BadRef":
        return evalbad("A1:A9+1")
    if c == "NotNum":
        match cell.to_num(cell.text("xy")):
            Ok(x): return Err("to_num turned text into the number " + str(x))
            Err(e): return Err(cell.err_detail(e))
    # CellErr.Cycle, through the grid. Two shapes: a ring, and a cell that
    # refers to itself -- the degenerate case a depth-first walk with an
    # off-by-one "have I seen this?" test misses.
    if c == "Cycle":
        return cycle(["A1", "B1"], ["=B1", "=A1"])
    if c == "SelfCycle":
        return cycle(["G1"], ["=G1"])
    return Err("unknown case " + c)
EOF
if ! "$TYCHOC" "$E/errprobe.ty" -o "$T/errprobe" >"$T/errprobe.log" 2>&1; then
    bad "probe: tychoc could not build the error-message probe"
    sed 's/^/      /' "$T/errprobe.log" | head -8
else
    # <case> <the whole message it must die with>
    errcase() {
        _c=$1; _msg=$2
        $TO "$T/errprobe" "$_c" > "$T/x.out" 2> "$T/x.err"
        _rc=$?
        if [ "$_rc" -eq 124 ]; then
            bad "$_c: TIMED OUT -- it did not fail, it hung"
        elif [ "$_rc" -eq 0 ]; then
            bad "$_c: EXITED 0 -- the API accepted what the variant exists to refuse"
        elif ! grep -qxF "$_msg" "$T/x.err"; then
            bad "$_c: failed but not with its own whole message"
            sed 's/^/      /' "$T/x.err"
        fi
        [ -s "$T/x.out" ] && bad "$_c: wrote to STDOUT"
        printf '=== err %s\n' "$_c" >> "$out"
        cat "$T/x.err" >> "$out"
    }
    errcase NoInput   'empty formula'
    errcase BadChar   "unexpected character '@' at 0"
    errcase BadNumber "malformed number '1e999999' at 0"
    errcase Expected  'expected a value but found end of formula at 2'
    errcase Unclosed  "unclosed '(' opened at 0"
    errcase Trailing  "trailing input '2' at 2"
    errcase TooDeep   'formula nested deeper than 64 at 65'
    errcase HugeRef   "reference 'A1048577' is outside the grid, at 0"
    errcase DivZero   '#DIV/0! division by zero'
    errcase BadName   '#NAME? unknown function NOPE'
    errcase BadRef    '#REF! A1:A9 is a range, not a single cell'
    errcase NotNum    '#VALUE! text "xy" is not a number'
    # The cycle NAMES itself here too, through a caller that dies by it. [6]
    # asserts the same claim on the transcript; this one cannot be blessed by
    # RECORD=1 and is bounded, so a detector that hangs reports rather than sits.
    errcase Cycle     '#CYCLE! A1 -> B1 -> A1'
    errcase SelfCycle '#CYCLE! G1 -> G1'
fi

# The coverage floor for [9], read out of the same two enums as [5]. NoText is
# the one variant with no arm, because nothing constructs it -- asserted
# statically above, not assumed here.
COVERED9='NoInput BadChar BadNumber Expected Unclosed Trailing TooDeep HugeRef
          DivZero BadName BadRef NotNum Cycle'
n9=0
for src_file in "$src/cell/cell.ty:CellErr" "$src/cell/expr.ty:ParseErr"; do
    f=${src_file%:*}; en=${src_file#*:}
    for v in $(enum_variants "$f" "$en"); do
        n9=$((n9 + 1))
        if [ "$v" = NoText ]; then continue; fi
        hit=0
        for c in $COVERED9; do [ "$v" = "$c" ] && hit=1; done
        [ "$hit" -eq 1 ] || bad "$en variant $v has no arm in [9] -- nothing proves a caller dies by its message"
    done
done
[ "$n9" -eq 14 ] || bad "found $n9 error variant(s) across CellErr and ParseErr, expected 14 -- the scan is broken and [9]'s floor asserts nothing"

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
    echo "tycho-sheet: green (demo transcript byte-identical over 2 runs and equal to the golden; 98411 rendered floats all read back bit-equal with 0.1+0.2, 2^53, DBL_MAX and the min subnormal each asserted on its own and none of the 98411 falling back to #NUM!; str(0.1+0.2) round-trips through the fixed builtin; =1/0 is a #DIV/0! VALUE that reaches its readers while five malformed formulas are refused and store nothing; $((nvar - nunreach)) of $nvar error variants reached and $nunreach declared unreachable, confirmed absent from every transcript and unconstructed in the source; $((n9 - 1)) of $n9 variants also exit non-zero with their own whole message and an empty stdout through a probe built on a copy of cell/ and sheet/; a cycle is named F1 -> F2 -> F3 -> F1 and a self-reference G1 -> G1, both bounded; a 10000- and a 100000-deep chain evaluate exactly and four different depth limits past them fail closed by name; three bad invocations exit non-zero)"
else
    echo "tycho-sheet: FAIL"; exit 1
fi
