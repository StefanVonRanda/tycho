#!/bin/sh
# Prong B — head-to-head memory benchmarks. Two workloads, the same program in
# Hier, C, Rust, Go, and Koka, built at -O and run under bench/peakrss (peak
# RSS + wall time). Every binary in a workload must print byte-identical output
# (the cross-language correctness check). See RESULTS.md.
#
#   binary-trees   allocate a sea of short-lived trees + one long-lived, checksum
#   tree-rewrite   map-rewrite a persistent tree many times, checksum each result
#   iter-transform reassign a loop-carried value each step — the arena's WORST
#                  case (every dead intermediate retained until scope exit)
#
# Languages: Hier (implicit arenas), C (manual malloc/free), Rust (Box/RAII),
# Go (GC), Koka (Perceus reference counting + reuse — the direct rival).
set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
HIERC=./hierc
[ -x "$HIERC" ] || { echo "no ./hierc — run 'make' first"; exit 2; }
D=bench/prongB
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
CC="${CC:-cc}"
$CC -O2 -o "$T/peakrss" bench/peakrss.c || { echo "peakrss build failed"; exit 2; }
to_kb() { case "$(uname)" in Darwin) echo $(( $1 / 1024 ));; *) echo "$1";; esac; }

fail=0; ref=""
REC="$T/records"; : > "$REC"                   # "<workload> <lang> <rssMB> <ms>" rows, for the summary scorecard
WL="-"                                         # current workload slug (set by workload()/json_workload())
run_one() {                                   # <label> <binary>
    lbl="$1"; bin="$2"
    [ -x "$bin" ] || { printf '%-12s %10s   (not built)\n' "$lbl" "-"; return; }
    "$T/peakrss" "$bin" > "$T/out" 2> "$T/m"
    read rss ms < "$T/m"; kb="$(to_kb "$rss")"
    h="$(md5sum < "$T/out" | cut -c1-8)"
    [ -z "$ref" ] && ref="$h"
    [ "$h" = "$ref" ] && ok="ok" || { ok="OUTPUT DIFFERS"; fail=1; }
    printf '%-12s %8dMB %6dms   %s %s\n' "$lbl" "$(( kb / 1024 ))" "$ms" "$h" "$ok"
    printf '%s %s %d %d\n' "$WL" "$lbl" "$(( kb / 1024 ))" "$ms" >> "$REC"
}

workload() {                                  # <title> <hi/c/rs/go base> <kk base>
    title="$1"; b="$2"; kk="$3"; ref=""; WL="$b"
    echo "== $title =="
    printf '%-12s %10s %8s   %s\n' lang peakRSS time output
    $HIERC "$D/$b.hi" --emit-c -o "$T/w_hier" >/dev/null 2>&1 && $CC -O2 -o "$T/w_hier" "$T/w_hier.c"
    run_one hier "$T/w_hier"
    if [ -f "$D/${b}_scoped.hi" ]; then
        $HIERC "$D/${b}_scoped.hi" --emit-c -o "$T/w_hiers" >/dev/null 2>&1 && $CC -O2 -o "$T/w_hiers" "$T/w_hiers.c"
        run_one hier_scoped "$T/w_hiers"
    fi
    $CC -O2 -o "$T/w_c" "$D/$b.c" 2>/dev/null; run_one c "$T/w_c"
    command -v rustc >/dev/null 2>&1 && rustc -O -o "$T/w_rs" "$D/$b.rs" 2>/dev/null
    run_one rust "$T/w_rs"
    if command -v go >/dev/null 2>&1; then cp "$D/$b.go" "$T/w.go" && ( cd "$T" && go build -o w_go w.go 2>/dev/null ); fi
    run_one go "$T/w_go"
    if command -v koka >/dev/null 2>&1; then koka -O2 --builddir="$T/.koka" -o "$T/w_koka" "$D/$kk.kk" >/dev/null 2>&1 && chmod +x "$T/w_koka"; fi
    run_one koka "$T/w_koka"
    echo
}

# The json-parse workload reads a generated doc: the four C-family parsers take
# it on stdin; Koka (UTF-8 strings, no stdin slurp) reads it as a file-path arg.
json_run() {                                  # <label> <binary> <stdin|arg>
    lbl="$1"; bin="$2"; m="$3"
    [ -x "$bin" ] || { printf '%-12s %10s   (not built)\n' "$lbl" "-"; return; }
    if [ "$m" = stdin ]; then "$T/peakrss" "$bin" < "$T/json_in" > "$T/out" 2> "$T/m"
    else                     "$T/peakrss" "$bin" "$T/json_in"   > "$T/out" 2> "$T/m"; fi
    read rss ms < "$T/m"; kb="$(to_kb "$rss")"
    h="$(md5sum < "$T/out" | cut -c1-8)"
    [ -z "$ref" ] && ref="$h"
    [ "$h" = "$ref" ] && ok="ok" || { ok="OUTPUT DIFFERS"; fail=1; }
    printf '%-12s %8dMB %6dms   %s %s\n' "$lbl" "$(( kb / 1024 ))" "$ms" "$h" "$ok"
    printf '%s %s %d %d\n' "$WL" "$lbl" "$(( kb / 1024 ))" "$ms" >> "$REC"
}
json_workload() {
    ref=""; WL="json_parse"
    echo "== json-parse (recursive-descent: K parse-and-discard passes over a ~4.4MB doc) =="
    printf '%-12s %10s %8s   %s\n' lang peakRSS time output
    $HIERC "$D/json_gen.hi" --emit-c -o "$T/jgen" >/dev/null 2>&1 && $CC -O2 -o "$T/jgen" "$T/jgen.c"
    "$T/jgen" > "$T/json_in"
    $HIERC "$D/json_parse.hi" --emit-c -o "$T/j_hi" >/dev/null 2>&1 && $CC -O2 -o "$T/j_hi" "$T/j_hi.c"
    json_run hier "$T/j_hi" stdin
    $CC -O2 -o "$T/j_c" "$D/json_parse.c" 2>/dev/null; json_run c "$T/j_c" stdin
    command -v rustc >/dev/null 2>&1 && rustc -O -o "$T/j_rs" "$D/json_parse.rs" 2>/dev/null
    json_run rust "$T/j_rs" stdin
    if command -v go >/dev/null 2>&1; then cp "$D/json_parse.go" "$T/jg.go" && ( cd "$T" && go build -o j_go jg.go 2>/dev/null ); fi
    json_run go "$T/j_go" stdin
    if command -v koka >/dev/null 2>&1; then koka -O2 --builddir="$T/.kkj" -o "$T/j_kk" "$D/jsonparse.kk" >/dev/null 2>&1 && chmod +x "$T/j_kk"; fi
    json_run koka "$T/j_kk" arg
    echo
}

summary() {                                   # normalized head-to-head scorecard from the accumulated records
    ORDER="binary_trees maptree arr_pipeline string_pipe iter_transform json_parse"
    echo "==========================================================="
    echo "SUMMARY — peak RSS (MB).  'hier/C' is the headline thesis metric:"
    echo "implicit arenas vs hand-written malloc/free; ~1x = parity, lower is better."
    awk -v ord="$ORDER" '
      function v(w,l){ return (m[w"|"l]!="")? m[w"|"l] : "-" }
      { m[$1"|"$2]=$3 }
      END{ n=split(ord,o," ")
        printf "  %-16s %7s %7s %7s %7s %7s   %7s\n","workload","hier","c","rust","go","koka","hier/C"
        for(i=1;i<=n;i++){ w=o[i]; h=m[w"|hier"]; c=m[w"|c"]
          r=(h!=""&&c!=""&&c+0>0)? sprintf("%.2fx",h/c) : "-"
          printf "  %-16s %7s %7s %7s %7s %7s   %7s\n", w, v(w,"hier"),v(w,"c"),v(w,"rust"),v(w,"go"),v(w,"koka"), r }
      }' "$REC"
    echo
    echo "SUMMARY — wall time (ms):"
    awk -v ord="$ORDER" '
      function v(w,l){ return (t[w"|"l]!="")? t[w"|"l] : "-" }
      { t[$1"|"$2]=$4 }
      END{ n=split(ord,o," ")
        printf "  %-16s %7s %7s %7s %7s %7s\n","workload","hier","c","rust","go","koka"
        for(i=1;i<=n;i++){ w=o[i]; printf "  %-16s %7s %7s %7s %7s %7s\n", w, v(w,"hier"),v(w,"c"),v(w,"rust"),v(w,"go"),v(w,"koka") }
      }' "$REC"
    echo
}

workload "binary-trees (allocate / discard)"     binary_trees binarytrees
workload "tree-rewrite (map a persistent tree)"   maptree      maptree
workload "array-pipeline (flat-array passes)"     arr_pipeline arrpipeline
workload "string-pipeline (build + hash strings)" string_pipe  stringpipe
workload "iter-transform (the arena's WORST case: reassign a loop-carried value)" iter_transform itertransform
json_workload

summary
echo "-----------------------------------------------------------"
[ "$fail" -eq 0 ] && echo "all outputs identical within each workload" || { echo "OUTPUT MISMATCH"; exit 1; }
