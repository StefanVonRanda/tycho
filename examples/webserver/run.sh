set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root (so content paths resolve)
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
export TYCHO_CORELIB="$PWD/corelib"
D=examples/webserver
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

$TYCHOC "$D/main.ty" -o "$T/srv_c" 2>"$T/err" || { echo "webserver: tychoc BUILD FAILED"; cat "$T/err"; exit 1; }
"$T/srv_c" > "$T/out_c"                                # no args -> self-test

if [ "${RECORD:-0}" = "1" ]; then
    cp "$T/out_c" "$D/expected.out"; echo "webserver: golden recorded ($D/expected.out)"; exit 0
fi

fail=0
diff -u "$D/expected.out" "$T/out_c" || { echo "webserver: tychoc output differs from golden"; fail=1; }
[ $fail -eq 0 ] && echo "webserver: ok (tychoc == golden)" || exit 1
