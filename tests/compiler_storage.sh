#!/bin/sh
# Exercise the actual driver/emitter API with a deep snapshot and repeated use.
set -eu
cd "$(dirname "$0")/.."
export TYCHO_CORELIB="$PWD/corelib"
mkdir -p build
T=$(mktemp -d "$PWD/build/compiler-storage.XXXXXX")
trap 'rm -rf "$T"' EXIT HUP INT TERM
cp tests/fixtures/compiler_program_storage.src "$T/probe.ty"
for tc in ./tychoc ./tychoc1; do
    "$tc" "$T/probe.ty" -o "$T/probe" >"$T/build.log" 2>&1 || {
        tail -12 "$T/build.log"; exit 1;
    }
    for input in tests/inout_string.ty compiler/main.ty; do
        "$T/probe" "$input" "$T/first.c" "$T/second.c" >"$T/out" 2>"$T/err" || {
            tail -12 "$T/err"; exit 1;
        }
        grep -qxF 'compiler storage: immutable Program' "$T/out"
        cmp "$T/first.c" "$T/second.c"
        ./tychoc1 "$input" --emit-c -o "$T/reference" >"$T/ref.out" 2>"$T/ref.err"
        cmp "$T/first.c" "$T/reference.c"
    done
done
echo 'compiler storage: both compilers preserve a loaded Program and repeat identical emission'
