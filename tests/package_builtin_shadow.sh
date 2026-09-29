#!/bin/sh
# Package-local functions with builtin names win inside their package in both compilers.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir "$tmp/lib"
cat >"$tmp/lib/lib.ty" <<'TY'
package lib

fn pow(x: float, y: float) -> float:
    return x + y

fn answer() -> float:
    return pow(1.0, 2.0)
TY
cat >"$tmp/main.ty" <<'TY'
package main
import "lib"

fn main():
    println(str(lib.answer()))
TY
for compiler in ./tychoc ./tychoc1; do
    name=$(basename "$compiler")
    "$compiler" "$tmp/main.ty" -o "$tmp/$name" >"$tmp/$name.out" 2>"$tmp/$name.err" || {
        echo "package-builtin-shadow: $compiler failed to compile" >&2
        cat "$tmp/$name.err" >&2
        exit 1
    }
    grep -Fq '`pow` collides with the builtin of the same name -- unqualified calls in this package select this procedure.' "$tmp/$name.err" || {
        echo "package-builtin-shadow: $compiler emitted the wrong collision advice" >&2
        cat "$tmp/$name.err" >&2
        exit 1
    }
    if grep -q "pow.*never called" "$tmp/$name.err"; then
        echo "package-builtin-shadow: $compiler treated the package-local pow as unused" >&2
        exit 1
    fi
    got=$("$tmp/$name")
    [ "$got" = "3.0" ] || {
        echo "package-builtin-shadow: $compiler selected the builtin (got '$got')" >&2
        exit 1
    }
done
echo "package-builtin-shadow: both compilers select package-local pow and give accurate advice"
