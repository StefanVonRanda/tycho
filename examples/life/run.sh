set -u
cd "$(dirname "$0")"
TYCHOC="${TYCHOC:-../../tychoc1}"
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
