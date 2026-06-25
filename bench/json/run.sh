#!/bin/sh
# json head-to-head: parse a large JSON document into a generic tagged value tree and
# fold a checksum over it, the same in tycho / C / Go. Measures peak RSS + wall (via
# bench/peakrss) with a byte-identical checksum as the cross-language correctness check.
# The tree is the memory under test: tycho's implicit arena vs C's per-node malloc vs
# Go's GC. tycho uses core:json (no hand-written C). Skips a language whose toolchain
# is absent. NOT wired into `make ci`. See RESULTS.md.
set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
export TYCHO_CORELIB="$PWD/corelib"
D=bench/json
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

printf '%-8s %10s %9s   %s\n' lang peakRSS time checksum
# Fail-closed: a tycho build failure here is a real failure (core:json is in-tree).
if ! $TYCHOC "$D/json.ty" -o "$T/jt" > "$T/terr" 2>&1; then
    echo "json: TYCHO BUILD FAILED"; cat "$T/terr"; exit 2
fi
runlang tycho "$T/jt"
$CC -O3 "$D/json.c" -o "$T/jc" 2>/dev/null
runlang C "$T/jc"
if command -v go >/dev/null 2>&1; then
    cp "$D/json.go" "$T/jg.go" && ( cd "$T" && go build -o jg jg.go 2>/dev/null )
    runlang go "$T/jg"
fi
[ "$FAIL" = 0 ] && echo "json: ok (all checksums agree)" || { echo "json: FAIL"; exit 1; }
