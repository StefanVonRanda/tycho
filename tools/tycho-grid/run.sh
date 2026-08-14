#!/bin/sh
# Gate for tycho-grid, the integer grid in tools/tycho-grid/.
#
# Re-record the golden with:  RECORD=1 sh tools/tycho-grid/run.sh
#
# WHY THIS LANE EXISTS. Measured 2026-08-14, each of the three features this
# program uses had exactly ONE real consumer in the tree: `subscript`
# (tools/tycho-sim), `bounded[N]T` (tools/tycho-vm) and `# deprecated:`
# (corelib/sort). This is the second consumer of all three.
#
# WHAT A TRANSCRIPT CANNOT SHOW, and what each leg is therefore for:
#   - A SUBSCRIPT is a compile-time place-macro with no runtime object. If it
#     silently degraded from a place to a COPY, every printed number would stay
#     identical and only the write would stop landing. [2] writes through it and
#     reads the write back; [5] pins the five declaration rules that keep it a
#     place at all.
#   - A `bounded[N]T` overflow is a runtime abort, and its capacity is part of
#     the type. [4] asserts the abort by message and exit status, and [6] asserts
#     the by-value copy is independent -- a bounded that started sharing storage
#     would print the same grid.
#   - A DEPRECATION warning goes to stderr at COMPILE time, so it is absent from
#     the golden by construction. [3] reads the build log for it, and asserts the
#     inverse too: a comment that merely mentions the marker must NOT warn, which
#     is the false positive fixed on 2026-08-14 (FRICTION #46).
#
# Every run is bounded by timeout(1).
set -u
cd "$(dirname "$0")/../.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
RECORD="${RECORD:-0}"
golden="tools/tycho-grid/grid.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
note() { echo "FAIL $1"; fail=1; }

$TYCHOC -o "$T/grid" tools/tycho-grid/main.ty > "$T/build.log" 2>&1 || {
    echo "grid-check: FAILED (tycho-grid does not build)"; tail -3 "$T/build.log"; exit 1; }

ARGS="--rows 2 --cols 3 --set 0,0=5 --set 1,2=7 --mark 0,0 --mark 1,2"

# [1] two runs, identical, first equal to the golden
timeout 10 "$T/grid" $ARGS > "$T/one.txt" 2>&1 || note "[1] first run exited non-zero"
timeout 10 "$T/grid" $ARGS > "$T/two.txt" 2>&1 || note "[1] second run exited non-zero"
cmp -s "$T/one.txt" "$T/two.txt" || note "[1] two runs printed different output"

if [ "$RECORD" = 1 ]; then
    cp "$T/one.txt" "$golden"; echo "rec  $golden"
else
    [ -f "$golden" ] || { echo "grid-check: FAILED (no golden -- run RECORD=1)"; exit 1; }
    cmp -s "$T/one.txt" "$golden" || { note "[1] output differs from the golden"; diff "$golden" "$T/one.txt" | head -8; }
fi

# [2] the subscript as a PLACE and as an RVALUE, against literals. The two writes
# land at (0,0) and (1,2); the marks are the FLAT indices r*cols+c, so 0 and 5 --
# a projection that wrote to the wrong cell moves a row line and a row sum
# together, and `legacy` (which never uses the subscript) disagrees with `total`.
# Measured 2026-08-14: transposing the yield to `&g.rows[c][r]` on this NON-SQUARE
# grid is caught one leg earlier, by the runtime bounds check, so [1] reddens
# before [2] does. [2b] is what covers a wrong cell that stays in range.
cat > "$T/want.txt" <<'WANT'
5 0 0 | 5
0 0 7 | 7
total 12
legacy 12
marks 2/4
mark 0
mark 5
WANT
cmp -s "$T/one.txt" "$T/want.txt" || { note "[2] the grid/sums are not the expected ones"; diff "$T/want.txt" "$T/one.txt" | head -8; }

# [2b] the two totals are computed by DIFFERENT code -- `total` reads every cell
# through the subscript, `sum_all` walks the rows directly. Equal is the point:
# a subscript that projected wrongly would separate them.
tot=$(sed -n 's/^total //p' "$T/one.txt"); leg=$(sed -n 's/^legacy //p' "$T/one.txt")
[ "$tot" = "$leg" ] || note "[2b] total=$tot but the direct walk says $leg -- the subscript projects to the wrong cell"

# [3] the deprecation warning is emitted at COMPILE time, naming the fn and text
grep -q '`sum_all` is deprecated:' "$T/build.log" || { note "[3] no deprecation warning for sum_all"; head -3 "$T/build.log"; }
grep -q 'predates the subscript' "$T/build.log" || note "[3] the deprecation warning does not carry its text"

# [3b] THE INVERSE: a comment that merely MENTIONS the marker must not warn.
# This is FRICTION #46 -- the scan matched the substring anywhere in the line, so
# ordinary prose deprecated the fn below it and every caller got a phantom
# warning. Re-recording the golden cannot see this: it is a stderr diagnostic.
mkdir -p "$T/prose"
cat > "$T/prose/main.ty" <<'PROSE'
# this replaces the old deprecated: thing we removed
fn ordinary(x: int) -> int:
    return x + 1
fn main():
    println(str(ordinary(1)))
PROSE
$TYCHOC -o "$T/prose/p" "$T/prose/main.ty" > "$T/prose/log" 2>&1
grep -q 'is deprecated' "$T/prose/log" && { note "[3b] a comment that only MENTIONS the marker deprecated the fn below it"; grep -m1 'deprecated' "$T/prose/log"; }

# [3c] a deprecated fn taken as a VALUE warns too -- otherwise one binding
# launders the policy (FRICTION #47).
mkdir -p "$T/fnval"
cat > "$T/fnval/main.ty" <<'FNVAL'
# deprecated: use fresh
fn stale(x: int) -> int:
    return x + 1
fn main():
    f := stale
    println(str(f(1)))
FNVAL
$TYCHOC -o "$T/fnval/p" "$T/fnval/main.ty" > "$T/fnval/log" 2>&1
grep -q '`stale` is deprecated' "$T/fnval/log" || { note "[3c] taking a deprecated fn as a value warns nowhere"; head -2 "$T/fnval/log"; }

# [4] the bounded capacity is enforced at RUN time, by name and non-zero exit
timeout 10 "$T/grid" --rows 2 --cols 3 --mark 0,0 --mark 0,1 --mark 0,2 --mark 1,0 --mark 1,1 > "$T/full.txt" 2>&1
[ $? -ne 0 ] || note "[4] a fifth mark past the bounded[4] capacity exited 0"
grep -q 'at most 4 marks' "$T/full.txt" || { note "[4] the capacity refusal does not name the limit"; head -1 "$T/full.txt"; }

# [5] the five subscript declaration rules, each a probe that must FAIL
probe() {   # probe <name> <decl-body> <text the refusal must contain>
    _n=$1; _b=$2; _w=$3
    _d="$T/s_$_n"; mkdir -p "$_d"
    printf 'struct Graph:\n    nodes: [int]\n%s\n' "$_b" > "$_d/main.ty"
    if $TYCHOC --emit-c -o "$_d/p" "$_d/main.ty" > "$_d/err" 2>&1; then
        note "[5] $_n COMPILED -- a subscript rule stopped being enforced"
    else
        grep -q "$_w" "$_d/err" || { note "[5] $_n refused, but not for its own reason"; head -1 "$_d/err"; }
    fi
}
probe notyield 'subscript e(g: Graph, i: int) -> inout int:
    println("hi")
fn main():
    println("x")'                                     'single `yield &<place>`'
probe nonplace 'subscript e(g: Graph, i: int) -> inout int:
    yield &(i + 1)
fn main():
    println("x")'                                     'rooted in one of its parameters'
probe mismatch 'subscript e(g: Graph, i: int) -> inout string:
    yield &g.nodes[i]
fn main():
    g := Graph([1])
    println(g.e(0))'                                  'inout string'
probe twice 'subscript e(g: Graph, i: int) -> inout int:
    yield &g.nodes[i + i]
fn main():
    println("x")'                                     'used more than once'
probe generic 'struct Pool($T):
    items: [$T]
subscript at(p: Pool($T), i: int) -> inout $T:
    yield &p.items[i]
fn main():
    println("x")'                                     'may not be generic'

# [5b] the FLAT 2-D spelling stays refused, and for the once-per-parameter
# reason. FRICTION #48: `yield &g.cells[r * g.w + c]` reads two fields of the
# receiver, so the natural grid subscript is inexpressible and this program uses
# a nested array instead. Pinned so the limitation cannot lapse unnoticed.
mkdir -p "$T/flat"
cat > "$T/flat/main.ty" <<'FLAT'
struct G:
    w: int
    cells: [int]
subscript at(g: G, r: int, c: int) -> inout int:
    yield &g.cells[r * g.w + c]
fn main():
    println("x")
FLAT
if $TYCHOC --emit-c -o "$T/flat/p" "$T/flat/main.ty" > "$T/flat/err" 2>&1; then
    echo "note [5b] the flat 2-D subscript now COMPILES -- FRICTION #48 was lifted; update the program and this lane"
    fail=1
else
    grep -q "used more than once" "$T/flat/err" || note "[5b] the flat 2-D form is refused, but no longer for the once-per-parameter reason"
fi

# [6] a bounded inside a struct is copied BY VALUE: mutating the copy must not
# move the original. A bounded that started sharing storage prints the same grid.
mkdir -p "$T/val"
cat > "$T/val/main.ty" <<'VAL'
struct Box:
    slots: bounded[3]int
fn main():
    x := Box([1, 2])
    y := x
    push(y.slots, 9)
    println(str(len(x.slots)) + " " + str(len(y.slots)))
VAL
if $TYCHOC -o "$T/val/p" "$T/val/main.ty" > "$T/val/err" 2>&1; then
    out=$(timeout 10 "$T/val/p" 2>&1)
    [ "$out" = "2 3" ] || note "[6] a bounded struct field is not copied by value: got '$out', want '2 3'"
else
    note "[6] the bounded value-copy probe stopped compiling"; head -2 "$T/val/err"
fi

# [7] an unknown option is refused by name
timeout 10 "$T/grid" --rows 2 --cols 2 --marks 0,0 > "$T/unk.txt" 2>&1
[ $? -ne 0 ] || note "[7] an unknown option exited 0"
grep -q -- "--marks" "$T/unk.txt" || note "[7] the unknown-option message does not name the option"

[ "$fail" = 0 ] || { echo "grid-check: FAILED"; exit 1; }
echo "tycho-grid: green (run identical over 2 runs and equal to the golden; the subscript as a place and an rvalue against literals, with two independently-computed totals agreeing; the deprecation warning emitted with its text, NOT emitted for prose that merely mentions the marker, and emitted for a fn taken as a value; a fifth mark past bounded[4] exits non-zero naming the limit; all five subscript rules plus the flat 2-D once-per-parameter limit still refused; a bounded struct field still copies by value; an unknown option refused by name)"
