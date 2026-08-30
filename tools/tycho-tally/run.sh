set -u
cd "$(dirname "$0")/../.." || exit 2
TYCHOC="${TYCHOC:-./tychoc}"
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
pkg-config --exists sqlite3 2>/dev/null || { echo "tally-check: SKIPPED (missing: sqlite3)"; exit 0; }
RECORD="${RECORD:-0}"
golden="tools/tycho-tally/tally.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
note() { echo "FAIL $1"; fail=1; }

$TYCHOC -o "$T/tally" tools/tycho-tally/main.ty > "$T/build.log" 2>&1 || {
    echo "tally-check: FAILED (tycho-tally does not build)"; tail -3 "$T/build.log"; exit 1; }

# [1] the suite, twice
timeout 20 "$T/tally" --selftest > "$T/one.txt" 2>&1 || note "[1] --selftest exited non-zero"
timeout 20 "$T/tally" --selftest > "$T/two.txt" 2>&1 || note "[1] --selftest exited non-zero on the second run"
cmp -s "$T/one.txt" "$T/two.txt" || note "[1] two --selftest runs printed different output"

# [3] + [4] the ledger from outside, across four processes
db="$T/ledger.db"
timeout 20 "$T/tally" --db "$db" --add "coffee=350" > "$T/add1.txt" 2>&1 || note "[4] first add exited non-zero"
timeout 20 "$T/tally" --db "$db" --add "coffee=275" > "$T/add2.txt" 2>&1 || note "[4] second add exited non-zero"
timeout 20 "$T/tally" --db "$db" --add "books=1299" > "$T/add3.txt" 2>&1 || note "[4] third add exited non-zero"
timeout 20 "$T/tally" --db "$db" --report > "$T/report.txt" 2>&1 || note "[3] --report exited non-zero"
cat > "$T/want.txt" <<'WANT'
books 1299
coffee 625
WANT
cmp -s "$T/report.txt" "$T/want.txt" || { note "[3/4] the report is not the expected one"; diff "$T/want.txt" "$T/report.txt" | head -6; }

# [5] a typo'd amount is refused and books nothing
timeout 20 "$T/tally" --db "$db" --add "junk=35q" > "$T/bad.txt" 2>&1
rc=$?
[ "$rc" -eq 2 ] || note "[5] a non-numeric amount exited $rc, expected 2"
timeout 20 "$T/tally" --db "$db" --report > "$T/report2.txt" 2>&1
cmp -s "$T/report2.txt" "$T/want.txt" || note "[5] the refused entry still reached the ledger"

if [ "$RECORD" = 1 ]; then
    cat "$T/one.txt" "$T/report.txt" > "$golden"; echo "rec  $golden"
else
    [ -f "$golden" ] || { echo "tally-check: FAILED (no golden -- run RECORD=1)"; exit 1; }
    cat "$T/one.txt" "$T/report.txt" > "$T/got.txt"
    cmp -s "$T/got.txt" "$golden" || { note "[1/3] transcript differs from the golden"; diff "$golden" "$T/got.txt" | head -8; }
fi

# [2] THE POSITIVE CONTROL: the suite must be able to fail. One expected total is
# changed in a COPY -- the tree is never edited -- and the run must exit 1, name
# the check and show both sides.
mkdir -p "$T/broken"
sed 's/"625", "coffee total folded by SUM"/"999", "coffee total folded by SUM"/' \
    tools/tycho-tally/main.ty > "$T/broken/main.ty"
if ! cmp -s "$T/broken/main.ty" tools/tycho-tally/main.ty; then
    $TYCHOC -o "$T/broken/tally" "$T/broken/main.ty" > "$T/broken/build.log" 2>&1 || note "[2] the broken copy does not build"
    if [ -x "$T/broken/tally" ]; then
        timeout 20 "$T/broken/tally" --selftest > "$T/broken/out.txt" 2>&1
        rc=$?
        [ "$rc" -eq 1 ] || note "[2] a broken assertion exited $rc, expected 1 -- core:testing did not notice"
        grep -q "FAIL: coffee total folded by SUM (got 625, want 999)" "$T/broken/out.txt" \
            || note "[2] the failure does not name the check and both sides"
        grep -q "^FAIL tycho-tally (1 of 15 checks failed)" "$T/broken/out.txt" \
            || note "[2] the verdict line does not count the failure"
    fi
else
    note "[2] the sed found nothing to break -- this control is vacuous, fix the pattern"
fi

[ "$fail" = 0 ] || { echo "tally-check: FAILED"; exit 1; }
echo "tycho-tally: green (15-check suite passes twice byte-identically and its verdict is EARNED -- a copy with one expected total changed exits 1, names the check and counts 1 of 15; the ledger is written by three processes and read by a fourth, its report matching literals here with SQL doing the sort and the SUM; a non-numeric amount exits 2 and reaches no row)"
