#!/bin/sh
# Build examples/snake and check the deterministic scripted demo against the golden.
# The interactive mode (`./snake play`) needs raw-terminal input and isn't auto-checked.
# FFI-shim example, so it has its own runner (the main tests/run.sh can't link a shim).
set -u
cd "$(dirname "$0")"
TYCHOC="${TYCHOC:-../../tychoc}"
[ -x "$TYCHOC" ] || { echo "no $TYCHOC -- run 'make' at the repo root first"; exit 2; }
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

"$TYCHOC" snake.ty --shim snake_shim.c -o "$TMP/snake" || { echo "build failed"; exit 1; }
"$TMP/snake" > "$TMP/out.txt"
if diff -q snake.out "$TMP/out.txt" >/dev/null; then
    echo "ok   snake (deterministic demo matches golden)"
else
    echo "FAIL snake: output != golden"; diff snake.out "$TMP/out.txt" | head; exit 1
fi
echo "     interactive:  $TYCHOC snake.ty --shim snake_shim.c -o snake && ./snake play"
