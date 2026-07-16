#!/bin/sh
# weblog -- build with BOTH compilers, run the no-argument demo, and assert each
# matches the golden (and each other). Re-record: RECORD=1 sh examples/weblog/run.sh
# Not wired into `make ci`.
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

# tychoc0 (self-hosted), built fresh from source the way the fixpoint does
$TYCHOC compiler/tychoc0.ty -o "$T/tychoc0" 2>/dev/null || { echo "weblog: could not build tychoc0"; exit 1; }
"$T/tychoc0" "$D/main.ty" > "$T/wl0.c" 2>"$T/err0" || { echo "weblog: tychoc0 BUILD FAILED"; cat "$T/err0"; exit 1; }
$CC -O2 "$T/wl0.c" -o "$T/wl_0" -lm 2>/dev/null || { echo "weblog: tychoc0-emitted C did not compile"; exit 1; }
"$T/wl_0" > "$T/out_0"

fail=0
diff -u "$D/expected.out" "$T/out_c" || { echo "weblog: tychoc output differs from golden"; fail=1; }
cmp -s "$T/out_c" "$T/out_0" || { echo "weblog: tychoc0 differs from tychoc"; fail=1; }
[ $fail -eq 0 ] && echo "weblog: ok (tychoc == tychoc0 == golden)" || exit 1
