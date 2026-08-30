set -u
cd "$(dirname "$0")/../.." || exit 2
TYCHOC="${TYCHOC:-./tychoc}"
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
RECORD="${RECORD:-0}"
golden="tools/tycho-stat/stat.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
note() { echo "FAIL $1"; fail=1; }

$TYCHOC -o "$T/stat" tools/tycho-stat/main.ty > "$T/build.log" 2>&1 || {
    echo "stat-check: FAILED (tycho-stat does not build)"; tail -3 "$T/build.log"; exit 1; }

# [1] two runs, identical, first equal to the golden
timeout 10 "$T/stat" --nums 3,1,4,1,5 --label demo > "$T/one.txt" 2>&1 || note "[1] first run exited non-zero"
timeout 10 "$T/stat" --nums 3,1,4,1,5 --label demo > "$T/two.txt" 2>&1 || note "[1] second run exited non-zero"
cmp -s "$T/one.txt" "$T/two.txt" || note "[1] two runs printed different output"

if [ "$RECORD" = 1 ]; then
    cp "$T/one.txt" "$golden"; echo "rec  $golden"
else
    [ -f "$golden" ] || { echo "stat-check: FAILED (no golden -- run RECORD=1)"; exit 1; }
    cmp -s "$T/one.txt" "$golden" || { note "[1] output differs from the golden"; diff "$golden" "$T/one.txt" | head -8; }
fi

# [2] the statistics, against arithmetic done here. 0..200 inclusive: 201 values,
# sum 20100, min 0, max 200, mean exactly 100. Every expectation is COMPUTED from
# the corpus rather than written down, so widening the corpus cannot silently
# stop testing anything.
awk 'BEGIN { for (i = 0; i <= 200; i++) print i }' > "$T/corpus.txt"
exp_count=$(wc -l < "$T/corpus.txt" | tr -d ' ')
exp_sum=$(awk '{s += $1} END {print s}' "$T/corpus.txt")
exp_min=$(awk 'NR == 1 || $1 < m {m = $1} END {print m}' "$T/corpus.txt")
exp_max=$(awk 'NR == 1 || $1 > m {m = $1} END {print m}' "$T/corpus.txt")
exp_mean=$(awk -v s="$exp_sum" -v n="$exp_count" 'BEGIN {printf "%d", int(s / n)}')
timeout 10 "$T/stat" --file "$T/corpus.txt" --label corpus > "$T/corpus.out" 2>&1 || note "[2] the corpus run exited non-zero"
got() { sed -n "s/^  $1 *= //p" "$T/corpus.out"; }
[ "$(got count)" = "$exp_count" ] || note "[2] count is $(got count), the corpus has $exp_count"
[ "$(got sum)"   = "$exp_sum"   ] || note "[2] sum is $(got sum), the corpus sums to $exp_sum"
[ "$(got min)"   = "$exp_min"   ] || note "[2] min is $(got min), the corpus minimum is $exp_min"
[ "$(got max)"   = "$exp_max"   ] || note "[2] max is $(got max), the corpus maximum is $exp_max"
[ "$(got mean)"  = "$exp_mean"  ] || note "[2] mean is $(got mean), the corpus mean is $exp_mean"

# [3] the empty identities, from zero$(T) and the typed empty array
grep -q '^empty count = 0$' "$T/corpus.out" || { note "[3] the empty variadic count is not 0"; grep '^empty count' "$T/corpus.out"; }
grep -q '^empty sum   = 0$' "$T/corpus.out" || { note "[3] the empty zero-seeded sum is not 0"; grep '^empty sum' "$T/corpus.out"; }

# [4] negatives: a corpus whose minimum is below zero
timeout 10 "$T/stat" --nums -7,3,-2,10 --label neg > "$T/neg.out" 2>&1 || note "[4] the negative run exited non-zero"
[ "$(sed -n 's/^  min *= //p' "$T/neg.out")" = "-7" ] || { note "[4] min over negatives is wrong"; grep 'min' "$T/neg.out"; }
[ "$(sed -n 's/^  sum *= //p' "$T/neg.out")" = "4" ]  || { note "[4] sum over negatives is wrong"; grep 'sum' "$T/neg.out"; }

# [5] a non-numeric field is refused, naming it
timeout 10 "$T/stat" --nums 3x,1 > "$T/bad.txt" 2>&1
[ $? -ne 0 ] || note "[5] a non-numeric field exited 0 -- parse_int fails open, so 3x would read as 3"
grep -q "not a number: 3x" "$T/bad.txt" || note "[5] the refusal does not name the offending field"

# [6] #40's negative control, against a COPY of num/: an empty generic variadic
# that names no type must still be refused, and the message must point at the
# spelling that works.
mkdir -p "$T/probe"
cp -r tools/tycho-stat/num "$T/probe/num"
cat > "$T/probe/main.ty" <<'PROBE'
package main
import "num"
fn main():
    println(str(num.count()))
PROBE
if $TYCHOC -o "$T/probe/x" "$T/probe/main.ty" > "$T/probe/err" 2>&1; then
    note "[6] an empty generic variadic naming NO type COMPILED -- FRICTION #40's fix now infers where it should refuse"
else
    grep -q "cannot infer the element type" "$T/probe/err" || note "[6] refused, but not with the element-type diagnostic"
    grep -q "name the type" "$T/probe/err" || note "[6] the refusal does not point at the \$(<type>)() spelling that works"
fi

# [7] an unknown option is refused by name
timeout 10 "$T/stat" --nums 1,2 --numms 3 > "$T/unk.txt" 2>&1
[ $? -ne 0 ] || note "[7] an unknown option exited 0"
grep -q -- "--numms" "$T/unk.txt" || note "[7] the unknown-option message does not name the option"

[ "$fail" = 0 ] || { echo "stat-check: FAILED"; exit 1; }
echo "tycho-stat: green (run identical over 2 runs and equal to the golden; count/sum/min/max/mean over a 201-number corpus each equal to the runner's own arithmetic; both empty identities exactly 0 from zero\$(T); negatives survive parsing and min; a non-numeric field is refused by name rather than read as its leading digits; an empty generic variadic naming no type is still refused with a message that points at the cure; an unknown option is refused by name)"
