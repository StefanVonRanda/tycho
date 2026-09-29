#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -O2 -DZD_MAX_OUT=65536 \
    corelib/compress/compress_shim.c tests/compress_limits.c -lz -o "$t/compress-limits"
"$t/compress-limits"
