cd "$(dirname "$0")/.." || exit 1            # repo root (portable; was a hardcoded Linux path)
TYCHOC="${TYCHOC:-./tychoc}"
CC="${CC:-cc}"
T=$(mktemp -d); $CC -O2 -o "$T/pk" bench/peakrss.c || exit 1
# ru_maxrss is KB on Linux, bytes on macOS/BSD — normalize to KB so $bkb/1024 is MB on both.
to_kb() { case "$(uname)" in Darwin) echo $(( $1 / 1024 ));; *) echo "$1";; esac; }
# Service rows also feed the same ratio scorecard as prongB. A `-` or a
# failed build is omitted from the TSV, never treated as a zero.
REC="$T/records"; : > "$REC"
emit() { # <workload> <language> <result from best()>
  case "$3" in
    *MB/*ms*)
      clean=$(printf '%s' "$3" | tr -d '[:space:]')
      case "$clean" in
        *MB/*ms*)
          mb=${clean%%MB/*}
          ms=${clean#*MB/}; ms=${ms%%ms*}
          printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$mb" "$ms" >> "$REC"
          ;;
      esac
      ;;
  esac
}

# Fail-closed: a nonzero exit prints a loud FAILED marker instead of numbers
# (peakrss propagates the child's status). stdout goes to $T/cur.out and is
# compared against the row's first contender ($T/ref.out — tycho() resets it,
# and tycho is always first on a row): a mismatch appends OUT-DIFF.
best() { bms=9999999; bkb=0; rc=0
  # Two discarded warm-ups: page-in and the first execution are not the
  # workload, and this keeps the service rows consistent with prongB.
  for i in 1 2; do
    "$T/pk" "$1" > "$T/warm.out" 2> "$T/warm.stat" || rc=$?
  done
  for i in 1 2 3; do
    "$T/pk" "$1" > "$T/cur.out" 2> "$T/stat" || rc=$?
    last=$(tail -1 "$T/stat")
    kb=$(to_kb "$(echo "$last" | awk '{print $(NF-1)}')"); ms=$(echo "$last" | awk '{print $NF}')
    case "$ms" in *[!0-9]*|"") ms=9999999 ;; esac
    [ "$ms" -lt "$bms" ] && bms=$ms
    [ "$kb" -gt "$bkb" ] && bkb=$kb
  done
  if [ "$rc" -ne 0 ]; then printf 'FAILED(rc=%s)' "$rc"; return 0; fi
  if [ -f "$T/ref.out" ]; then
    cmp -s "$T/cur.out" "$T/ref.out" || { awk "BEGIN{printf \"%5.1fMB/%s OUT-DIFF\", $bkb/1024.0, \"${bms}ms\"}"; return 0; }
  else cp "$T/cur.out" "$T/ref.out"; fi
  awk "BEGIN{printf \"%5.1fMB/%-7s\", $bkb/1024.0, \"${bms}ms\"}"
}
tycho() { rm -f "$T/ref.out"; $TYCHOC "$1" --emit-c -o "$T/b" >/dev/null 2>&1 && $CC -O3 -o "$T/b" "$T/b.c" -lm 2>/dev/null && best "$T/b" || echo "build-fail"; }
cc3()  { $CC -O3 -o "$T/b" "$1" -lm 2>/dev/null && best "$T/b" || echo "-"; }
rs3()  { rustc -C opt-level=3 -o "$T/b" "$1" 2>/dev/null && best "$T/b" || echo "-"; }
go3()  { ( cd "$(dirname "$1")" && go build -o "$T/b" "$(basename "$1")" 2>/dev/null ) && best "$T/b" || echo "-"; }
java3() {
  [ -f "$1" ] && command -v javac >/dev/null 2>&1 || { echo "-"; return; }
  mkdir -p "$T/jc3"
  javac -d "$T/jc3" "$1" 2>/dev/null || { echo "-"; return; }
  base=$(basename "$1" .java)
  printf '#!/bin/sh\nexec java -cp "%s" %s\n' "$T/jc3" "$base" > "$T/jw3"
  chmod +x "$T/jw3"
  best "$T/jw3"
}
swift3() {
  [ -f "$1" ] && command -v swiftc >/dev/null 2>&1 || { echo "-"; return; }
  swiftc -O -o "$T/sw3" "$1" 2>/dev/null && best "$T/sw3" || echo "-"
}

echo "===== winagg (tycho/C/Rust/Go/Java/Swift) ====="
W=bench/winagg
w_ty=$(tycho $W/winagg.ty); w_c=$(cc3 $W/winagg.c); w_rs=$(rs3 $W/winagg.rs); w_go=$(go3 $W/winagg.go); w_java=$(java3 $W/winagg.java); w_sw=$(swift3 $W/winagg.swift)
printf "  tycho %s\n  C    %s\n  Rust %s\n  Go   %s\n  Java %s\n  Swift %s\n" "$w_ty" "$w_c" "$w_rs" "$w_go" "$w_java" "$w_sw"
emit winagg tycho "$w_ty"; emit winagg c "$w_c"; emit winagg rust "$w_rs"; emit winagg go "$w_go"; emit winagg java "$w_java"; emit winagg swift "$w_sw"

echo "===== invindex side-array  (growth + count-fill) ====="
I=bench/invindex
i_ty=$(tycho $I/invindex.ty); i_c=$(cc3 $I/invindex.c); i_rs=$(rs3 $I/invindex.rs); i_go=$(go3 $I/invindex.go); i_java=$(java3 $I/invindex.java); i_sw=$(swift3 $I/invindex.swift)
printf "  growth     tycho %s  C %s  Rust %s  Go %s  Java %s  Swift %s\n" "$i_ty" "$i_c" "$i_rs" "$i_go" "$i_java" "$i_sw"
emit invindex_growth tycho "$i_ty"; emit invindex_growth c "$i_c"; emit invindex_growth rust "$i_rs"; emit invindex_growth go "$i_go"; emit invindex_growth java "$i_java"; emit invindex_growth swift "$i_sw"
i2_ty=$(tycho $I/invindex_exact.ty); i2_c=$(cc3 $I/invindex_exact.c); i2_rs=$(rs3 $I/invindex_exact.rs); i2_go=$(go3 $I/invindex_exact.go); i2_java=$(java3 $I/invindex_exact.java); i2_sw=$(swift3 $I/invindex_exact.swift)
printf "  count-fill tycho %s  C %s  Rust %s  Go %s  Java %s  Swift %s\n" "$i2_ty" "$i2_c" "$i2_rs" "$i2_go" "$i2_java" "$i2_sw"
emit invindex_count tycho "$i2_ty"; emit invindex_count c "$i2_c"; emit invindex_count rust "$i2_rs"; emit invindex_count go "$i2_go"; emit invindex_count java "$i2_java"; emit invindex_count swift "$i2_sw"

echo "===== invindex map  (growth + count-fill) ====="
im_ty=$(tycho $I/invindex_map.ty); im_c=$(cc3 $I/invindex_map.c); im_rs=$(rs3 $I/invindex_map.rs); im_go=$(go3 $I/invindex_map.go); im_java=$(java3 $I/invindex_map.java); im_sw=$(swift3 $I/invindex_map.swift)
printf "  growth     tycho %s  C %s  Rust %s  Go %s  Java %s  Swift %s\n" "$im_ty" "$im_c" "$im_rs" "$im_go" "$im_java" "$im_sw"
emit invindex_map_growth tycho "$im_ty"; emit invindex_map_growth c "$im_c"; emit invindex_map_growth rust "$im_rs"; emit invindex_map_growth go "$im_go"; emit invindex_map_growth java "$im_java"; emit invindex_map_growth swift "$im_sw"
im2_ty=$(tycho $I/invindex_map_exact.ty); im2_c=$(cc3 $I/invindex_map_exact.c); im2_rs=$(rs3 $I/invindex_map_exact.rs); im2_go=$(go3 $I/invindex_map_exact.go); im2_java=$(java3 $I/invindex_map_exact.java); im2_sw=$(swift3 $I/invindex_map_exact.swift)
printf "  count-fill tycho %s  C %s  Rust %s  Go %s  Java %s  Swift %s\n" "$im2_ty" "$im2_c" "$im2_rs" "$im2_go" "$im2_java" "$im2_sw"
emit invindex_map_count tycho "$im2_ty"; emit invindex_map_count c "$im2_c"; emit invindex_map_count rust "$im2_rs"; emit invindex_map_count go "$im2_go"; emit invindex_map_count java "$im2_java"; emit invindex_map_count swift "$im2_sw"

echo "===== window (string + int) ====="
WD=bench/window
wd_ty=$(tycho $WD/window_naive.ty); wd_c=$(cc3 $WD/window.c); wd_rs=$(rs3 $WD/window.rs); wd_go=$(go3 $WD/window.go); wd_java=$(java3 $WD/window.java); wd_sw=$(swift3 $WD/window.swift)
printf "  string tycho %s  C %s  Rust %s  Go %s  Java %s  Swift %s\n" "$wd_ty" "$wd_c" "$wd_rs" "$wd_go" "$wd_java" "$wd_sw"
emit window_string tycho "$wd_ty"; emit window_string c "$wd_c"; emit window_string rust "$wd_rs"; emit window_string go "$wd_go"; emit window_string java "$wd_java"; emit window_string swift "$wd_sw"
wi_ty=$(tycho $WD/window_int.ty); wi_c=$(cc3 $WD/window_int.c); wi_rs=$(rs3 $WD/window_int.rs); wi_go=$(go3 $WD/window_int.go); wi_java=$(java3 $WD/window_int.java); wi_sw=$(swift3 $WD/window_int.swift)
printf "  int    tycho %s  C %s  Rust %s  Go %s  Java %s  Swift %s\n" "$wi_ty" "$wi_c" "$wi_rs" "$wi_go" "$wi_java" "$wi_sw"
emit window_int tycho "$wi_ty"; emit window_int c "$wi_c"; emit window_int rust "$wi_rs"; emit window_int go "$wi_go"; emit window_int java "$wi_java"; emit window_int swift "$wi_sw"

echo "===== dbquery (real SQLite via FFI; skips if no libsqlite3) ====="
if pkg-config --exists sqlite3 2>/dev/null || echo '#include <sqlite3.h>' | $CC -E - >/dev/null 2>&1; then
  DB=bench/dbquery
  # macOS ships libsqlite3 + sqlite3.h but no .pc file — use --link (not --pkg) there.
  if pkg-config --exists sqlite3 2>/dev/null; then TYCHO_SQLITE="--pkg sqlite3"; else TYCHO_SQLITE="--link sqlite3"; fi
  rm -f "$T/ref.out"
  db_ty=$( $TYCHOC "$DB/dbquery.ty" -o "$T/dbh" $TYCHO_SQLITE >/dev/null 2>&1 && best "$T/dbh" || echo "build-fail" )
  printf "  tycho %s\n" "$db_ty"; emit dbquery tycho "$db_ty"
  LIBS=$(pkg-config --cflags --libs sqlite3 2>/dev/null || echo -lsqlite3)
  db_c=$( $CC -O3 $DB/dbquery.c -o "$T/dbc" $LIBS 2>/dev/null && best "$T/dbc" || echo "build-fail" )
  printf "  C    %s\n" "$db_c"; emit dbquery c "$db_c"
  db_go=$( ( cd "$DB" && go build -o "$T/dbg" dbquery.go 2>/dev/null ) && best "$T/dbg" || echo "build-fail" )
  printf "  Go   %s\n" "$db_go"; emit dbquery go "$db_go"
else
  echo "  (libsqlite3 not available — skipped)"
fi
if command -v python3 >/dev/null 2>&1; then
  python3 bench/score.py "$REC"
fi
rm -rf "$T"
