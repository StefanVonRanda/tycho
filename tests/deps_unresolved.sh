#!/bin/sh
# Spec §28.6: a `deps` name pkg-config cannot resolve prints a diagnostic (a
# warning naming the deps file and line) and the build goes on; a missing
# library would then surface at the cc stage. It used to be skipped silently.
set -eu
cd "$(dirname "$0")/.."
d="tests/pkg/.deps-unresolved-$$"
trap 'rm -rf "$d"' EXIT HUP INT TERM
mkdir -p "$d/lib"
cat >"$d/main.ty" <<'TY'
package main
import "lib"

fn main():
    println(str(lib.two()))
TY
cat >"$d/lib/lib.ty" <<'TY'
package lib

fn two() -> int:
    return 2
TY
printf '# a name no host provides\ntycho-no-such-pkgconfig-name\n' >"$d/lib/deps"
log="$d/compiler.log"
if ! TYCHO_CORELIB="$PWD/corelib" ./tychoc1 "$d/main.ty" -o "$d/prog" >"$log" 2>&1; then
    echo "deps-unresolved: the build failed, but nothing in it needs the missing library"
    cat "$log"; exit 1
fi
grep -Fq "lib/deps:2: warning: pkg-config cannot resolve 'tycho-no-such-pkgconfig-name'" "$log" || {
    echo "deps-unresolved: no diagnostic for an unresolvable deps name"
    cat "$log"; exit 1
}
[ "$("$d/prog")" = 2 ] || { echo "deps-unresolved: the program did not run"; exit 1; }
echo "deps-unresolved: an unresolvable deps name is reported and the build goes on"
