#!/bin/sh
# Local CI gate for hier. NO GitHub Actions, no cloud -- this runs on your machine.
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

bar() { printf '================================================================\n'; }
step() { printf '\n>>> %s\n' "$1"; }

bar
printf ' hier local CI   (no GitHub Actions -- runs here, on this machine)\n'
printf ' fuzz seeds: %s\n' "$N"
bar

step "[1/6] build (make hierc)"
make -s hierc

step "[2/6] make test  (golden output + ASan/UBSan/LeakSanitizer)"
make -s test

step "[3/6] make fixpoint  (self-host B==C + packages + standalone driver)"
make -s fixpoint

step "[4/6] make corelib  (corelib packages: C compiler vs hierc0 + goldens)"
make -s corelib

step "[5/6] make ffi  (extern fn: both compilers vs golden, ASan-clean)"
make -s ffi

if [ "$N" -gt 0 ] 2>/dev/null; then
    step "[6/6] make fuzz N=$N  (differential hierc vs hierc0 + ASan/UBSan)"
    python3 fuzz/run.py "$N"
else
    step "[6/6] fuzz skipped (N=0)"
fi

bar
printf ' CI GREEN -- tree is good\n'
bar
