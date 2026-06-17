#!/bin/sh
# Static-site-generation head-to-head: render N Markdown pages to HTML with the
# SAME inlined renderer in hier / C / Go, measuring peak RSS + wall (bench/peakrss)
# over an identical generated corpus, gated by an FNV-1a-32 checksum of every
# rendered byte (so the three do byte-identical work). The point: hier renders
# each page inside the loop body, whose per-scope arena is reclaimed every
# iteration -> FLAT peak RSS (matches C, with no manual free) where Go holds
# garbage under its GC. Best-of-3 wall. Skips any absent toolchain. NOT in make ci.
#
#   sh bench/site/run.sh [N]      (N default 5000)
set -u
cd "$(dirname "$0")/../.." || exit 2
HIERC=./hierc
[ -x "$HIERC" ] || { echo "no ./hierc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
N="${1:-5000}"
D=bench/site
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
$CC -O2 -o "$T/peakrss" bench/peakrss.c || { echo "peakrss build failed"; exit 2; }
to_kb() { case "$(uname)" in Darwin) echo $(( $1 / 1024 ));; *) echo "$1";; esac; }
FAIL=0

# build the renderers (hier + C required; Go optional/skipped if absent)
$HIERC "$D/gensite.hi" -o "$T/gensite" >"$T/log" 2>&1 || { echo "gensite build failed"; cat "$T/log"; exit 2; }
$HIERC "$D/site.hi"    -o "$T/hier"    >"$T/log" 2>&1 || { echo "hier build failed"; cat "$T/log"; exit 2; }
$CC -O2 -o "$T/c" "$D/site.c" 2>"$T/log" || echo "  (c build failed -- skipping)"
have_go=0
if command -v go >/dev/null 2>&1; then
    go build -o "$T/go" "$D/site.go" 2>"$T/log" && have_go=1 || echo "  (go build failed -- skipping)"
fi

# generate the corpus (dogfoods hier's write_file)
CORPUS="$T/pages"; mkdir -p "$CORPUS"
"$T/gensite" "$CORPUS" "$N" >/dev/null || { echo "corpus generation failed"; exit 2; }
echo "static-site render: $N pages  (peak RSS / best-of-3 wall / checksum)"

ref=""
run_one() {                                   # <label> <binary>
    [ -x "$2" ] || { printf '  %-5s (skipped)\n' "$1"; return; }
    best=""; out=""
    for _ in 1 2 3; do
        "$T/peakrss" "$2" "$CORPUS" "$N" > "$T/out" 2> "$T/m"
        ms=$(awk 'END{print $2}' "$T/m")
        { [ -z "$best" ] || [ "$ms" -lt "$best" ]; } && best="$ms"
        kb=$(to_kb "$(awk 'END{print $1}' "$T/m")"); rss=$(awk "BEGIN{printf \"%.1f\", $kb/1024}")
        out=$(cat "$T/out")
    done
    printf '  %-5s %7s MB %7s ms   %s\n' "$1" "$rss" "$best" "$out"
    if [ -z "$ref" ]; then ref="$out"; elif [ "$out" != "$ref" ]; then echo "  ^ CHECKSUM MISMATCH (expected $ref)"; FAIL=1; fi
}
run_one hier "$T/hier"
run_one c    "$T/c"
[ "$have_go" = 1 ] && run_one go "$T/go" || printf '  %-5s (go not installed)\n' go

[ "$FAIL" = 0 ] && echo "site-bench: OK (identical checksum -- hier holds C-flat memory, no manual free, no GC)" || { echo "site-bench: CHECKSUM MISMATCH"; exit 1; }
