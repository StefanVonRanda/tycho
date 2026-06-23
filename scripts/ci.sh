#!/bin/sh
# Local CI gate for tycho. NO GitHub Actions, no cloud -- this runs on your machine.
# It is the single source of truth for "is the tree green": build, golden +
# sanitizer tests, self-host fixpoint, and a differential fuzz campaign. Exits
# nonzero on the FIRST failure so it composes into hooks and `make ci`.
#
# Usage:
#   scripts/ci.sh [FUZZ_N]     FUZZ_N = fuzz seeds (default 500; 0 skips the fuzz)
#   make ci                    same, N defaults to 500 (override: make ci N=200)
set -eu
cd "$(dirname "$0")/.."
N="${1:-500}"
# Fail-closed: a non-numeric FUZZ_N must abort, not silently skip the fuzz.
case "$N" in
    *[!0-9]*|"") printf 'ci.sh: FUZZ_N must be a non-negative integer, got "%s"\n' "$N" >&2; exit 2 ;;
esac

bar() { printf '================================================================\n'; }
step() { printf '\n>>> %s\n' "$1"; }

bar
printf ' tycho local CI   (no GitHub Actions -- runs here, on this machine)\n'
printf ' fuzz seeds: %s\n' "$N"
bar

step "[1/12] build (make tychoc)"
make -s tychoc

step "[2/12] make test  (golden output + ASan/UBSan/LeakSanitizer)"
make -s test

step "[3/12] make fixpoint  (self-host B==C + packages + standalone driver)"
make -s fixpoint

step "[4/12] make corelib  (corelib packages + examples + the site dogfood: C compiler vs tychoc0 + goldens)"
make -s corelib
make -s corelib-examples
make -s site

step "[5/12] make conc  (spawn/parallel-for/channels: ASan+TSan + tychoc0 parity)"
make -s conc

step "[6/12] make ffi  (extern fn: both compilers vs golden, ASan-clean)"
make -s ffi

if [ "$N" -gt 0 ]; then
    step "[7/12] make fuzz N=$N  (differential tychoc vs tychoc0 + ASan/UBSan)"
    python3 fuzz/run.py "$N"
    step "[8/12] make fuzz-reject N=$N  (malformed input: both compilers must fail closed)"
    python3 fuzz/run_reject.py "$N"
    # leak lane is the slowest (sequential ASan+LeakSanitizer, both compilers per
    # seed) and leak bugs surface fast (seeds <50), so cap it to keep `make ci`
    # practical; `make fuzz-leak N=...` runs a deeper sweep.
    LN="$N"; [ "$LN" -gt 150 ] && LN=150
    step "[9/12] make fuzz-leak N=$LN  (LeakSanitizer: arena / owner-0 leaks)"
    python3 fuzz/run_leak.py "$LN"
else
    step "[7/12] fuzz lanes skipped (N=0)"
fi

step "[10/12] make tools-check  (formatter idempotence + semantic preservation + LSP smoke)"
sh scripts/tools_check.sh

step "[11/12] make typeparity  (binary-op operand types: tychoc and tychoc0 must agree on accept/reject)"
make -s typeparity

step "[12/12] bench-guard  (tree-alloc wall: tycho must beat C -- perf regression gate)"
sh bench/guard.sh

bar
printf ' CI GREEN -- tree is good\n'
bar
