#!/bin/sh
# The checked-in bootstrap C builds the compiler and reproduces itself.
set -eu
cd "$(dirname "$0")/.."

CC="${CC:-cc}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT INT TERM
SRC=compiler/main.ty

shims="corelib/strings/strings_shim.c corelib/os/os_shim.c corelib/io/io_shim.c"
$CC -O2 -fwrapv -std=c11 -pthread -Icorelib -o "$T/bootstrap" \
    bootstrap/tycho_compiler.c $shims -lm \
    || { echo "bootstrap-check: checked-in C does not build"; exit 1; }

TYCHO_CORELIB="$PWD/corelib" "$T/bootstrap" "$SRC" --emit-c -o "$T/gen" \
    >"$T/emit.log" 2>&1 \
    || { echo "bootstrap-check: bootstrap compiler cannot emit the compiler"; tail -5 "$T/emit.log"; exit 1; }

cmp -s bootstrap/tycho_compiler.c "$T/gen.c" \
    || { echo "bootstrap-check: emitted C differs from the checked-in bootstrap"; exit 1; }

TYCHO_CORELIB="$PWD/corelib" "$T/bootstrap" --version | grep -q '^tychoc ' \
    || { echo "bootstrap-check: bootstrap compiler does not report a version"; exit 1; }

echo "bootstrap-check: ok (checked-in C builds, emits itself byte-for-byte)"
