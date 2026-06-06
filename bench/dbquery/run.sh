#!/bin/sh
# dbquery head-to-head: a REAL in-memory SQLite workload, the same in hier / C /
# Go, measuring peak RSS + wall (via bench/peakrss) with a byte-identical
# checksum as the cross-language correctness check. SQLite work is held constant;
# what differs is the host-side data handling -- hier's arena vs C's stack/manual
# vs Go's GC. Needs libsqlite3; skips any language whose toolchain is absent.
# NOT wired into `make ci` (system dependency). See RESULTS.md.
set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
HIERC=./hierc
[ -x "$HIERC" ] || { echo "no ./hierc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
if ! pkg-config --exists sqlite3 2>/dev/null; then echo "dbquery: SKIP (libsqlite3 not installed)"; exit 0; fi
LIBS="$(pkg-config --libs sqlite3)"
D=bench/dbquery
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
$CC -O2 -o "$T/peakrss" bench/peakrss.c || { echo "peakrss build failed"; exit 2; }
ref=""; FAIL=0

runlang() {                                           # <label> <binary>
    if [ ! -x "$2" ]; then printf '%-8s %s\n' "$1" "(build skipped)"; return; fi
    "$T/peakrss" "$2" > "$T/out" 2> "$T/m"
    rss=$(awk '{printf "%.1f", $1/1024}' "$T/m"); ms=$(awk '{print $2}' "$T/m"); out=$(cat "$T/out")
    printf '%-8s %7s MB %6s ms   %s\n' "$1" "$rss" "$ms" "$out"
    if [ -z "$ref" ]; then ref="$out"; elif [ "$out" != "$ref" ]; then echo "  ^ CHECKSUM MISMATCH (expected $ref)"; FAIL=1; fi
}

printf '%-8s %10s %9s   %s\n' lang peakRSS time checksum
$HIERC "$D/dbquery.hi" -o "$T/dbq_hier" --shim "$D/db_shim.c" --pkg sqlite3 >/dev/null 2>&1
runlang hier "$T/dbq_hier"
$CC -O2 "$D/dbquery.c" -o "$T/dbq_c" $LIBS 2>/dev/null
runlang C "$T/dbq_c"
if command -v go >/dev/null 2>&1; then
    cp "$D/dbquery.go" "$T/dbq.go" && ( cd "$T" && go build -o dbq_go dbq.go 2>/dev/null )
    runlang go "$T/dbq_go"
fi
[ "$FAIL" = 0 ] && echo "dbquery: ok (all checksums agree)" || { echo "dbquery: FAIL"; exit 1; }
