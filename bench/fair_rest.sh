#!/bin/sh
# Fair standard-opt (-O3) re-run of the benches not covered by fair_full.sh:
# winagg, invindex (side-array + map, growth + count-fill), window, dbquery.
# peak RSS + best-of-3 wall via bench/peakrss. Self-contained workloads (no stdin).
cd "$(dirname "$0")/.." || exit 1            # repo root (portable; was a hardcoded Linux path)
HIERC=./hierc
T=$(mktemp -d); cc -O2 -o "$T/pk" bench/peakrss.c || exit 1
# ru_maxrss is KB on Linux, bytes on macOS/BSD — normalize to KB so $bkb/1024 is MB on both.
to_kb() { case "$(uname)" in Darwin) echo $(( $1 / 1024 ));; *) echo "$1";; esac; }
best() { bms=9999999; bkb=0
  for i in 1 2 3; do
    o=$("$T/pk" "$1" 2>&1); last=$(echo "$o" | tail -1)
    kb=$(to_kb "$(echo "$last" | awk '{print $(NF-1)}')"); ms=$(echo "$last" | awk '{print $NF}')
    case "$ms" in *[!0-9]*|"") ms=9999999 ;; esac
    [ "$ms" -lt "$bms" ] && bms=$ms && bkb=$kb
  done
  awk "BEGIN{printf \"%5.1fMB/%-7s\", $bkb/1024.0, \"${bms}ms\"}"
}
hier() { $HIERC "$1" --emit-c -o "$T/b" >/dev/null 2>&1 && cc -O3 -o "$T/b" "$T/b.c" -lm 2>/dev/null && best "$T/b" || echo "build-fail"; }
cc3()  { cc -O3 -o "$T/b" "$1" -lm 2>/dev/null && best "$T/b" || echo "-"; }
rs3()  { rustc -C opt-level=3 -o "$T/b" "$1" 2>/dev/null && best "$T/b" || echo "-"; }
go3()  { ( cd "$(dirname "$1")" && go build -o "$T/b" "$(basename "$1")" 2>/dev/null ) && best "$T/b" || echo "-"; }

echo "===== winagg (hier/C/Rust/Go) ====="
W=bench/winagg
printf "  hier %s\n  C    %s\n  Rust %s\n  Go   %s\n" "$(hier $W/winagg.hi)" "$(cc3 $W/winagg.c)" "$(rs3 $W/winagg.rs)" "$(go3 $W/winagg.go)"

echo "===== invindex side-array  (growth: .{hi,c,go} | count-fill: _exact.{hi,c}) ====="
I=bench/invindex
printf "  growth     hier %s  C %s  Go %s\n" "$(hier $I/invindex.hi)" "$(cc3 $I/invindex.c)" "$(go3 $I/invindex.go)"
printf "  count-fill hier %s  C %s\n" "$(hier $I/invindex_exact.hi)" "$(cc3 $I/invindex_exact.c)"

echo "===== invindex map  (growth: _map.{hi,c,go} | count-fill: _map_exact.{hi,c,go}) ====="
printf "  growth     hier %s  C %s  Go %s\n" "$(hier $I/invindex_map.hi)" "$(cc3 $I/invindex_map.c)" "$(go3 $I/invindex_map.go)"
printf "  count-fill hier %s  C %s  Go %s\n" "$(hier $I/invindex_map_exact.hi)" "$(cc3 $I/invindex_map_exact.c)" "$(go3 $I/invindex_map_exact.go)"

echo "===== window (string + int) ====="
WD=bench/window
printf "  string hier %s  C %s  Go %s\n" "$(hier $WD/window_naive.hi)" "$(cc3 $WD/window.c)" "$(go3 $WD/window.go)"
printf "  int    hier %s\n" "$(hier $WD/window_int.hi)"

echo "===== dbquery (real SQLite via FFI; skips if no libsqlite3) ====="
if pkg-config --exists sqlite3 2>/dev/null || echo '#include <sqlite3.h>' | cc -E - >/dev/null 2>&1; then
  DB=bench/dbquery
  $HIERC "$DB/dbquery.hi" -o "$T/dbh" --shim "$DB/db_shim.c" --pkg sqlite3 >/dev/null 2>&1 && printf "  hier %s\n" "$(best "$T/dbh")" || echo "  hier build-fail"
  LIBS=$(pkg-config --cflags --libs sqlite3 2>/dev/null || echo -lsqlite3)
  cc -O3 $DB/dbquery.c -o "$T/dbc" $LIBS 2>/dev/null && printf "  C    %s\n" "$(best "$T/dbc")" || echo "  C build-fail"
  ( cd "$DB" && go build -o "$T/dbg" dbquery.go 2>/dev/null ) && printf "  Go   %s\n" "$(best "$T/dbg")" || echo "  Go (cgo) skip/fail"
else
  echo "  (libsqlite3 not available — skipped)"
fi
rm -rf "$T"
