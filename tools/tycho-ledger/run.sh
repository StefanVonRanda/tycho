set -u
cd "$(dirname "$0")/../.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
RECORD="${RECORD:-0}"
golden="tools/tycho-ledger/ledger.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
note() { echo "FAIL $1"; fail=1; }

$TYCHOC -o "$T/led" tools/tycho-ledger/main.ty > "$T/build.log" 2>&1 || {
    echo "ledger-check: FAILED (tycho-ledger does not build)"; tail -3 "$T/build.log"; exit 1; }

cat > "$T/l.txt" <<'LEDGER'
alice 250
bob 75
# a comment line, and a blank one below

alice 100
carol -40
LEDGER

# [1] two runs, identical, first equal to the golden
timeout 10 "$T/led" --file "$T/l.txt" > "$T/one.txt" 2>&1 || note "[1] first run exited non-zero"
timeout 10 "$T/led" --file "$T/l.txt" > "$T/two.txt" 2>&1 || note "[1] second run exited non-zero"
cmp -s "$T/one.txt" "$T/two.txt" || note "[1] two runs printed different output"

if [ "$RECORD" = 1 ]; then
    cp "$T/one.txt" "$golden"; echo "rec  $golden"
else
    [ -f "$golden" ] || { echo "ledger-check: FAILED (no golden -- run RECORD=1)"; exit 1; }
    cmp -s "$T/one.txt" "$golden" || { note "[1] output differs from the golden"; diff "$golden" "$T/one.txt" | head -8; }
fi

# [2] the money, against literals. alice 250+100, bob 75, carol -40; the grand
# total is their sum, and --rate 2.0 doubles every line including the total.
cat > "$T/want.txt" <<'WANT'
alice 3.50
bob 0.75
carol -0.40
total 3.85
accounts 3
WANT
cmp -s "$T/one.txt" "$T/want.txt" || { note "[2] the totals are not the expected ones"; diff "$T/want.txt" "$T/one.txt" | head -6; }

timeout 10 "$T/led" --file "$T/l.txt" --rate 2.0 > "$T/dbl.txt" 2>&1 || note "[2] the --rate run exited non-zero"
cat > "$T/wantdbl.txt" <<'WANT'
alice 7.00
bob 1.50
carol -0.80
total 7.70
accounts 3
WANT
cmp -s "$T/dbl.txt" "$T/wantdbl.txt" || { note "[2] --rate 2.0 did not double every line"; diff "$T/wantdbl.txt" "$T/dbl.txt" | head -6; }

# [3]+[4] the five refusals, against a COPY of money/. Each must fail to compile,
# and the message must name the type the way the SOURCE spells it.
mkdir -p "$T/probe"
cp -r tools/tycho-ledger/money "$T/probe/money"
probe() {   # probe <name> <body> <type-that-must-be-named>
    _n=$1; _body=$2; _want=$3
    printf 'package main\nimport "money"\nfn main():\n%s\n' "$_body" > "$T/probe/main.ty"
    if $TYCHOC -o "$T/probe/x" "$T/probe/main.ty" > "$T/probe/err" 2>&1; then
        note "[3] $_n COMPILED -- newtype distinctness was lost; the whole point of money/ is gone"
    else
        grep -q "$_want" "$T/probe/err" || { note "[4] $_n was refused, but the message does not name '$_want'"; head -1 "$T/probe/err"; }
        grep -q '__' "$T/probe/err" && { note "[4] $_n names a MANGLED type (contains '__') -- diagnostics must print what the source can type"; head -1 "$T/probe/err"; }
    fi
}
probe "Cents + Rate"        '    println(str(to_int(money.cents(1) + money.rate(2.0))))' 'money.Cents'
probe "raw int as Cents"    '    println(money.show(money.add(money.cents(1), 5)))'      'money.Cents'
probe "bare string key"     '    m := []money.Account: money.Cents
    m[money.account("a")] = money.cents(1)
    println(money.show(m["a"]))'                                                         'money.Account'
probe "Account as Cents"    '    println(money.show(money.account("x")))'                'money.Account'
probe "Cents into an int"   '    v: int = money.cents(1)
    println(str(v))'                                                                     'money.Cents'

# [5] keys() returns WRAPPED keys: index the map with what it returned, no unwrap
cat > "$T/probe/main.ty" <<'PROBE'
package main
import "money"
fn main():
    m := []money.Account: money.Cents
    m[money.account("a")] = money.cents(5)
    for k in keys(m):
        println(money.name_of(k) + "=" + money.show(m[k]))
PROBE
$TYCHOC -o "$T/probe/k" "$T/probe/main.ty" > "$T/probe/kerr" 2>&1 || {
    note "[5] keys() no longer yields the wrapped key type"; head -2 "$T/probe/kerr"; }
[ -x "$T/probe/k" ] && { out=$(timeout 10 "$T/probe/k"); [ "$out" = "a=0.05" ] || note "[5] the keys() round trip printed '$out', want 'a=0.05'"; }

# [6] a non-numeric amount is refused, naming it
printf 'alice 25x\n' > "$T/bad.txt"
timeout 10 "$T/led" --file "$T/bad.txt" > "$T/bad.out" 2>&1
[ $? -ne 0 ] || note "[6] a non-numeric amount exited 0 -- parse_int fails open, so 25x would post as 25"
grep -q "not an amount: 25x" "$T/bad.out" || note "[6] the refusal does not name the offending field"

# [7] an unknown option is refused by name
timeout 10 "$T/led" --file "$T/l.txt" --raet 2 > "$T/unk.txt" 2>&1
[ $? -ne 0 ] || note "[7] an unknown option exited 0"
grep -q -- "--raet" "$T/unk.txt" || note "[7] the unknown-option message does not name the option"

[ "$fail" = 0 ] || { echo "ledger-check: FAILED"; exit 1; }
echo "tycho-ledger: green (run identical over 2 runs and equal to the golden; per-account totals and the --rate 2.0 doubling both against literals; all FIVE distinctness violations -- Cents+Rate, a raw int as Cents, a bare string key, an Account as Cents, a Cents into an int -- still refused, each naming an unmangled type; keys() hands back wrapped keys that index the map with no unwrap; a non-numeric amount refused by name; an unknown option refused by name)"
