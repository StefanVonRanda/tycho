#!/bin/sh
# trie head-to-head: insert N deterministic words into a prefix tree where each node
# owns a small int->child map, and report (node count, word count) as a byte-identical
# cross-language checksum. The whole trie is the memory under test: tycho's value-
# semantic arena map vs C's per-node malloc map vs Go's GC map[byte]*Node. tycho uses
# native [int: Trie] (no hand-written C). Skips a language whose toolchain is absent.
# NOT wired into `make ci`. See RESULTS.md.
set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
export TYCHO_CORELIB="$PWD/corelib"
D=bench/trie
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

printf '%-8s %10s %9s   %s\n' lang peakRSS time 'checksum(nodes words)'
if ! $TYCHOC "$D/trie.ty" -o "$T/tt" > "$T/terr" 2>&1; then
    echo "trie: TYCHO BUILD FAILED"; cat "$T/terr"; exit 2
fi
runlang tycho "$T/tt"
$CC -O3 "$D/trie.c" -o "$T/tc" 2>/dev/null
runlang C "$T/tc"
if command -v go >/dev/null 2>&1; then
    cp "$D/trie.go" "$T/tg.go" && ( cd "$T" && go build -o tg tg.go 2>/dev/null )
    runlang go "$T/tg"
fi
[ "$FAIL" = 0 ] && echo "trie: ok (all checksums agree)" || { echo "trie: FAIL"; exit 1; }
