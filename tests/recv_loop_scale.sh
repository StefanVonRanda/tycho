#!/bin/sh
# Repeated composite recv payloads must be reclaimed by the iteration arena.
set -eu
cd "$(dirname "$0")/.."
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT HUP INT TERM
for cc in tychoc tychoc1; do
    ./$cc tests/recv_loop_scale.ty -o "$t/$cc" >/dev/null
    (ulimit -v 98304; "$t/$cc") >"$t/$cc.out"
    cmp tests/recv_loop_scale.out "$t/$cc.out"
done
echo "recv-loop-scale: 100,000 composite receives stay bounded under both compilers"
