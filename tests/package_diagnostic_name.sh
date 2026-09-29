#!/bin/sh
# Keep package-qualified generic diagnostics source-spelled in both compilers.
set -eu
cd "$(dirname "$0")/.."
d="tests/pkg/.diagnostic-name-$$"
trap 'rm -rf "$d"' EXIT HUP INT TERM
mkdir -p "$d"
cat >"$d/main.ty" <<'TY'
package main

import "core:math"

fn main():
    v: vector[4]f32 = [1.0, 2.0, 3.0, 4.0]
    math.clamp(v, [-3.0, 0.0, 0.0, 0.0], [255.0, 255.0, 255.0, 255.0])
TY
for compiler in tychoc tychoc1; do
    log="$d/$compiler.log"
    if TYCHO_CORELIB="$PWD/corelib" ./$compiler "$d/main.ty" --emit-c -o "$d/out" >"$log" 2>&1; then
        echo "package-diagnostic-name: $compiler accepted a mismatched parameter"
        exit 1
    fi
    if [ "$compiler" = tychoc ]; then name=math.clamp; else name=clamp; fi
    grep -Fq "argument 2 of '$name' is [float], which does not fit the parameter pattern" "$log" || {
        echo "package-diagnostic-name: $compiler did not use a source-spelled function name"
        cat "$log"
        exit 1
    }
done
echo "package-diagnostic-name: package function diagnostics use source names in both compilers"
