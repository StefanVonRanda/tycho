#!/bin/sh
# Fair standard-opt (-O3) re-run of the benches not covered by fair_full.sh:
# winagg, invindex (side-array + map, growth + count-fill), window, dbquery.
# peak RSS + best-of-3 wall via bench/peakrss. Self-contained workloads (no stdin).
cd "$(dirname "$0")/.." || exit 1            # repo root (portable; was a hardcoded Linux path)
TYCHOC=./tychoc
T=$(mktemp -d); cc -O2 -o "$T/pk" bench/peakrss.c || exit 1
# ru_maxrss is KB on Linux, bytes on macOS/BSD — normalize to KB so $bkb/1024 is MB on both.
to_kb() { case "$(uname)" in Darwin) echo $(( $1 / 1024 ));; *) echo "$1";; esac; }
# Fail-closed: a nonzero exit prints a loud FAILED marker instead of numbers
# (peakrss propagates the child's status). stdout goes to $T/cur.out and is
# compared against the row's first contender ($T/ref.out — tycho() resets it,
# and tycho is always first on a row): a mismatch appends OUT-DIFF.
best() { bms=9999999; bkb=0; rc=0
  for i in 1 2 3; do
    "$T/pk" "$1" > "$T/cur.out" 2> "$T/stat" || rc=$?
    last=$(tail -1 "$T/stat")
    kb=$(to_kb "$(echo "$last" | awk '{print $(NF-1)}')"); ms=$(echo "$last" | awk '{print $NF}')
    case "$ms" in *[!0-9]*|"") ms=9999999 ;; esac
    [ "$ms" -lt "$bms" ] && bms=$ms && bkb=$kb
  done
  if [ "$rc" -ne 0 ]; then printf 'FAILED(rc=%s)' "$rc"; return 0; fi
  if [ -f "$T/ref.out" ]; then
    cmp -s "$T/cur.out" "$T/ref.out" || { awk "BEGIN{printf \"%5.1fMB/%s OUT-DIFF\", $bkb/1024.0, \"${bms}ms\"}"; return 0; }
  else cp "$T/cur.out" "$T/ref.out"; fi
  awk "BEGIN{printf \"%5.1fMB/%-7s\", $bkb/1024.0, \"${bms}ms\"}"
}
tycho() { rm -f "$T/ref.out"; $TYCHOC "$1" --emit-c -o "$T/b" >/dev/null 2>&1 && cc -O3 -o "$T/b" "$T/b.c" -lm 2>/dev/null && best "$T/b" || echo "build-fail"; }
cc3()  { cc -O3 -o "$T/b" "$1" -lm 2>/dev/null && best "$T/b" || echo "-"; }
rs3()  { rustc -C opt-level=3 -o "$T/b" "$1" 2>/dev/null && best "$T/b" || echo "-"; }
go3()  { ( cd "$(dirname "$1")" && go build -o "$T/b" "$(basename "$1")" 2>/dev/null ) && best "$T/b" || echo "-"; }

echo "===== winagg (tycho/C/Rust/Go) ====="
W=bench/winagg
printf "  tycho %s\n  C    %s\n  Rust %s\n  Go   %s\n" "$(tycho $W/winagg.ty)" "$(cc3 $W/winagg.c)" "$(rs3 $W/winagg.rs)" "$(go3 $W/winagg.go)"

echo "===== invindex side-array  (growth: .{ty,c,go} | count-fill: _exact.{ty,c}) ====="
I=bench/invindex
printf "  growth     tycho %s  C %s  Go %s\n" "$(tycho $I/invindex.ty)" "$(cc3 $I/invindex.c)" "$(go3 $I/invindex.go)"
printf "  count-fill tycho %s  C %s\n" "$(tycho $I/invindex_exact.ty)" "$(cc3 $I/invindex_exact.c)"

echo "===== invindex map  (growth: _map.{ty,c,go} | count-fill: _map_exact.{ty,c,go}) ====="
printf "  growth     tycho %s  C %s  Go %s\n" "$(tycho $I/invindex_map.ty)" "$(cc3 $I/invindex_map.c)" "$(go3 $I/invindex_map.go)"
printf "  count-fill tycho %s  C %s  Go %s\n" "$(tycho $I/invindex_map_exact.ty)" "$(cc3 $I/invindex_map_exact.c)" "$(go3 $I/invindex_map_exact.go)"

echo "===== window (string + int) ====="
WD=bench/window
printf "  string tycho %s  C %s  Go %s\n" "$(tycho $WD/window_naive.ty)" "$(cc3 $WD/window.c)" "$(go3 $WD/window.go)"
printf "  int    tycho %s\n" "$(tycho $WD/window_int.ty)"

echo "===== dbquery (real SQLite via FFI; skips if no libsqlite3) ====="
if pkg-config --exists sqlite3 2>/dev/null || echo '#include <sqlite3.h>' | cc -E - >/dev/null 2>&1; then
  DB=bench/dbquery
  # macOS ships libsqlite3 + sqlite3.h but no .pc file — use --link (not --pkg) there.
  if pkg-config --exists sqlite3 2>/dev/null; then TYCHO_SQLITE="--pkg sqlite3"; else TYCHO_SQLITE="--link sqlite3"; fi
  rm -f "$T/ref.out"
  $TYCHOC "$DB/dbquery.ty" -o "$T/dbh" $TYCHO_SQLITE >/dev/null 2>&1 && printf "  tycho %s\n" "$(best "$T/dbh")" || echo "  tycho build-fail"
  LIBS=$(pkg-config --cflags --libs sqlite3 2>/dev/null || echo -lsqlite3)
  cc -O3 $DB/dbquery.c -o "$T/dbc" $LIBS 2>/dev/null && printf "  C    %s\n" "$(best "$T/dbc")" || echo "  C build-fail"
  ( cd "$DB" && go build -o "$T/dbg" dbquery.go 2>/dev/null ) && printf "  Go   %s\n" "$(best "$T/dbg")" || echo "  Go (cgo) skip/fail"
else
  echo "  (libsqlite3 not available — skipped)"
fi
rm -rf "$T"
