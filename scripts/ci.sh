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

step "[1/4] build (make hierc)"
make -s hierc

step "[2/4] make test  (golden output + ASan/UBSan/LeakSanitizer)"
make -s test

step "[3/4] make fixpoint  (self-host B==C + packages + standalone driver)"
make -s fixpoint

if [ "$N" -gt 0 ] 2>/dev/null; then
    step "[4/4] make fuzz N=$N  (differential hierc vs hierc0 + ASan/UBSan)"
    python3 fuzz/run.py "$N"
else
    step "[4/4] fuzz skipped (N=0)"
fi

bar
printf ' CI GREEN -- tree is good\n'
bar
