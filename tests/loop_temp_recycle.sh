#!/bin/sh
# Bound loop-lifetime allocations for repeated string-element replacement and
# transient variadic packs under both code generators.
set -eu
cd "$(dirname "$0")/.."
t="$(mktemp -d)"
trap 'rm -rf "$t"' EXIT HUP INT TERM
for cc in tychoc tychoc1; do
    ./$cc tests/loop_temp_recycle.ty -o "$t/$cc" >/dev/null
    (ulimit -v 98304 2>/dev/null || true; "$t/$cc") >"$t/$cc.out"
    cmp tests/loop_temp_recycle.out "$t/$cc.out" || {
        echo "loop-temp-recycle: $cc output mismatch" >&2
        exit 1
    }
done
echo "loop-temp-recycle: repeated string stores and variadic packs stay bounded under both compilers"
