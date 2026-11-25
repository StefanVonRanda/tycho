#!/bin/sh
# result.expect / expect_some (FRICTION 245): the value, or `msg: error` on stderr
# and exit 1. A package of its own: it imports core:result, which tests/abort's
# single-file fixtures cannot.
set -u
cd "$(dirname "$0")/.."
T=$(mktemp -d)
mkdir -p "$T/p" && cp tests/result_expect/main.ty "$T/p/main.ty"
TYCHO_CORELIB="$PWD/corelib" ./tychoc "$T/p/main.ty" -o "$T/m" > "$T/build.log" 2>&1 || { cat "$T/build.log"; echo "FAIL result_expect: did not build"; exit 1; }
"$T/m" > "$T/out" 2> "$T/err"; rc=$?
if [ $rc -ne 1 ] || [ "$(cat "$T/out")" != "5" ] || ! cmp -s "$T/err" tests/result_expect/expected.err; then
    echo "FAIL result_expect: exit $rc, stdout '$(cat "$T/out")', stderr '$(cat "$T/err")'"; rm -rf "$T"; exit 1
fi
rm -rf "$T"
echo "ok    result_expect: the value, or the message and exit 1"
