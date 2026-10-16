#!/bin/sh
# Regression for D72/D80: the self-hosted emitter must append bytes in place
# and find a string accumulator declared inside a match arm.
set -eu
cd "$(dirname "$0")/.."
. ./scripts/shlib.sh
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT HUP INT TERM
for compiler in ./tychoc ./tychoc1; do
    name=$(basename "$compiler")
    TYCHO_CORELIB="$PWD/corelib" "$compiler" tests/accumulator_scope.ty -o "$t/$name" >/dev/null
    (ulimit -v 1200000 2>/dev/null || true; timeout 60 "$t/$name") >"$t/$name.out"
    cmp tests/accumulator_scope.out "$t/$name.out"
done
echo "accumulator-scope: bytes and match-arm strings append in place under both compilers"
