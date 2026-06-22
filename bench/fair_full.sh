#!/bin/sh
# Fair "each language at its standard optimized build" sweep — peak RSS + wall.
#   tycho : -O3 (its default; rides the C optimizer)   C/Rust : -O3
#   Go   : go build (Go's only optimized level)        Koka  : koka -O2 (its max)
# best-of-3 wall ms; peak RSS kb via bench/peakrss (prints "... rss_kb wall_ms").
cd "$(dirname "$0")/.." || exit 1            # repo root (portable; was a hardcoded Linux path)
T=$(mktemp -d); cc -O2 -o "$T/pk" bench/peakrss.c || exit 1
P=bench/prongB
# ru_maxrss is KB on Linux, bytes on macOS/BSD — normalize to KB so $bkb/1024 is MB on both.
to_kb() { case "$(uname)" in Darwin) echo $(( $1 / 1024 ));; *) echo "$1";; esac; }
# best <bin> [input] [mode]  mode: stdin (default) | arg — Koka json_parse takes
# the doc as a PATH ARGUMENT (jsonparse.kk: get-args + read-text-file), not stdin.
# Fail-closed: a nonzero exit prints a loud FAILED marker instead of numbers
# (peakrss propagates the child's status). stdout goes to $T/cur.out and is
# compared against the row's first contender ($T/ref.out, reset per row): a
# checksum mismatch appends OUT-DIFF to the cell.
best() { bms=9999999; bkb=0; rc=0
  for i in 1 2 3; do
    case "${3:-stdin}" in
      arg) "$T/pk" "$1" "$2" > "$T/cur.out" 2> "$T/stat" || rc=$? ;;
      *)   if [ -n "${2:-}" ]; then "$T/pk" "$1" < "$2" > "$T/cur.out" 2> "$T/stat" || rc=$?
           else "$T/pk" "$1" > "$T/cur.out" 2> "$T/stat" || rc=$?; fi ;;
    esac
    last=$(tail -1 "$T/stat")
    kb=$(to_kb "$(echo "$last" | awk '{print $(NF-1)}')"); ms=$(echo "$last" | awk '{print $NF}')
    case "$ms" in *[!0-9]*|"") ms=9999999 ;; esac
    [ "$ms" -lt "$bms" ] && bms=$ms && bkb=$kb
  done
  if [ "$rc" -ne 0 ]; then printf 'FAILED(rc=%s)' "$rc"; return 0; fi
  if [ -f "$T/ref.out" ]; then
    cmp -s "$T/cur.out" "$T/ref.out" || { awk "BEGIN{printf \"%5.1fMB/%s OUT-DIFF\", $bkb/1024.0, \"${bms}ms\"}"; return 0; }
  else cp "$T/cur.out" "$T/ref.out"; fi
  awk "BEGIN{printf \"%5.1fMB/%-6s\", $bkb/1024.0, \"${bms}ms\"}"
}
# json doc
./tychoc "$P/json_gen.ty" --emit-c -o "$T/jg" >/dev/null 2>&1 && cc -O3 -o "$T/jg" "$T/jg.c" -lm 2>/dev/null && "$T/jg" > "$T/doc.json" 2>/dev/null

row() { # label hi c rs go kk [input] [koka-input-mode: stdin|arg]
  rm -f "$T/ref.out"                          # output-identity reference, per row
  printf "| %-14s" "$1"
  if [ -f "$2" ] && ./tychoc "$2" --emit-c -o "$T/h" >/dev/null 2>&1 && cc -O3 -o "$T/h" "$T/h.c" -lm 2>/dev/null; then printf "| %-14s" "$(best "$T/h" "$7")"; else printf "| %-14s" "-"; fi
  if [ -f "$3" ] && cc -O3 -o "$T/c" "$3" -lm 2>/dev/null; then printf "| %-14s" "$(best "$T/c" "$7")"; else printf "| %-14s" "-"; fi
  if [ -f "$4" ] && rustc -C opt-level=3 -o "$T/r" "$4" 2>/dev/null; then printf "| %-14s" "$(best "$T/r" "$7")"; else printf "| %-14s" "-"; fi
  if [ -f "$5" ] && ( cd "$(dirname "$5")" && go build -o "$T/g" "$(basename "$5")" 2>/dev/null ); then printf "| %-14s" "$(best "$T/g" "$7")"; else printf "| %-14s" "-"; fi
  if [ -n "$6" ] && [ -f "$6" ] && command -v koka >/dev/null 2>&1 && koka -O2 --builddir="$T/.kk" -o "$T/k" "$6" >/dev/null 2>&1; then chmod +x "$T/k"; printf "| %-14s" "$(best "$T/k" "$7" "${8:-stdin}")"; else printf "| %-14s" "-"; fi
  echo "|"
}

echo "fair standard-opt sweep ($(date '+%Y-%m-%d')); peak RSS / best-of-3 wall"
echo "| workload       | tycho         | C            | Rust         | Go           | Koka         |"
echo "|----------------|--------------|--------------|--------------|--------------|--------------|"
row binary_trees  "$P/binary_trees.ty"   "$P/binary_trees.c"   "$P/binary_trees.rs"   "$P/binary_trees.go"   "$P/binarytrees.kk"
row maptree       "$P/maptree.ty"        "$P/maptree.c"        "$P/maptree.rs"        "$P/maptree.go"        "$P/maptree.kk"
row arr_pipeline  "$P/arr_pipeline.ty"   "$P/arr_pipeline.c"   "$P/arr_pipeline.rs"   "$P/arr_pipeline.go"   "$P/arrpipeline.kk"
row json_parse    "$P/json_parse.ty"     "$P/json_parse.c"     "$P/json_parse.rs"     "$P/json_parse.go"     "$P/jsonparse.kk"   "$T/doc.json"  arg
row gcscan        bench/gcscan/gcscan.ty bench/gcscan/gcscan.c bench/gcscan/gcscan.rs bench/gcscan/gcscan.go ""
row latency       bench/latency/latency.ty bench/latency/latency.c bench/latency/latency.rs bench/latency/latency.go ""
rm -rf "$T"
