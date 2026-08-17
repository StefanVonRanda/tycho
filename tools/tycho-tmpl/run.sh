set -u
cd "$(dirname "$0")/../.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
RECORD="${RECORD:-0}"
golden="tools/tycho-tmpl/tmpl.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
note() { echo "FAIL $1"; fail=1; }

$TYCHOC -o "$T/tmpl" tools/tycho-tmpl/main.ty > "$T/build.log" 2>&1 || {
    echo "tmpl-check: FAILED (tycho-tmpl does not build)"; tail -3 "$T/build.log"; exit 1; }

printf 'hello {{name}}\nyou are {{role}}\nbye {{name}}\n' > "$T/t.tmpl"

# [1] two runs, identical, first equal to the golden
timeout 10 "$T/tmpl" --tmpl "$T/t.tmpl" --set name=ada --set role=admin > "$T/one.txt" 2>&1 || note "[1] first run exited non-zero"
timeout 10 "$T/tmpl" --tmpl "$T/t.tmpl" --set name=ada --set role=admin > "$T/two.txt" 2>&1 || note "[1] second run exited non-zero"
cmp -s "$T/one.txt" "$T/two.txt" || note "[1] two runs printed different output"

if [ "$RECORD" = 1 ]; then
    cp "$T/one.txt" "$golden"; echo "rec  $golden"
else
    [ -f "$golden" ] || { echo "tmpl-check: FAILED (no golden -- run RECORD=1)"; exit 1; }
    cmp -s "$T/one.txt" "$golden" || { note "[1] output differs from the golden"; diff "$golden" "$T/one.txt" | head -8; }
fi

# [2] the substitution, against literals
cat > "$T/want.txt" <<'WANT'
hello ada
you are admin
bye ada
-- rendered 3 line(s)
WANT
cmp -s "$T/one.txt" "$T/want.txt" || { note "[2] the render is not the expected one"; diff "$T/want.txt" "$T/one.txt" | head -6; }

# [3] the four shapes a `sink` builder cannot have. Each probe is its own package
# directory (a directory IS a package here), compiled with tychoc, and must fail.
probe() {   # probe <name> <body-file-content>
    _n=$1; _src=$2
    mkdir -p "$T/p_$_n"
    printf '%s\n' "$_src" > "$T/p_$_n/main.ty"
    if $TYCHOC -o "$T/p_$_n/x" "$T/p_$_n/main.ty" > "$T/p_$_n/err" 2>&1; then
        note "[3] $_n COMPILED -- the sink consume rule was relaxed; FRICTION #37 needs re-reading"
    elif ! grep -q "sink" "$T/p_$_n/err"; then
        note "[3] $_n failed, but not with a sink diagnostic"; head -1 "$T/p_$_n/err"
    fi
}
_pre='package main
struct D:
    parts: [string]
fn add(d: sink D, s: string) -> D:
    push(d.parts, s)
    return d
fn take(d: sink D) -> int:
    return len(d.parts)
fn of(xs: sink [string]) -> int:
    return len(xs)
fn main():'
probe loop      "$_pre
    d := D([]string)
    for s in [\"a\", \"b\"]:
        d = add(d, s)
    println(str(take(d)))"
probe collected "$_pre
    xs := []string
    push(xs, \"a\")
    println(str(of(xs)))"
probe rebound   "$_pre
    d := D([]string)
    d = add(d, \"a\")
    println(str(take(d)))"
probe observed  "$_pre
    d := D([]string)
    n := len(d.parts)
    println(str(take(d)) + str(n))"

# [4] a missing key is named
timeout 10 "$T/tmpl" --tmpl "$T/t.tmpl" --set name=ada > "$T/miss.txt" 2>&1
[ $? -ne 0 ] || note "[4] a missing key exited 0"
grep -q "no value for {{role}}" "$T/miss.txt" || note "[4] the missing-key message does not name the placeholder"

# [5] an unknown option is refused by name
timeout 10 "$T/tmpl" --tmpl "$T/t.tmpl" --sett x=1 > "$T/unk.txt" 2>&1
[ $? -ne 0 ] || note "[5] an unknown option exited 0"
grep -q -- "--sett" "$T/unk.txt" || note "[5] the unknown-option message does not name the option"

[ "$fail" = 0 ] || { echo "tmpl-check: FAILED"; exit 1; }
echo "tycho-tmpl: green (render identical over 2 runs and equal to the golden; substitution against literals with a repeated key and a counted trailer; all FOUR sink shapes -- accumulate in a loop, collect then consume, create-grow-consume, count before consuming -- still refused with a sink diagnostic; a missing key exits 1 naming the placeholder; an unknown option is refused by name)"
