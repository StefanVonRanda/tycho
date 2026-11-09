#!/bin/sh
# Regression for D12: -g must quote the source path in emitted #line directives.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

src="$tmp/back\\slash/quote\"name.ty"
nl='
'
src_nl="$tmp/line${nl}break.ty"
mkdir -p "$(dirname "$src")"
printf 'fn main():\n    println("debug-path-ok")\n' >"$src"
printf 'fn main():\n    println("debug-path-ok")\n' >"$src_nl"

check() {
    compiler=$1
    source=$2
    label=$3
    cc=${4:-}
    exe="$tmp/$(basename "$compiler")-$label-${cc:-default}"
    if [ -n "$cc" ]; then
        "$compiler" "$source" -g --cc "$cc" -o "$exe"
    else
        "$compiler" "$source" -g -o "$exe"
    fi
    got=$("$exe")
    [ "$got" = debug-path-ok ] || { echo "$compiler -g printed '$got'"; exit 1; }
}

for compiler in ./tychoc ./tychoc1; do
    check "$compiler" "$src" quoted
    check "$compiler" "$src_nl" newline
    if command -v clang >/dev/null 2>&1; then
        check "$compiler" "$src" quoted clang
        check "$compiler" "$src_nl" newline clang
    fi
done

pkg="$tmp/pkg"
mkdir -p "$pkg/lib"
printf 'package main\nimport "./lib"\nfn main():\n    println(lib.hi())\n' >"$pkg/main.ty"
printf 'package lib\nfn hi() -> string:\n    return "pkg-dbg-ok"\n' >"$pkg/lib/lib.ty"
./tychoc "$pkg/main.ty" -g -o "$pkg/out"
[ "$("$pkg/out")" = pkg-dbg-ok ] || { echo "package -g build printed the wrong output"; exit 1; }
./tychoc "$pkg/main.ty" -g --emit-c -o "$pkg/emit"
grep -q '#line .*lib/lib.ty' "$pkg/emit.c" || { echo "package -g dropped lib/lib.ty line info"; exit 1; }
grep -q '#line .*main.ty' "$pkg/emit.c" || { echo "package -g dropped main.ty line info"; exit 1; }
echo "debug-path: quoted, backslash and newline source paths and package -g line info passed"
