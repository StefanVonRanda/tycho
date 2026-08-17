set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
export TYCHO_CORELIB="$PWD/corelib"
D=examples/weblog
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

# tychoc (C reference compiler)
$TYCHOC "$D/main.ty" -o "$T/wl_c" 2>"$T/err" || { echo "weblog: tychoc BUILD FAILED"; cat "$T/err"; exit 1; }
"$T/wl_c" > "$T/out_c"                                # no args -> embedded demo log

if [ "${RECORD:-0}" = "1" ]; then
    cp "$T/out_c" "$D/expected.out"; echo "weblog: golden recorded ($D/expected.out)"; exit 0
fi

fail=0
diff -u "$D/expected.out" "$T/out_c" || { echo "weblog: tychoc output differs from golden"; fail=1; }
[ $fail -eq 0 ] && echo "weblog: ok (tychoc == golden; the tychoc0 leg was retired 2026-07-29)" || exit 1
