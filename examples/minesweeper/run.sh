#!/bin/sh
# Build examples/minesweeper and check the deterministic demo against the golden.
# The interactive mode (`./mine play`) needs raw-terminal input and isn't checked.
# FFI-shim example, so it has its own runner (the main tests/run.sh can't link a shim).
set -u
cd "$(dirname "$0")"
HIERC="${HIERC:-../../hierc}"
[ -x "$HIERC" ] || { echo "no $HIERC -- run 'make' at the repo root first"; exit 2; }
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

"$HIERC" mine.hi --shim mine_shim.c -o "$TMP/mine" || { echo "build failed"; exit 1; }
"$TMP/mine" > "$TMP/out.txt"
if diff -q mine.out "$TMP/out.txt" >/dev/null; then
    echo "ok   minesweeper (deterministic demo matches golden)"
else
    echo "FAIL minesweeper: output != golden"; diff mine.out "$TMP/out.txt" | head; exit 1
fi
echo "     interactive:  $HIERC mine.hi --shim mine_shim.c -o mine && ./mine play"
