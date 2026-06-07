#!/bin/sh
# Fair "each language at its standard optimized build" sweep — peak RSS + wall.
#   hier : -O3 (its default; rides the C optimizer)   C/Rust : -O3
#   Go   : go build (Go's only optimized level)        Koka  : koka -O2 (its max)
# best-of-3 wall ms; peak RSS kb via bench/peakrss (prints "... rss_kb wall_ms").
cd /home/igzo/github/hier || exit 1
T=$(mktemp -d); cc -O2 -o "$T/pk" bench/peakrss.c || exit 1
P=bench/prongB
best() { bms=9999999; bkb=0
  for i in 1 2 3; do
    if [ -n "$2" ]; then o=$("$T/pk" "$1" < "$2" 2>&1); else o=$("$T/pk" "$1" 2>&1); fi
    last=$(echo "$o" | tail -1)
    kb=$(echo "$last" | awk '{print $(NF-1)}'); ms=$(echo "$last" | awk '{print $NF}')
    case "$ms" in *[!0-9]*|"") ms=9999999 ;; esac
    [ "$ms" -lt "$bms" ] && bms=$ms && bkb=$kb
  done
  awk "BEGIN{printf \"%5.1fMB/%-6s\", $bkb/1024.0, \"${bms}ms\"}"
}
# json doc
./hierc "$P/json_gen.hi" --emit-c -o "$T/jg" >/dev/null 2>&1 && cc -O3 -o "$T/jg" "$T/jg.c" -lm 2>/dev/null && "$T/jg" > "$T/doc.json" 2>/dev/null

row() { # label hi c rs go kk [stdin]
  printf "| %-14s" "$1"
  if [ -f "$2" ] && ./hierc "$2" --emit-c -o "$T/h" >/dev/null 2>&1 && cc -O3 -o "$T/h" "$T/h.c" -lm 2>/dev/null; then printf "| %-14s" "$(best "$T/h" "$7")"; else printf "| %-14s" "-"; fi
  if [ -f "$3" ] && cc -O3 -o "$T/c" "$3" -lm 2>/dev/null; then printf "| %-14s" "$(best "$T/c" "$7")"; else printf "| %-14s" "-"; fi
  if [ -f "$4" ] && rustc -C opt-level=3 -o "$T/r" "$4" 2>/dev/null; then printf "| %-14s" "$(best "$T/r" "$7")"; else printf "| %-14s" "-"; fi
  if [ -f "$5" ] && ( cd "$(dirname "$5")" && go build -o "$T/g" "$(basename "$5")" 2>/dev/null ); then printf "| %-14s" "$(best "$T/g" "$7")"; else printf "| %-14s" "-"; fi
  if [ -n "$6" ] && [ -f "$6" ] && command -v koka >/dev/null 2>&1 && koka -O2 --builddir="$T/.kk" -o "$T/k" "$6" >/dev/null 2>&1; then chmod +x "$T/k"; printf "| %-14s" "$(best "$T/k" "$7")"; else printf "| %-14s" "-"; fi
  echo "|"
}

echo "fair standard-opt sweep ($(date '+%Y-%m-%d')); peak RSS / best-of-3 wall"
echo "| workload       | hier         | C            | Rust         | Go           | Koka         |"
echo "|----------------|--------------|--------------|--------------|--------------|--------------|"
row binary_trees  "$P/binary_trees.hi"   "$P/binary_trees.c"   "$P/binary_trees.rs"   "$P/binary_trees.go"   "$P/binarytrees.kk"
row maptree       "$P/maptree.hi"        "$P/maptree.c"        "$P/maptree.rs"        "$P/maptree.go"        "$P/maptree.kk"
row arr_pipeline  "$P/arr_pipeline.hi"   "$P/arr_pipeline.c"   "$P/arr_pipeline.rs"   "$P/arr_pipeline.go"   "$P/arrpipeline.kk"
row json_parse    "$P/json_parse.hi"     "$P/json_parse.c"     "$P/json_parse.rs"     "$P/json_parse.go"     "$P/jsonparse.kk"   "$T/doc.json"
row gcscan        bench/gcscan/gcscan.hi bench/gcscan/gcscan.c bench/gcscan/gcscan.rs bench/gcscan/gcscan.go ""
row latency       bench/latency/latency.hi bench/latency/latency.c bench/latency/latency.rs bench/latency/latency.go ""
rm -rf "$T"
