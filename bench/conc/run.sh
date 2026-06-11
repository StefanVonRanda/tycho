#!/bin/sh
# Concurrency head-to-head: the same two workloads in hier / C / Go / Rust,
# measuring peak RSS + wall (bench/peakrss) with a cross-language checksum.
#
#   parreduce  compute-bound parallel reduction (hier `parallel for`,
#              C pthread chunks, Go goroutines, Rust scoped threads)
#   pipeline   1 producer -> bounded channel(cap 256) -> 4 consumers, 1e6
#              string payloads (hier channels deep-copy every payload twice
#              -- value semantics; C passes pointers, Go shares under GC,
#              Rust moves ownership)
#
# Fair-bench rule: each language at its standard optimized build (hier -O3
# via hierc, C -O3, go build, rustc -O). Each binary picks K = online cores
# itself. Skips any language whose toolchain is absent. NOT in `make ci`.
set -u
cd "$(dirname "$0")/../.." || exit 2
HIERC=./hierc
[ -x "$HIERC" ] || { echo "no ./hierc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
D=bench/conc
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
$CC -O2 -o "$T/peakrss" bench/peakrss.c || { echo "peakrss build failed"; exit 2; }
to_kb() { case "$(uname)" in Darwin) echo $(( $1 / 1024 ));; *) echo "$1";; esac; }
FAIL=0

run_one() {                                  # <label> <binary> ; uses $ref
    if [ ! -x "$2" ]; then printf '  %-6s %s\n' "$1" "(build skipped)"; return; fi
    "$T/peakrss" "$2" > "$T/out" 2> "$T/m"
    kb=$(to_kb "$(awk '{print $1}' "$T/m")"); rss=$(awk "BEGIN{printf \"%.1f\", $kb/1024}"); ms=$(awk '{print $2}' "$T/m"); out=$(cat "$T/out")
    printf '  %-6s %7s MB %7s ms   %s\n' "$1" "$rss" "$ms" "$out"
    if [ -z "$ref" ]; then ref="$out"; elif [ "$out" != "$ref" ]; then echo "  ^ CHECKSUM MISMATCH (expected $ref)"; FAIL=1; fi
}

bench() {                                    # <name>
    b=$1
    echo "$b:"
    ref=""
    if ! $HIERC "$D/$b.hi" -o "$T/${b}_hier" > "$T/hier_err" 2>&1; then
        echo "conc: HIER BUILD FAILED"; cat "$T/hier_err"; exit 2
    fi
    run_one hier "$T/${b}_hier"
    $CC -O3 -pthread "$D/$b.c" -o "$T/${b}_c" -lm 2>/dev/null
    run_one C "$T/${b}_c"
    if command -v go >/dev/null 2>&1; then
        cp "$D/$b.go" "$T/$b.go" && ( cd "$T" && go build -o "${b}_go" "$b.go" 2>/dev/null )
    fi
    run_one Go "$T/${b}_go"
    if command -v rustc >/dev/null 2>&1; then
        rustc -O "$D/$b.rs" -o "$T/${b}_rs" 2>/dev/null
    fi
    run_one Rust "$T/${b}_rs"
}

printf '  %-6s %10s %10s   %s\n' lang peakRSS time checksum
bench parreduce
bench pipeline
[ $FAIL -eq 0 ] && echo "conc bench: ok (checksums agree)" || { echo "conc bench: CHECKSUM FAILURE"; exit 1; }
