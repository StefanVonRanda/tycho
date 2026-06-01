#!/bin/sh
# Prong B — head-to-head memory benchmark. The same binary-trees program
# (Computer Language Benchmarks Game) in Hier, C, Rust, and Go, built at -O,
# run under bench/peakrss (peak RSS + wall time). All five binaries must print
# byte-identical output (the cross-language correctness check). See RESULTS.md.
#
#   hier        Hier, idiomatic `check(make(d))` (transient charged to the
#               outer loop's arena — see RESULTS.md)
#   hier_scoped Hier, transient bound to an inner-loop local `t := make(d)`
#   c           C, manual malloc/free per node
#   rust        Rust, Box-owned enum, RAII drop
#   go          Go, pointer structs, garbage collected
#   koka        Koka, Perceus reference counting + reuse (the direct rival)
set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
HIERC=./hierc
[ -x "$HIERC" ] || { echo "no ./hierc — run 'make' first"; exit 2; }
D=bench/prongB
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
CC="${CC:-cc}"
$CC -O2 -o "$T/peakrss" bench/peakrss.c || { echo "peakrss build failed"; exit 2; }
to_kb() { case "$(uname)" in Darwin) echo $(( $1 / 1024 ));; *) echo "$1";; esac; }

# build
$HIERC "$D/binary_trees.hi"        --emit-c -o "$T/hier"   >/dev/null 2>&1 && $CC -O2 -o "$T/hier"   "$T/hier.c"
$HIERC "$D/binary_trees_scoped.hi" --emit-c -o "$T/hiers"  >/dev/null 2>&1 && $CC -O2 -o "$T/hiers"  "$T/hiers.c"
$CC -O2 -o "$T/c" "$D/binary_trees.c"
command -v rustc >/dev/null 2>&1 && rustc -O -o "$T/rs" "$D/binary_trees.rs" 2>/dev/null
if command -v go >/dev/null 2>&1; then cp "$D/binary_trees.go" "$T/" && ( cd "$T" && go build -o go binary_trees.go 2>/dev/null ); fi
# koka writes its build tree under --builddir; keep it out of the repo. koka -o
# does not set +x, so chmod after.
if command -v koka >/dev/null 2>&1; then koka -O2 --builddir="$T/.koka" -o "$T/koka" "$D/binarytrees.kk" >/dev/null 2>&1 && chmod +x "$T/koka"; fi

ref=""; fail=0
printf '%-12s %10s %8s   %s\n' lang peakRSS time output
run() {
    name="$1"; bin="$2"
    [ -x "$T/$bin" ] || { printf '%-12s %10s   (not built)\n' "$name" "-"; return; }
    "$T/peakrss" "$T/$bin" > "$T/$bin.out" 2> "$T/$bin.m"
    read rss ms < "$T/$bin.m"; kb="$(to_kb "$rss")"
    h="$(md5sum < "$T/$bin.out" | cut -c1-8)"
    [ -z "$ref" ] && ref="$h"
    [ "$h" = "$ref" ] && ok="ok" || { ok="OUTPUT DIFFERS"; fail=1; }
    printf '%-12s %8dMB %6dms   %s %s\n' "$name" "$(( kb / 1024 ))" "$ms" "$h" "$ok"
}
run hier        hier
run hier_scoped hiers
run c           c
run rust        rs
run go          go
run koka        koka
echo "-----------------------------------------------------------"
[ "$fail" -eq 0 ] && echo "all outputs identical" || { echo "OUTPUT MISMATCH"; exit 1; }
