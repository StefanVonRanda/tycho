#!/bin/sh
# weblog -- build with tychoc, run the no-argument demo, diff against the golden.
# Re-record the golden with:  RECORD=1 sh examples/weblog/run.sh
# tychoc-only (the self-hosted tychoc0 has a parse gap on this program -- see
# README.md "Dogfood findings"); not wired into `make ci`.
set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
D=examples/weblog
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

$TYCHOC "$D/main.ty" -o "$T/weblog" 2>"$T/err" || { echo "weblog: BUILD FAILED"; cat "$T/err"; exit 1; }
"$T/weblog" > "$T/out"                                # no args -> embedded demo log

if [ "${RECORD:-0}" = "1" ]; then
    cp "$T/out" "$D/expected.out"; echo "weblog: golden recorded ($D/expected.out)"; exit 0
fi
if diff -u "$D/expected.out" "$T/out"; then
    echo "weblog: ok (output matches golden)"
else
    echo "weblog: FAIL (output differs from golden)"; exit 1
fi
