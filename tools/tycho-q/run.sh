set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "tycho-q: no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
RECORD="${RECORD:-0}"
golden="$PWD/tools/tycho-q/expected.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL: $*"; fail=1; }

Q="$T/tycho-q"
if ! "$TYCHOC" tools/tycho-q/main.ty -o "$Q" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/build.log"
    echo "tycho-q: FAIL"; exit 1
fi

fix="$T/fix"; mkdir -p "$fix"

# empty field (Di's region) · quoted comma (Ada's note) · embedded newline (Bo's
# note) · UTF-8 (café) · leading-zero numeric strings (007, 0080) · a negative ·
# decimals at two scales · duplicate sort keys (price 1.50 twice, qty 7 twice).
cat > "$fix/sales.csv" <<'EOF'
name,region,qty,price,code,note
Ada,eu,12,1.50,007,"Hello, world"
Bo,us,-3,0.10,42,"line one
line two"
Cy,eu,7,0.20,0080,café
Di,,0,2.00,9,plain
Eve,eu,7,1.50,11,dup
EOF

cat > "$fix/sales.json" <<'EOF'
[{"name":"Ada","region":"eu","qty":12,"price":1.50,"code":"007","note":"Hello, world"},
 {"name":"Bo","region":"us","qty":-3,"price":0.10,"code":"42","note":"line one\nline two"},
 {"name":"Cy","region":"eu","qty":7,"price":0.20,"code":"0080","note":"café"},
 {"name":"Di","region":null,"qty":0,"price":2.00,"code":"9","note":"plain"},
 {"name":"Eve","region":"eu","qty":7,"price":1.50,"code":"11","note":"dup"}]
EOF

cat > "$fix/types.csv" <<'EOF'
n,v
1,007
2,0
3,-0
4,-3
5,1.50
6,1e3
7,+1
8,1.
9,.5
10,12345678901234567890123456
11,
EOF

# ranks: null < bool < number < string. CSV cannot produce a bool at all
# (DECISION 1 types by round trip and VBool is not in it), so the bool rank is
# only reachable from JSON -- which is why this fixture exists.
cat > "$fix/mix.json" <<'EOF'
[{"id":1,"v":42},{"id":2,"v":null},{"id":3,"v":"zz"},
 {"id":4,"v":true},{"id":5,"v":false},{"id":6,"v":7}]
EOF

# a column first seen in the LAST row is still a column: the header is the union
# of keys in first-appearance order, and every earlier row needs a null in it.
printf '%s\n' '[{"a":1,"b":2},{"b":5},{"a":9,"c":"new"}]' > "$fix/sparse.json"

printf '%s' '[{"a":1.5,"tenth":0.1,"big":123456789012345678901234567890,"scale":1.50}]' > "$fix/float.json"

# the two spellings json_float_cell refuses, one file each, so a leg that stops
# firing cannot hide behind the other.
printf '%s' '[{"a":1e3}]'   > "$fix/exp.json"      # an exponent: refused HERE, not by core:json
printf '%s' '[{"a":-0.0}]'  > "$fix/negzero.json"  # the round trip drops the sign
printf '%s' '[{"a":1.}]'    > "$fix/badnum.json"   # malformed: core:json's own BadFloat
printf '%s' '[}]'           > "$fix/spin.json"     # was: out of memory

# ---------------------------------------------------------------------------
# [1] the transcript, against the golden
#
# Every query runs with the fixture directory as cwd, so the `from` clause names
# a bare relative path and no temp directory can reach the recorded output.
# ---------------------------------------------------------------------------
cd "$fix" || exit 2
out="$T/all.out"
: > "$out"

# q <label> <arg>...  -- runs, appends stdout to the transcript, requires exit 0.
q() {
    _lbl=$1; shift
    printf '=== %s\n' "$_lbl" >> "$out"
    "$Q" "$@" > "$T/o" 2> "$T/e"
    _rc=$?
    cat "$T/o" >> "$out"
    if [ "$_rc" -ne 0 ]; then
        bad "$_lbl: exited $_rc, expected 0"; sed 's/^/      /' "$T/e"
    fi
    if [ -s "$T/e" ]; then bad "$_lbl: wrote to stderr"; sed 's/^/      /' "$T/e"; fi
}

# --- the parse. Precedence and associativity are proved by the s-expression.
q 'Q01 precedence'      --explain 'select 1 + 2 * 3 from x.csv'
q 'Q02 left assoc'      --explain 'select 1 - 2 - 3 from x.csv'
q 'Q03 parens + unary'  --explain 'select (1 + 2) * 3, -1 + 2 from x.csv'
q 'Q04 literals'        --explain "select 1, 1.50, 0.1, \"eu\", 'sq', true, false, null from x.csv"
q 'Q05 and/or/not'      --explain 'select a == 1 and b > 2 or not c from x.csv'
q 'Q06 all clauses'     --explain "select name, qty * price as total from sales.csv where region == 'eu' and qty > 10 order by total desc limit 5"
q 'Q07 multi-key'       --explain 'select a from x.csv order by a, b desc, c asc limit 0'

# --- rows in, rows out.
q 'Q08 select *'        'select * from sales.csv'
q 'Q09 projection'      'select name, code, note from sales.csv'
q 'Q10 where string'    "select name from sales.csv where region == 'eu'"
q 'Q11 where decimal'   'select name, price from sales.csv where price > 0.15'
q 'Q12 where null'      'select name from sales.csv where region == null'
q 'Q13 typing'          'select n, v * 1 as num from types.csv where n == 2 or n == 4 or n == 5 or n == 10'
q 'Q14 no rows'         "select name from sales.csv where region == 'zz'"
q 'Q15 and/or/not rows' "select name from sales.csv where not (region == 'eu') or qty > 10"

# --- arithmetic. 0.1 + 0.2 is 0.3 exactly: core:decimal, and no float anywhere.
q 'Q16 decimal mul'     'select name, qty * price as total from sales.csv where qty > 0'
q 'Q17 0.1 + 0.2'       "select 0.1 + 0.2 as sum from sales.csv where name == 'Ada'"
q 'Q18 div'             "select name, qty / 3 as third from sales.csv where name == 'Ada'"
q 'Q19 unaliased'       "select name, qty * price from sales.csv where name == 'Cy'"

# --- the order. Both directions of every rank boundary, and stability.
q 'Q20 order asc'       'select name, price from sales.csv order by price'
q 'Q21 order desc'      'select name, price from sales.csv order by price desc'
q 'Q22 order string'    'select name from sales.csv order by name desc'
q 'Q23 mixed keys'      'select name, region, price from sales.csv order by region asc, price desc'
q 'Q24 total order asc' 'select id, v from mix.json order by v'
q 'Q25 total order desc' 'select id, v from mix.json order by v desc'

# --- limit, after the ordering.
q 'Q26 order + limit'   'select name, price from sales.csv order by price desc limit 3'
q 'Q27 limit 0'         'select name from sales.csv order by name limit 0'
q 'Q28 limit > rows'    'select name from sales.csv limit 99'

# --- JSON.
q 'Q29 json select *'   'select * from sales.json'
q 'Q30 json sparse'     'select * from sparse.json'
q 'Q31 order by alias'  "select name, qty * price as total from sales.csv where region == 'eu' order by total desc limit 5"

# --- JSON floats, read through the LEXEME and not through binary64. Q32 is the
# exactness claim (see the float.json comment for what each column proves) and
# Q33 is the ordering-and-arithmetic claim; leg [3] below turns Q33 into a `cmp`
# against the identical query over the CSV fixture, which is what makes it a
# claim about JSON floats and CSV decimals AGREEING rather than a claim about
# JSON alone.
q 'Q32 json float exact' 'select * from float.json'
q 'Q33 json float order' 'select name, price, qty * price as total from sales.json where price > 0.15 order by price desc, name asc'

q 'Q34 total / count'   'select name, qty * price as total, qty * price / qty as avg from sales.csv where qty > 0'
q 'Q35 inexact div'     'select name, qty / 5 as fifth from sales.csv'

if [ "$RECORD" = 1 ]; then
    if [ "$fail" -ne 0 ]; then echo "FAIL: refusing to RECORD from a failed run"; exit 1; fi
    cp "$out" "$golden"; echo "rec  tycho-q"
fi
if [ ! -f "$golden" ]; then
    bad "no golden -- run RECORD=1 sh tools/tycho-q/run.sh"
elif ! cmp -s "$out" "$golden"; then
    bad "transcript != golden"; diff "$golden" "$out" | sed 's/^/      /'
fi

# ---------------------------------------------------------------------------
# [2] reading does not rewrite
# ---------------------------------------------------------------------------
for f in sales.csv types.csv; do
    "$Q" "select * from $f" > "$T/rt.out" 2>"$T/rt.err" || {
        bad "round trip: select * from $f exited non-zero"; sed 's/^/      /' "$T/rt.err"
    }
    cmp -s "$T/rt.out" "$f" || bad "round trip: select * from $f is not byte-identical to $f"
done

ident="select name, qty, code, price, qty * price as total from %s where region == 'eu' and price > 0.15 order by price desc, name asc limit 2"
# shellcheck disable=SC2059
"$Q" "$(printf "$ident" sales.csv)"  > "$T/id.csv"  2>"$T/id.err" || bad "identity: CSV leg failed"
# shellcheck disable=SC2059
"$Q" "$(printf "$ident" sales.json)" > "$T/id.json" 2>>"$T/id.err" || bad "identity: JSON leg failed"
if ! cmp -s "$T/id.csv" "$T/id.json"; then
    bad "identity: CSV and JSON disagree on the same query"
    diff "$T/id.csv" "$T/id.json" | sed 's/^/      /'
fi
# and the strongest form of the same claim: `select *` over the JSON fixture is
# byte-identical to the CSV fixture FILE, so the JSON header (union of keys, in
# first-appearance order) reproduces the CSV column order as well as the rows.
"$Q" 'select * from sales.json' > "$T/star.json" 2>/dev/null
cmp -s "$T/star.json" sales.csv || bad "identity: select * over JSON != the CSV fixture"

# ---------------------------------------------------------------------------
# [4] and [5] the failure legs
#
# refuses <label> <expected stderr substring> <arg>...
# Non-zero exit, that substring on stderr, and ZERO bytes on stdout -- all three,
# because a tool that prints half a result and then complains is worse than one
# that prints nothing.
# ---------------------------------------------------------------------------
refuses() {
    _lbl=$1; _want=$2; shift 2
    "$Q" "$@" > "$T/r.out" 2> "$T/r.err"
    _rc=$?
    if [ "$_rc" -eq 0 ]; then
        bad "$_lbl: EXITED 0 -- the query was accepted"
    elif ! grep -qF "$_want" "$T/r.err"; then
        bad "$_lbl: failed but not for the expected reason (want: $_want)"
        sed 's/^/      /' "$T/r.err"
    fi
    if [ -s "$T/r.out" ]; then
        bad "$_lbl: wrote $(wc -c < "$T/r.out") bytes to STDOUT before failing"
    fi
}

refuses 'malformed query'   'parse error at byte 9: unexpected token form' \
        'select a form sales.csv'
refuses 'missing file'      'no such file: nope.csv' \
        'select * from nope.csv'
refuses 'unknown column'    'no such column: nope (the header has: name, region, qty, price, code, note)' \
        'select nope from sales.csv'
refuses 'unknown order key' 'no such column: nope' \
        'select name from sales.csv order by nope'
refuses 'bad comparison'    'cannot compare string "007" with int 42' \
        'select name from sales.csv where code == 42'
refuses 'divide by zero'    '`/` by zero' \
        'select name, price / qty as u from sales.csv'
refuses 'no truthiness'     '`where` needs a boolean' \
        'select name from sales.csv where qty'
refuses 'json exponent'     'the number 1e3 has no exact decimal spelling here' \
        'select * from exp.json'
refuses 'json negative zero' 'the number -0.0 has no exact decimal spelling here' \
        'select * from negzero.json'
refuses 'json malformed num' "byte 8: '.' or exponent with no digits after it -- a JSON number needs at least one digit after the \`.\`" \
        'select * from badnum.json'
# `[}]`: the old OOM. The message is the corelib's own -- `}` begins no value --
# and the byte offset is the point: it names the `}` at 1, not the document.
refuses 'json bad byte'     'spin.json: byte 1: byte begins no JSON value' \
        'select * from spin.json'

if [ "$fail" -eq 0 ]; then
    echo "tycho-q: green (35-query transcript == golden; select * over 2 fixtures byte-identical to the input; CSV and JSON agree under cmp on a query that filters, orders, multiplies and renders a price that is a decimal in one and a float in the other; malformed query, missing file, unknown column, bad comparison, division by zero, both refused JSON float spellings and both core:json parse errors all refused with empty stdout)"
else
    echo "tycho-q: FAIL"; exit 1
fi
