#!/bin/sh
# Parallel text indexer head-to-head: the SAME concurrent program in
# tycho / C / Go, measuring peak RSS + wall (bench/peakrss) over an identical
# synthetic corpus, gated by a cross-language checksum.
#
#   gencorpus  writes a deterministic corpus (400 files x 4000 words, 5000-word
#              vocab) so every language indexes byte-identical input.
#   index      K=4 workers pull file paths off a channel (tycho) / buffered
#              channel (Go) / mutex work-queue (C), each tallies a LOCAL
#              term->count map, then main merges. tycho deep-copies every worker
#              map back across the thread boundary (value semantics, zero GC);
#              Go shares string bodies under its GC; C owns every byte by hand.
#
# Checksum "files tokens distinct csum" (csum = sum of len(term)*count) is
# order-independent, so the nondeterministic file->worker assignment still
# yields one oracle. Best-of-3 wall. Skips any absent toolchain. NOT in make ci.
set -u
cd "$(dirname "$0")/../.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
D=bench/indexer
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
$CC -O2 -o "$T/peakrss" bench/peakrss.c || { echo "peakrss build failed"; exit 2; }
to_kb() { case "$(uname)" in Darwin) echo $(( $1 / 1024 ));; *) echo "$1";; esac; }
FAIL=0

# ---- build + generate the corpus (dogfoods tycho's write_file builtin) ----
CORPUS="$T/corpus"; mkdir -p "$CORPUS"
$TYCHOC "$D/gencorpus.ty" -o "$T/gencorpus" > "$T/gen_err" 2>&1 || { echo "gencorpus build failed"; cat "$T/gen_err"; exit 2; }
"$T/gencorpus" "$CORPUS" >/dev/null || { echo "corpus generation failed"; exit 2; }

ref=""
run_one() {                                  # <label> <binary>
    if [ ! -x "$2" ]; then printf '  %-6s %s\n' "$1" "(build skipped)"; return; fi
    best=""; out=""
    for _ in 1 2 3; do
        "$T/peakrss" "$2" "$CORPUS" > "$T/out" 2> "$T/m"
        ms=$(awk '{print $2}' "$T/m")
        [ -z "$best" ] || [ "$ms" -lt "$best" ] && best="$ms"
        kb=$(to_kb "$(awk '{print $1}' "$T/m")"); rss=$(awk "BEGIN{printf \"%.1f\", $kb/1024}")
        out=$(cat "$T/out")
    done
    printf '  %-6s %7s MB %7s ms   %s\n' "$1" "$rss" "$best" "$out"
    if [ -z "$ref" ]; then ref="$out"; elif [ "$out" != "$ref" ]; then echo "  ^ CHECKSUM MISMATCH (expected $ref)"; FAIL=1; fi
}

printf '  %-6s %10s %10s   %s\n' lang peakRSS time checksum

$TYCHOC "$D/index.ty" -o "$T/index_tycho" > "$T/tycho_err" 2>&1 || { echo "indexer: TYCHO BUILD FAILED"; cat "$T/tycho_err"; exit 2; }
run_one tycho "$T/index_tycho"

$CC -O3 -pthread "$D/index.c" -o "$T/index_c" 2>/dev/null
run_one C "$T/index_c"

if command -v go >/dev/null 2>&1; then
    cp "$D/index.go" "$T/index.go" && ( cd "$T" && go build -o index_go index.go 2>/dev/null )
fi
run_one Go "$T/index_go"

[ $FAIL -eq 0 ] && echo "indexer bench: ok (checksums agree)" || { echo "indexer bench: CHECKSUM FAILURE"; exit 1; }
