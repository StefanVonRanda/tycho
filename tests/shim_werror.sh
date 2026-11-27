#!/bin/sh
# FRICTION 268: a shim's C warning that is always a bug is an error. A
# package's shim passing an int64_t* where a double* is taken must not build;
# the same shim with the right pointer builds and runs.
set -eu
cd "$(dirname "$0")/.."
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT HUP INT TERM
mkdir -p "$T/p/lib"
printf 'package main\nimport "./lib"\n\nfn main():\n    println(str(lib.first()))\n' > "$T/p/main.ty"
printf 'package lib\n\nextern fn lib_first() -> int\n\nfn first() -> int:\n    return lib_first()\n' > "$T/p/lib/lib.ty"
cat > "$T/p/lib/lib_shim.c" <<'C'
#include <stdint.h>
static double take(const double *p) { return p[0]; }
int64_t lib_first(void) {
    int64_t xs[1] = { 7 };
    return (int64_t)take(xs);
}
C
if ./tychoc "$T/p/main.ty" -o "$T/bad" > "$T/bad.log" 2>&1; then
    echo "shim-werror: a shim passing int64_t* for double* built"; cat "$T/bad.log"; exit 1
fi
grep -q "incompatible-pointer-types" "$T/bad.log" || { echo "shim-werror: refused, but not for the pointer"; cat "$T/bad.log"; exit 1; }
sed -i.bak 's/int64_t xs\[1\] = { 7 };/double xs[1] = { 7 };/' "$T/p/lib/lib_shim.c"
./tychoc "$T/p/main.ty" -o "$T/good" > "$T/good.log" 2>&1 || { echo "shim-werror: the right shim did not build"; cat "$T/good.log"; exit 1; }
[ "$("$T/good")" = "7" ] || { echo "shim-werror: the right shim printed $("$T/good")"; exit 1; }
echo "shim-werror: a shim's wrong pointer type is refused at the build; the right one builds"
