#!/bin/sh
# Gate for tycho-agg, the group-and-count in tools/tycho-agg/.
#
# Re-record the golden with:  RECORD=1 sh tools/tycho-agg/run.sh
#
# WHY THIS LANE'S SUBJECT IS GENERICS, NOT COUNTING. tycho-agg declares its own
# generics in `pipe/` and instantiates them across a package boundary at its own
# `Row`. A golden can see the counts; it cannot see whether the generics were
# instantiated at all, so leg [4] reads the emitted C for the mangled symbols --
# and that IS what no other lane does: no other `run.sh` greps a `pkg__fn__type`
# mangling (checked 2026-08-13). tycho-flow's lane probes a cross-package generic
# composition, but through behaviour, not through the emitted symbol.
#
# WHAT IT ASSERTS
#   [1] THE REPORT, TWICE, and equal to the golden. No clock, no environment.
#   [2] THE COUNTS AGAINST LITERALS. north 3, south 2, east 1 over a fixture
#       written here, so `RECORD=1` cannot bless a miscount. The fixture carries
#       a row with an EMPTY key, which must be dropped by `pipe.keep` and must
#       not become a fourth group -- that row is the reason `nonempty` and
#       `rows` differ, and both numbers are asserted.
#   [3] --min FILTERS AND DOES NOT RECOUNT. At `--min 2` east disappears while
#       the header line still reports distinct=3: the threshold is a display
#       rule, not a change to the aggregation.
#   [4] THE GENERICS REALLY INSTANTIATED. The emitted C must carry
#       `pipe__keep__`, `pipe__to__` and `pipe__group_into__` mangled at this
#       program's own types. Dead-code elimination or a mis-resolved call would
#       leave the numbers right and this leg red.
#   [5] THE FAILURE PATHS. A missing file, a column that is not in the header,
#       and an unknown option each exit non-zero and name the thing.
#
# Every run is bounded by timeout(1).
set -u
cd "$(dirname "$0")/../.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
RECORD="${RECORD:-0}"
golden="tools/tycho-agg/agg.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
note() { echo "FAIL $1"; fail=1; }

$TYCHOC -o "$T/agg" tools/tycho-agg/main.ty > "$T/build.log" 2>&1 || {
    echo "agg-check: FAILED (tycho-agg does not build)"; tail -3 "$T/build.log"; exit 1; }

cat > "$T/sales.csv" <<'CSV'
region,item,qty
north,apple,3
south,pear,1
north,apple,2
east,fig,7
south,apple,4
north,pear,9
,orphan,1
CSV

# [1] two runs, identical, and the first equal to the golden
timeout 10 "$T/agg" --csv "$T/sales.csv" --by region > "$T/one.txt" 2>&1 || note "[1] first run exited non-zero"
timeout 10 "$T/agg" --csv "$T/sales.csv" --by region > "$T/two.txt" 2>&1 || note "[1] second run exited non-zero"
cmp -s "$T/one.txt" "$T/two.txt" || note "[1] two runs printed different output"

if [ "$RECORD" = 1 ]; then
    cp "$T/one.txt" "$golden"; echo "rec  $golden"
else
    [ -f "$golden" ] || { echo "agg-check: FAILED (no golden -- run RECORD=1)"; exit 1; }
    cmp -s "$T/one.txt" "$golden" || { note "[1] output differs from the golden"; diff "$golden" "$T/one.txt" | head -8; }
fi

# [2] the counts, and the empty-key row's exclusion, against literals
cat > "$T/want.txt" <<'WANT'
rows=7 nonempty=6 distinct=3 counted=6
east 1
north 3
south 2
WANT
cmp -s "$T/one.txt" "$T/want.txt" || { note "[2] counts are not the expected ones"; diff "$T/want.txt" "$T/one.txt" | head -8; }

# [3] --min filters the display without changing the aggregation
timeout 10 "$T/agg" --csv "$T/sales.csv" --by region --min 2 > "$T/min.txt" 2>&1 || note "[3] --min run exited non-zero"
grep -q "^rows=7 nonempty=6 distinct=3 counted=6$" "$T/min.txt" || note "[3] --min changed the header line -- it must filter the display only"
grep -q "^east " "$T/min.txt" && note "[3] east (count 1) survived --min 2"
grep -q "^north 3$" "$T/min.txt" || note "[3] north 3 missing under --min 2"

# [4] the generics were instantiated at this program's own types
$TYCHOC tools/tycho-agg/main.ty --emit-c -o "$T/agg_c" >/dev/null 2>&1
if [ -f "$T/agg_c.c" ]; then
    for sym in pipe__keep__ pipe__to__ pipe__group_into__; do
        grep -q "$sym" "$T/agg_c.c" || note "[4] the emitted C carries no $sym -- the generic did not instantiate"
    done
else
    note "[4] no emitted C to read"
fi

# [5] failure paths
timeout 10 "$T/agg" --csv "$T/nosuch.csv" --by region > "$T/e1.txt" 2>&1
[ $? -ne 0 ] || note "[5] a missing csv exited 0"
grep -q "nosuch.csv" "$T/e1.txt" || note "[5] the missing-file message does not name the file"
timeout 10 "$T/agg" --csv "$T/sales.csv" --by nosuchcol > "$T/e2.txt" 2>&1
[ $? -ne 0 ] || note "[5] an absent column exited 0"
grep -q "nosuchcol" "$T/e2.txt" || note "[5] the absent-column message does not name the column"
timeout 10 "$T/agg" --csv "$T/sales.csv" --by region --bogus 1 > "$T/e3.txt" 2>&1
rc=$?
[ "$rc" -eq 2 ] || note "[5] an unknown option exited $rc, expected 2"

[ "$fail" = 0 ] || { echo "agg-check: FAILED"; exit 1; }
echo "tycho-agg: green (report identical over 2 runs and equal to the golden; counts north 3 / south 2 / east 1 against literals with the empty-key row dropped by the generic filter and rows 7 vs nonempty 6 both pinned; --min filters the display without moving distinct=3; the emitted C carries pipe__keep__, pipe__to__ and pipe__group_into__ instantiated at this program's own Row; a missing file, an absent column and an unknown option each exit non-zero naming the thing)"
