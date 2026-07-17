#!/bin/sh
# webserver -- build with BOTH compilers, run the deterministic self-test (routes
# dispatched through the pure handler, no socket), and assert each matches the
# golden and each other. Re-record: RECORD=1 sh examples/webserver/run.sh
# For a live server:  ./server --serve   (optionally PORT=8137). Not in `make ci`.
set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root (so content paths resolve)
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
export TYCHO_CORELIB="$PWD/corelib"
D=examples/webserver
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
SHIMS="corelib/net/net_shim.c corelib/io/io_shim.c"   # core:net + core:io (streaming reader)

$TYCHOC "$D/main.ty" -o "$T/srv_c" 2>"$T/err" || { echo "webserver: tychoc BUILD FAILED"; cat "$T/err"; exit 1; }
"$T/srv_c" > "$T/out_c"                                # no args -> self-test

if [ "${RECORD:-0}" = "1" ]; then
    cp "$T/out_c" "$D/expected.out"; echo "webserver: golden recorded ($D/expected.out)"; exit 0
fi

$TYCHOC compiler/tychoc0.ty -o "$T/tychoc0" 2>/dev/null || { echo "webserver: could not build tychoc0"; exit 1; }
"$T/tychoc0" "$D/main.ty" > "$T/s0.c" 2>"$T/e0" || { echo "webserver: tychoc0 BUILD FAILED"; cat "$T/e0"; exit 1; }
$CC -O2 "$T/s0.c" $SHIMS -o "$T/srv_0" -lm 2>/dev/null || { echo "webserver: tychoc0-emitted C did not compile"; exit 1; }
"$T/srv_0" > "$T/out_0"

fail=0
diff -u "$D/expected.out" "$T/out_c" || { echo "webserver: tychoc output differs from golden"; fail=1; }
cmp -s "$T/out_c" "$T/out_0" || { echo "webserver: tychoc0 differs from tychoc"; fail=1; }
[ $fail -eq 0 ] && echo "webserver: ok (tychoc == tychoc0 == golden)" || exit 1
