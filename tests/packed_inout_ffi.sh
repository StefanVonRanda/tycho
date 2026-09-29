#!/bin/sh
# The packed-field bounce must also cover extern inout arguments.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cat >"$tmp/ffi.ty" <<'TY'
packed struct Record:
    tag: u8
    count: i64

extern fn c_bump(value: inout i64)

fn main():
    r := Record(to_u8(1), to_i64(9))
    c_bump(&r.count)
    println(str(r.count))
TY
cat >"$tmp/shim.c" <<'C'
#include <stdint.h>
void c_bump(int64_t *value) { ++*value; }
C
for compiler in tychoc tychoc1; do
    TYCHO_CFLAGS='-O2 -fsanitize=undefined -fno-sanitize-recover=all -Werror=address-of-packed-member' \
        ./$compiler "$tmp/ffi.ty" --shim "$tmp/shim.c" -o "$tmp/$compiler"
    got=$("$tmp/$compiler")
    [ "$got" = 10 ] || { echo "packed-inout-ffi: $compiler printed '$got'"; exit 1; }
done
echo "packed-inout-ffi: extern inout on a packed field is aligned and copies back"
