#!/bin/sh
# LRU cache head-to-head: a fixed-capacity least-recently-used cache driven by a
# shared LCG op-stream (70% get / 30% put over a keyspace larger than capacity, so
# eviction churns), folding hits + returned values into a checksum. The cache is the
# memory under test: tycho's value-semantic index pool (`[Node]` + `[int:int]` map,
# tail-slot recycled on eviction) vs C's hash map + doubly-linked list (backward-shift
# delete) vs Go's builtin map + node slice. A textbook LRU is a pointer-linked list;
# tycho cannot store pointers, so it uses indices -- the value-shaped idiom. This bench
# also exercises delete-heavy maps (every eviction deletes a key), the workload that
# surfaced the O(n)->O(1) map-delete fix. A byte-identical `hits sum` checksum across the
# three ports is the correctness check. tycho uses no hand-written C. NOT wired into
# `make ci`. See RESULTS.md.
set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
export TYCHO_CORELIB="$PWD/corelib"
D=bench/lru
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

printf '%-8s %10s %9s   %s\n' lang peakRSS time 'checksum(hits sum)'
if ! $TYCHOC "$D/lru.ty" -o "$T/lt" > "$T/terr" 2>&1; then
    echo "lru: TYCHO BUILD FAILED"; cat "$T/terr"; exit 2
fi
runlang tycho "$T/lt"
$CC -O3 "$D/lru.c" -o "$T/lc" 2>/dev/null
runlang C "$T/lc"
if command -v go >/dev/null 2>&1; then
    cp "$D/lru.go" "$T/lg.go" && ( cd "$T" && go build -o lg lg.go 2>/dev/null )
    runlang go "$T/lg"
fi
[ "$FAIL" = 0 ] && echo "lru: ok (all checksums agree)" || { echo "lru: FAIL"; exit 1; }
