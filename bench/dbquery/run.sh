#!/bin/sh
# dbquery head-to-head: a REAL in-memory SQLite workload, the same in tycho / C /
# Go, measuring peak RSS + wall (via bench/peakrss) with a byte-identical
# checksum as the cross-language correctness check. SQLite work is held constant;
# what differs is the host-side data handling -- tycho's arena vs C's stack/manual
# vs Go's GC. Needs libsqlite3; skips any language whose toolchain is absent.
# NOT wired into `make ci` (system dependency). See RESULTS.md.
set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
# Detect sqlite3 portably. Prefer pkg-config (Linux); on macOS Apple ships
# libsqlite3 + sqlite3.h in the SDK but NO .pc file, so fall back to a bare
# `-lsqlite3` link probe and pass tycho `--link sqlite3` (skips pkg-config)
# instead of `--pkg sqlite3`.
if pkg-config --exists sqlite3 2>/dev/null; then
    LIBS="$(pkg-config --libs sqlite3)"; TYCHO_SQLITE="--pkg sqlite3"
elif echo 'int main(void){return 0;}' | $CC -xc - -lsqlite3 -o /dev/null 2>/dev/null; then
    LIBS="-lsqlite3"; TYCHO_SQLITE="--link sqlite3"
else
    echo "dbquery: SKIP (libsqlite3 not installed)"; exit 0
fi
D=bench/dbquery
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
$CC -O2 -o "$T/peakrss" bench/peakrss.c || { echo "peakrss build failed"; exit 2; }
# ru_maxrss is KB on Linux, bytes on macOS/BSD — normalize to KB, then /1024 for MB.
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
# Fail-closed: libsqlite3 is present (checked above), so a tycho build failure
# here is a real failure -- print the compiler error and exit nonzero, never
# a silent "(build skipped)" followed by "dbquery: ok".
if ! $TYCHOC "$D/dbquery.ty" -o "$T/dbq_tycho" $TYCHO_SQLITE > "$T/tycho_err" 2>&1; then
    echo "dbquery: TYCHO BUILD FAILED"; cat "$T/tycho_err"; exit 2
fi
runlang tycho "$T/dbq_tycho"
$CC -O3 "$D/dbquery.c" -o "$T/dbq_c" $LIBS 2>/dev/null
runlang C "$T/dbq_c"
if command -v go >/dev/null 2>&1; then
    cp "$D/dbquery.go" "$T/dbq.go" && ( cd "$T" && go build -o dbq_go dbq.go 2>/dev/null )
    runlang go "$T/dbq_go"
fi
[ "$FAIL" = 0 ] && echo "dbquery: ok (all checksums agree)" || { echo "dbquery: FAIL"; exit 1; }
