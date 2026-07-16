#!/bin/sh
# Concurrency head-to-head: the same two workloads in tycho / C / Go / Rust,
# measuring peak RSS + wall (bench/peakrss) with a cross-language checksum.
#
#   parreduce  compute-bound parallel reduction (tycho `parallel for`,
#              C pthread chunks, Go goroutines, Rust scoped threads)
#   mandelbrot the same reduction shape but the kernel is FLOAT work -- a
#              1200x1200 Mandelbrot escape-count sum. Chaotic map, so it also
#              checks the float pipeline agrees bit-for-bit across all four ports
#              (the multiply is materialized to block FMA fusion)
#   pipeline   1 producer -> bounded channel(cap 256) -> 4 consumers, 1e6
#              string payloads (tycho channels deep-copy every payload twice
#              -- value semantics; C passes pointers, Go shares under GC,
#              Rust moves ownership)
#   pool       1 producer -> bounded channel(cap 256) -> K=cores workers, 1e6
#              int jobs each running a 50-step MINSTD kernel. tycho uses the
#              `parallel for x in ch:` sugar (no hand-spawned workers); Go is
#              the classic `for j := range jobs` WaitGroup pool; C is a
#              mutex+condvar ring; Rust shares one Receiver behind a Mutex
#
# Fair-bench rule: each language at its standard optimized build (tycho -O3
# via tychoc, C -O3, go build, rustc -O). Each binary picks K = online cores
# itself. Skips any language whose toolchain is absent. NOT in `make ci`.
set -u
cd "$(dirname "$0")/../.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
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
    if ! $TYCHOC "$D/$b.ty" -o "$T/${b}_tycho" > "$T/tycho_err" 2>&1; then
        echo "conc: TYCHO BUILD FAILED"; cat "$T/tycho_err"; exit 2
    fi
    run_one tycho "$T/${b}_tycho"
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
bench mandelbrot
bench pipeline
bench pool
[ $FAIL -eq 0 ] && echo "conc bench: ok (checksums agree)" || { echo "conc bench: CHECKSUM FAILURE"; exit 1; }
