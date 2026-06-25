#!/bin/sh
# Dijkstra head-to-head: build a sparse random graph as an adjacency LIST OF INDICES,
# run single-source shortest paths with a hand-built binary min-heap, and fold the
# reachable distances into a checksum. The graph is the memory under test: tycho's
# value-semantic `[[Edge]]` vs C's `Edge**` vs Go's `[][]Edge`. A graph by indices is
# value-shaped (no shared pointers), so this is where value semantics should be
# competitive -- the opposite of the pointer-linked trie. A byte-identical checksum
# across the three ports (distances are tie-break independent) is the correctness check.
# tycho uses no hand-written C. NOT wired into `make ci`. See RESULTS.md.
set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
export TYCHO_CORELIB="$PWD/corelib"
D=bench/dijkstra
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
$CC -O2 -o "$T/peakrss" bench/peakrss.c || { echo "peakrss build failed"; exit 2; }
to_kb() { case "$(uname)" in Darwin) echo $(( $1 / 1024 ));; *) echo "$1";; esac; }
ref=""; FAIL=0

runlang() {                                           # <label> <binary>
    if [ ! -x "$2" ]; then printf '%-8s %s\n' "$1" "(build skipped)"; return; fi
    "$T/peakrss" "$2" > "$T/out" 2> "$T/m"
    kb=$(to_kb "$(awk '{print $1}' "$T/m")"); rss=$(awk "BEGIN{printf \"%.1f\", $kb/1024}"); ms=$(awk '{print $2}' "$T/m"); out=$(cat "$T/out")
    printf '%-8s %7s MB %6s ms   %s\n' "$1" "$rss" "$ms" "$out"
    if [ -z "$ref" ]; then ref="$out"; elif [ "$out" != "$ref" ]; then echo "  ^ CHECKSUM MISMATCH (expected $ref)"; FAIL=1; fi
}

printf '%-8s %10s %9s   %s\n' lang peakRSS time 'checksum(reach sum)'
if ! $TYCHOC "$D/dijkstra.ty" -o "$T/dt" > "$T/terr" 2>&1; then
    echo "dijkstra: TYCHO BUILD FAILED"; cat "$T/terr"; exit 2
fi
runlang tycho "$T/dt"
$CC -O3 "$D/dijkstra.c" -o "$T/dc" 2>/dev/null
runlang C "$T/dc"
if command -v go >/dev/null 2>&1; then
    cp "$D/dijkstra.go" "$T/dg.go" && ( cd "$T" && go build -o dg dg.go 2>/dev/null )
    runlang go "$T/dg"
fi
[ "$FAIL" = 0 ] && echo "dijkstra: ok (all checksums agree)" || { echo "dijkstra: FAIL"; exit 1; }
