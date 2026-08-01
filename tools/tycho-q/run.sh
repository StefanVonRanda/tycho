#!/bin/sh
# Gate for tycho-q, the SQL-ish query tool in tools/tycho-q/main.ty.
#
# WHY THIS IS A GOLDEN RUNNER AND NOT A DAEMON HARNESS. server/run.sh has to
# start a process, read a bound port out of a banner and talk to it over a
# socket, because the thing it gates is a server. tycho-q is a batch program:
# a query and a file go in, rows come out on stdout, so it gates the way
# tools/tycho-ar/run.sh and examples/*/run.sh do -- build it, run it over a
# fixture, compare stdout to a recorded golden -- with the query-specific
# assertions layered on top.
#
# Re-record the golden with:  RECORD=1 sh tools/tycho-q/run.sh
#
# WHAT IT ASSERTS, and why each leg exists. A query tool has exactly one way to
# betray the person using it: return the wrong rows, and look like it worked.
# Every leg below is aimed at one route to that.
#
#   [1] THE TRANSCRIPT MATCHES A GOLDEN. 30 queries covering the parse (`--explain`
#       s-expressions, so precedence is proved by shape and not by prose), cell
#       typing, `where`, decimal arithmetic, the total order, sort stability,
#       multi-key mixed directions, `limit`, and both readers. The golden is the
#       concatenated stdout, so it reddens on any change to a row, an order, a
#       header or a rendering.
#   [2] READING DOES NOT REWRITE. `select *` over each fixture is `cmp`-identical
#       to the fixture file itself. A query that neither filters nor computes must
#       return the bytes it was given -- including `007`, `-0`, `0080`, a 26-digit
#       integer, an empty field, a quoted comma, an embedded newline and UTF-8.
#   [3] CSV AND JSON AGREE. The same logical query over sales.csv and sales.json
#       is compared with `cmp`. Two readers that disagree would make the source
#       format part of the answer.
#   [4] THE FAILURE LEGS REFUSE. A malformed query, a missing file, an unknown
#       column, an incompatible comparison and a `/` with no exact answer each
#       exit non-zero with their reason on STDERR and NOTHING on stdout. The
#       empty-stdout half is the load-bearing half: tycho-q builds the whole
#       result before printing, so a consumer never reads a truncated CSV it
#       cannot tell is truncated.
#   [5] core:json's ERROR CHANNEL REACHES THE USER. `[{"a":1.5}]` and `[}]` are
#       refused, each naming the byte that failed. Handed to the OLD `json.parse`
#       the first exited 0 having FABRICATED a column -- the leftover `.5}]` was
#       read as the next key -- and the second exhausted memory from three bytes,
#       because `parse_value` consumed nothing at a byte it did not recognise
#       while the array loop advanced only on `,` or `]`. Neither was reportable:
#       `parse` returned `Json`, not a `Result`, so the package had no error
#       channel and tycho-q carried its own pre-validator in front of it.
#       `corelib/json/json.ty@parse_checked` fixed that on 2026-08-01 and the
#       pre-validator is gone; these two legs now assert on the CORELIB's message
#       (`tools/tycho-q/main.ty@json_err` relabels it and adds the float advice),
#       which is what proves the error actually crosses the package boundary and
#       reaches stderr.
#
# THE GOLDEN IS DETERMINISTIC ONLY BECAUSE THE FIXTURES ARE BUILT HERE. Every
# fixture is written by this script from a heredoc literal -- never from
# /dev/urandom, never copied out of the tree -- and every query is run with the
# fixture directory as the working directory, so no temp path and no host detail
# can reach stdout. Unlike tycho-ar's fixture no `touch -d` is needed: tycho-q
# reads no mtime and prints none.
#
# WHY THE FAILURE LEGS ARE NOT IN THE GOLDEN. Their stdout is empty by
# construction, so a golden over it would assert nothing; what has to be checked
# is the exit code and a substring of stderr, which is a `grep -qF` and not a
# `cmp`. Same split as tools/tycho-ar/run.sh, for the same reason.
set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC=./tychoc
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

# ---------------------------------------------------------------------------
# the fixtures -- every awkward case the phases used, from literals only.
#
# sales.csv and sales.json carry the SAME five rows. `price` is spelled as a
# JSON string because core:json has no float path at all and now refuses `1.50`
# outright rather than let it be read as 1 (DECISION 3 in
# tools/tycho-q/main.ty); a JSON string is not re-classified, so it stays a
# string there and a decimal here. That asymmetry is the documented cost, and
# leg [3] below picks a query over the columns where the two agree.
# ---------------------------------------------------------------------------
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
[{"name":"Ada","region":"eu","qty":12,"price":"1.50","code":"007","note":"Hello, world"},
 {"name":"Bo","region":"us","qty":-3,"price":"0.10","code":"42","note":"line one\nline two"},
 {"name":"Cy","region":"eu","qty":7,"price":"0.20","code":"0080","note":"café"},
 {"name":"Di","region":null,"qty":0,"price":"2.00","code":"9","note":"plain"},
 {"name":"Eve","region":"eu","qty":7,"price":"1.50","code":"11","note":"dup"}]
EOF

# one classification edge case per row. `v * 1` succeeds only where the cell was
# typed as a number, so leg [1]'s Q13 is a verdict on each in turn, and leg [2]'s
# round trip proves none of them is rewritten on the way out. The 26-digit
# integer is here rather than in sales.csv because a JSON integer that large
# would overflow core:json's int-only JNum, which is a different finding.
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

printf '%s' '[{"a":1.5}]' > "$fix/float.json"   # was: exit 0, key `.5}]`
printf '%s' '[}]'         > "$fix/spin.json"    # was: out of memory

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
q 'Q18 exact div'       "select name, qty / 3 as third from sales.csv where name == 'Ada'"
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

# ---------------------------------------------------------------------------
# [3] CSV and JSON agree
#
# `where` + both order directions + a tie broken by the second key + `limit`,
# over the columns the two fixtures spell identically (DECISION 3's asymmetry
# puts `price` outside that set, deliberately).
# ---------------------------------------------------------------------------
ident="select name, qty, code from %s where region == 'eu' order by qty desc, name asc limit 2"
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
refuses 'inexact /'         '`/` is exact-only' \
        'select name, qty / 5 as h from sales.csv'
refuses 'decimal /'         '`/` on a decimal has no exact result' \
        'select name, price / qty as u from sales.csv'
refuses 'no truthiness'     '`where` needs a boolean' \
        'select name from sales.csv where qty'
refuses 'json float'        'JSON numbers here must be integers' \
        'select * from float.json'
# `[}]`: the old OOM. The message is the corelib's own -- `}` begins no value --
# and the byte offset is the point: it names the `}` at 1, not the document.
refuses 'json bad byte'     'spin.json: byte 1: byte begins no JSON value' \
        'select * from spin.json'

if [ "$fail" -eq 0 ]; then
    echo "tycho-q: green (31-query transcript == golden; select * over 2 fixtures byte-identical to the input; CSV and JSON agree under cmp; malformed query, missing file, unknown column, bad comparison, inexact / and both core:json parse errors all refused with empty stdout)"
else
    echo "tycho-q: FAIL"; exit 1
fi
