set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "tycho-chess: no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
RECORD="${RECORD:-0}"
golden="$PWD/tools/tycho-chess/expected.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL: $*"; fail=1; }

CHESS="$T/tycho-chess"
if ! "$TYCHOC" tools/tycho-chess/main.ty -o "$CHESS" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/build.log"
    echo "tycho-chess: FAIL"; exit 1
fi

W="$T/w"; mkdir -p "$W"; cd "$W" || exit 2
out="$T/all.out"
: > "$out"

START="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
KIWI="r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"
POS3="8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"
CASTLE="r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"
KEND="8/8/8/8/8/8/8/k6K w - - 0 1"
PAWNRACE="8/8/8/8/8/8/P7/K6k w - - 0 1"
EP="rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2"
PROMO="1n2k2n/1P6/8/8/8/8/6p1/2K5 w - - 0 1"

# assert_total <fen> <depth> <want> <label> -- parse the `total N` line out of
# `perft`'s divide output and compare. A moved total reddens the label.
assert_total() {
    _fen=$1; _depth=$2; _want=$3; _lbl=$4
    _got=$("$CHESS" perft "$_depth" "$_fen" 2>/dev/null | sed -n 's/^total //p')
    if [ "$_got" != "$_want" ]; then
        bad "$_lbl perft($_depth): got $_got, want $_want"
    fi
}

assert_total "$START"  1 20        start
assert_total "$START"  2 400       start
assert_total "$START"  3 8902      start
assert_total "$START"  4 197281    start
assert_total "$START"  5 4865609   start
assert_total "$KIWI"   1 48        kiwipete
assert_total "$KIWI"   2 2039      kiwipete
assert_total "$KIWI"   3 97862     kiwipete
assert_total "$KIWI"   4 4085603   kiwipete
assert_total "$POS3"   1 14        pos3
assert_total "$POS3"   2 191       pos3
assert_total "$POS3"   3 2812      pos3
assert_total "$POS3"   4 43238     pos3
assert_total "$POS3"   5 674624    pos3
assert_total "$POS3"   6 11030083  pos3
assert_total "$CASTLE" 1 26        castle
assert_total "$CASTLE" 2 568       castle
assert_total "$CASTLE" 3 13744     castle
assert_total "$KEND"   1 3         kend
assert_total "$KEND"   2 9         kend
assert_total "$KEND"   3 54        kend
assert_total "$PAWNRACE" 1 4       pawnrace
assert_total "$PAWNRACE" 2 12      pawnrace
assert_total "$PAWNRACE" 3 69      pawnrace
assert_total "$EP"     1 29        ep
assert_total "$EP"     2 835       ep
assert_total "$EP"     3 24825     ep
assert_total "$PROMO"  1 5         promo
assert_total "$PROMO"  2 70        promo
assert_total "$PROMO"  3 506       promo

# ---------------------------------------------------------------------------
# [2] the golden transcript -- the per-root divide at the deep depths
# ---------------------------------------------------------------------------
printf '=== start d5\n' >> "$out"
"$CHESS" perft 5 "$START" >> "$out" 2>/dev/null
printf '=== kiwipete d4\n' >> "$out"
"$CHESS" perft 4 "$KIWI" >> "$out" 2>/dev/null
printf '=== pos3 d6\n' >> "$out"
"$CHESS" perft 6 "$POS3" >> "$out" 2>/dev/null
printf '=== castling d3\n' >> "$out"
"$CHESS" perft 3 "$CASTLE" >> "$out" 2>/dev/null
printf '=== ep d3\n' >> "$out"
"$CHESS" perft 3 "$EP" >> "$out" 2>/dev/null
printf '=== promo d3\n' >> "$out"
"$CHESS" perft 3 "$PROMO" >> "$out" 2>/dev/null

SCHOLAR="r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5Q2/PPPP1PPP/RNB1K1NR w KQkq - 0 1"
RXC2="k7/8/8/8/8/8/1Rq5/K7 w - - 0 1"
HANGQ="k7/8/8/8/8/8/1q6/KR6 w - - 0 1"

# search_pair <fen> <depth> <label> -- determinism + TT-invariance, and the
# first run's transcript goes to the golden.
search_pair() {
    _fen=$1; _depth=$2; _lbl=$3
    "$CHESS" search "$_depth" "$_fen" > "$W/s1" 2>"$T/e" || {
        bad "$_lbl search exited non-zero"; sed 's/^/      /' "$T/e"; return
    }
    "$CHESS" search "$_depth" "$_fen" > "$W/s2" 2>/dev/null
    cmp -s "$W/s1" "$W/s2" || bad "$_lbl search is not deterministic"
    "$CHESS" search-nott "$_depth" "$_fen" > "$W/sn" 2>/dev/null
    _t=$(grep '^best' "$W/s1")
    _n=$(grep '^best' "$W/sn")
    if [ "$_t" != "$_n" ]; then
        bad "$_lbl TT changed the answer: search='$_t' nott='$_n'"
    fi
    printf '=== search %s d%s\n' "$_lbl" "$_depth" >> "$out"
    cat "$W/s1" >> "$out"
}

search_pair "$START"   5 start
search_pair "$KIWI"    4 kiwipete
search_pair "$POS3"    5 pos3
search_pair "$SCHOLAR" 3 scholar
search_pair "$RXC2"    3 rxc2

# the exact probes
_g=$("$CHESS" search-nott 3 "$RXC2" 2>/dev/null | grep -E '^b2c2')
if [ "$_g" != "b2c2 510" ]; then bad "rxc2 probe: got '$_g', want 'b2c2 510'"; fi
_g=$("$CHESS" search-nott 2 "$HANGQ" 2>/dev/null | grep '^best')
if [ "$_g" != "best b1b2 510" ]; then bad "hanging-queen probe: got '$_g', want 'best b1b2 510'"; fi
_g=$("$CHESS" search-nott 3 "$SCHOLAR" 2>/dev/null | grep '^best')
if [ "$_g" != "best f3f7 100000" ]; then bad "scholar probe: got '$_g', want 'best f3f7 100000'"; fi

parsearch_pair() {
    _fen=$1; _depth=$2; _lbl=$3
    "$CHESS" parsearch "$_depth" "$_fen" > "$W/p1" 2>"$T/e" || {
        bad "$_lbl parsearch exited non-zero"; sed 's/^/      /' "$T/e"; return
    }
    "$CHESS" parsearch "$_depth" "$_fen" > "$W/p2" 2>/dev/null
    cmp -s "$W/p1" "$W/p2" || bad "$_lbl parsearch is not deterministic"
    _p=$(grep '^best' "$W/p1")
    _s=$("$CHESS" search "$_depth" "$_fen" 2>/dev/null | grep '^best')
    if [ "$_p" != "$_s" ]; then
        bad "$_lbl parallel != serial: par='$_p' ser='$_s'"
    fi
    printf '=== parsearch %s d%s\n' "$_lbl" "$_depth" >> "$out"
    cat "$W/p1" >> "$out"
}

parsearch_pair "$START"   5 start
parsearch_pair "$POS3"    5 pos3
parsearch_pair "$SCHOLAR" 3 scholar

if [ "$RECORD" = 1 ]; then
    if [ "$fail" -ne 0 ]; then echo "FAIL: refusing to RECORD from a failed run"; exit 1; fi
    cp "$out" "$golden"; echo "rec  tycho-chess"
fi
if [ ! -f "$golden" ]; then
    bad "no golden -- run RECORD=1 sh tools/tycho-chess/run.sh"
elif ! cmp -s "$out" "$golden"; then
    bad "transcript != golden"; diff "$golden" "$out" | sed 's/^/      /'
fi

if [ "$fail" -eq 0 ]; then
    echo "tycho-chess: green (24 published/oracle perft totals; divides at start d5/kiwipete d4/pos3 d6 + 3 edge divides == golden; 5 search positions deterministic + TT-invariant, 3 tactical probes exact; 3 parsearch positions parallel==serial, deterministic)"
else
    echo "tycho-chess: FAIL"; exit 1
fi
