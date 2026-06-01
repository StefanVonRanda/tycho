#!/bin/sh
# Prong B — head-to-head memory benchmarks. Two workloads, the same program in
# Hier, C, Rust, Go, and Koka, built at -O and run under bench/peakrss (peak
# RSS + wall time). Every binary in a workload must print byte-identical output
# (the cross-language correctness check). See RESULTS.md.
#
#   binary-trees  allocate a sea of short-lived trees + one long-lived, checksum
#   tree-rewrite  map-rewrite a persistent tree many times, checksum each result
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
run_one() {                                   # <label> <binary>
    lbl="$1"; bin="$2"
    [ -x "$bin" ] || { printf '%-12s %10s   (not built)\n' "$lbl" "-"; return; }
    "$T/peakrss" "$bin" > "$T/out" 2> "$T/m"
    read rss ms < "$T/m"; kb="$(to_kb "$rss")"
    h="$(md5sum < "$T/out" | cut -c1-8)"
    [ -z "$ref" ] && ref="$h"
    [ "$h" = "$ref" ] && ok="ok" || { ok="OUTPUT DIFFERS"; fail=1; }
    printf '%-12s %8dMB %6dms   %s %s\n' "$lbl" "$(( kb / 1024 ))" "$ms" "$h" "$ok"
}

workload() {                                  # <title> <hi/c/rs/go base> <kk base>
    title="$1"; b="$2"; kk="$3"; ref=""
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

workload "binary-trees (allocate / discard)"     binary_trees binarytrees
workload "tree-rewrite (map a persistent tree)"   maptree      maptree
workload "array-pipeline (flat-array passes)"     arr_pipeline arrpipeline
workload "string-pipeline (build + hash strings)" string_pipe  stringpipe

echo "-----------------------------------------------------------"
[ "$fail" -eq 0 ] && echo "all outputs identical within each workload" || { echo "OUTPUT MISMATCH"; exit 1; }
