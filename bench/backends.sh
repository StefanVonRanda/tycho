#!/bin/sh
# Run benchmark harnesses under more than one C backend.
#
# Tycho emits C, so a Tycho-vs-C result built by different backends confounds
# the language with the compiler. This keeps the same source and checksum but
# reports gcc and clang side by side. Rust is LLVM, so the clang column is the
# closer backend comparison for it; the gcc column is the shipped default.
#
# Usage:
#   sh bench/backends.sh                         # latency, the hot-loop row
#   sh bench/backends.sh bench/prongB/run.sh
#   BENCH_BACKENDS="gcc clang" sh bench/backends.sh ...
set -u
cd "$(dirname "$0")/.." || exit 2
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "no $TYCHOC -- run 'make' first"; exit 2; }
[ "$#" -gt 0 ] || set -- bench/latency/run.sh
for cc in ${BENCH_BACKENDS:-gcc clang}; do
    command -v "$cc" >/dev/null 2>&1 || { echo "skip $cc (not installed)"; continue; }
    echo "===== backend: $cc ====="
    CC="$cc" TYCHOC="$TYCHOC" sh "$@" || exit 2
done
