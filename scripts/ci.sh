#!/bin/sh
# Local CI gate for tycho. NO GitHub Actions, no cloud -- this runs on your machine.
# It is the single source of truth for "is the tree green": build, golden +
# sanitizer tests, self-host fixpoint, and a differential fuzz campaign. Exits
# nonzero on the FIRST failure so it composes into hooks and `make ci`.
#
# Usage:
#   scripts/ci.sh [FUZZ_N]     FUZZ_N = fuzz seeds (default 200; 0 skips the fuzz)
#   make ci                    same, N defaults to 200 (override: make ci N=500 for a deeper sweep)
set -eu
cd "$(dirname "$0")/.."
N="${1:-200}"
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

step "[1/16] build (make tychoc)"
make -s tychoc

step "[2/16] make test  (golden output + ASan/UBSan/LeakSanitizer)"
make -s test

step "[3/16] make fixpoint  (self-host B==C + packages + standalone driver)"
make -s fixpoint

step "[4/16] make corelib  (corelib packages + examples + the site dogfood: C compiler vs tychoc0 + goldens)"
make -s corelib
make -s corelib-examples
make -s site

step "[5/16] make conc  (spawn/parallel-for/channels: ASan+TSan + tychoc0 parity)"
make -s conc

step "[6/16] make ffi  (extern fn: both compilers vs golden, ASan-clean)"
make -s ffi

if [ "$N" -gt 0 ]; then
    step "[7/16] make fuzz N=$N  (differential tychoc vs tychoc0 + ASan/UBSan)"
    python3 fuzz/run.py "$N"
    step "[8/16] make fuzz-reject N=$N  (malformed input: both compilers must fail closed)"
    python3 fuzz/run_reject.py "$N"
    # leak lane is the slowest (sequential ASan+LeakSanitizer, both compilers per
    # seed) and leak bugs surface fast (seeds <50), so cap it to keep `make ci`
    # practical; `make fuzz-leak N=...` runs a deeper sweep.
    LN="$N"; [ "$LN" -gt 150 ] && LN=150
    step "[9/16] make fuzz-leak N=$LN  (LeakSanitizer: arena / owner-0 leaks)"
    python3 fuzz/run_leak.py "$LN"
else
    step "[7/16] fuzz lanes skipped (N=0)"
fi

step "[10/16] make tools-check  (formatter idempotence + semantic preservation + LSP smoke)"
sh scripts/tools_check.sh

step "[11/16] make typeparity  (binary-op operand types: tychoc and tychoc0 must agree on accept/reject)"
make -s typeparity

step "[12/16] make parforparity  (parallel-for body gates: tychoc and tychoc0 must agree on accept/reject)"
make -s parforparity

step "[13/16] make eqparity  (composite/newtype ==,!= : tychoc and tychoc0 must agree on accept/reject)"
make -s eqparity

step "[14/16] make unaryparity  (unary -, ~, not : tychoc and tychoc0 must agree on accept/reject)"
make -s unaryparity

step "[15/16] bench-guard  (tree-alloc wall: tycho must beat C -- perf regression gate)"
sh bench/guard.sh

step "[16/16] make recursion  (deep input fails closed in both compilers -- no stack-overflow DoS)"
make -s recursion

bar
printf ' CI GREEN -- tree is good\n'
bar
