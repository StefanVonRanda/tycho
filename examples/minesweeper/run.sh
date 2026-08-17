set -u
cd "$(dirname "$0")"
TYCHOC="${TYCHOC:-../../tychoc}"
[ -x "$TYCHOC" ] || { echo "no $TYCHOC -- run 'make' at the repo root first"; exit 2; }
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

"$TYCHOC" mine.ty --shim mine_shim.c -o "$TMP/mine" || { echo "build failed"; exit 1; }
"$TMP/mine" > "$TMP/out.txt"
if diff -q mine.out "$TMP/out.txt" >/dev/null; then
    echo "ok   minesweeper (deterministic demo matches golden)"
else
    echo "FAIL minesweeper: output != golden"; diff mine.out "$TMP/out.txt" | head; exit 1
fi
echo "     interactive:  $TYCHOC mine.ty --shim mine_shim.c -o mine && ./mine play"
