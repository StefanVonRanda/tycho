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
# Fail-closed: a non-numeric FUZZ_N must abort, not silently skip the fuzz.
case "$N" in
    *[!0-9]*|"") printf 'ci.sh: FUZZ_N must be a non-negative integer, got "%s"\n' "$N" >&2; exit 2 ;;
esac

bar() { printf '================================================================\n'; }
step() { printf '\n>>> %s\n' "$1"; }

bar
printf ' hier local CI   (no GitHub Actions -- runs here, on this machine)\n'
printf ' fuzz seeds: %s\n' "$N"
bar

step "[1/7] build (make hierc)"
make -s hierc

step "[2/7] make test  (golden output + ASan/UBSan/LeakSanitizer)"
make -s test

step "[3/7] make fixpoint  (self-host B==C + packages + standalone driver)"
make -s fixpoint

step "[4/7] make corelib  (corelib packages: C compiler vs hierc0 + goldens)"
make -s corelib

step "[5/7] make ffi  (extern fn: both compilers vs golden, ASan-clean)"
make -s ffi

if [ "$N" -gt 0 ]; then
    step "[6/7] make fuzz N=$N  (differential hierc vs hierc0 + ASan/UBSan)"
    python3 fuzz/run.py "$N"
else
    step "[6/7] fuzz skipped (N=0)"
fi

step "[7/7] bench-guard  (tree-alloc wall: hier must beat C -- perf regression gate)"
sh bench/guard.sh

bar
printf ' CI GREEN -- tree is good\n'
bar
