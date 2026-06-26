#!/bin/sh
# Tree-walking interpreter head-to-head: build a large deterministic expression AST (a
# recursive enum / tagged union), eval it, constant-fold it (a rewrite pass that
# allocates a new AST), count nodes, deep-compare original vs folded, and fold into a
# `n eval foldedN foldedEval same` checksum. The AST is the memory under test: tycho's
# value-semantic recursive enum vs C's malloc'd tagged-union nodes vs Go's struct
# pointers (GC) -- the recursive-by-value value-shape (the trie's cousin). A byte-
# identical checksum across the three ports is the correctness check; C is compiled
# -fwrapv so its eval wraps like tycho's. tycho uses no hand-written C. NOT wired into
# `make ci`. See RESULTS.md.
set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
D=bench/interp
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

printf '%-8s %10s %9s   %s\n' lang peakRSS time 'checksum(n eval foldN foldEval same)'
if ! $TYCHOC "$D/interp.ty" -o "$T/it" > "$T/terr" 2>&1; then
    echo "interp: TYCHO BUILD FAILED"; cat "$T/terr"; exit 2
fi
runlang tycho "$T/it"
$CC -O3 -fwrapv "$D/interp.c" -o "$T/ic" 2>/dev/null
runlang C "$T/ic"
if command -v go >/dev/null 2>&1; then
    cp "$D/interp.go" "$T/ig.go" && ( cd "$T" && go build -o ig ig.go 2>/dev/null )
    runlang go "$T/ig"
fi
[ "$FAIL" = 0 ] && echo "interp: ok (all checksums agree)" || { echo "interp: FAIL"; exit 1; }
