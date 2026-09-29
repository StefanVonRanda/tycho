#!/bin/sh
# Replacing one map[string]string value must recycle the old payload in the
# owning arena instead of retaining every value until function exit.
set -eu
cd "$(dirname "$0")/.."
t="$(mktemp -d)"
trap 'rm -rf "$t"' EXIT HUP INT TERM
for cc in tychoc tychoc1; do
    ./$cc tests/map_overwrite_scale.ty -o "$t/$cc" >/dev/null
    (ulimit -v 98304; "$t/$cc") >"$t/$cc.out"
    cmp tests/map_overwrite_scale.out "$t/$cc.out" || {
        echo "map-overwrite-scale: $cc output mismatch" >&2
        exit 1
    }
done
echo "map-overwrite-scale: same-key string replacements stay bounded under both compilers"
