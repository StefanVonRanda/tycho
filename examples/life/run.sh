#!/bin/sh
# Build examples/life and check the deterministic run against the golden. The
# animation (`./life animate`) is interactive (ANSI + sleep) and isn't auto-checked.
# Mirrors examples/sqlite: an FFI-shim example, so it lives in its own dir with its
# own runner rather than the main tests/run.sh harness (which can't link a shim).
set -u
cd "$(dirname "$0")"
TYCHOC="${TYCHOC:-../../tychoc}"
[ -x "$TYCHOC" ] || { echo "no $TYCHOC -- run 'make' at the repo root first"; exit 2; }
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

"$TYCHOC" life.ty --shim life_shim.c -o "$TMP/life" || { echo "build failed"; exit 1; }
"$TMP/life" > "$TMP/out.txt"
if diff -q life.out "$TMP/out.txt" >/dev/null; then
    echo "ok   life (deterministic output matches golden)"
else
    echo "FAIL life: output != golden"; diff life.out "$TMP/out.txt" | head; exit 1
fi
echo "     live animation:  $TYCHOC life.ty --shim life_shim.c -o life && ./life animate"
