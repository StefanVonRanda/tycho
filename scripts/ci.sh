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

step "[1/19] build (make tychoc)"
make -s tychoc

step "[2/19] make test  (golden output + ASan/UBSan/LeakSanitizer)"
make -s test

# `make test` above runs on this LP64 host, where long == int64 hides every width
# bug. This lane re-runs the SAME fixture suite under `gcc -m32` (ILP32: 32-bit
# long, 32-bit pointers), so anything that lowered Tycho `int` to a 32-bit C type
# truncates and reddens. tests/int64_width.ty is the in-glob fixture that makes it
# non-vacuous (every value there exceeds 2^31).
step "[2b/19] make ilp32  (fixture suite rebuilt under -m32: Tycho int stays 64-bit off LP64)"
make -s ilp32

# Both lanes above sanitize/rebuild the C tychoc EMITS. Neither -- nor anything
# else in this file before 2026-07-25 -- ever built src/tychoc.c itself with
# -fsanitize, so the compiler's OWN memory safety was unmeasured by every gate.
# That is how plan.md Phase 37's stack-buffer-overflow WRITE in parse_type_inner,
# reachable from a valid program, survived 16 phases and a full 1.0 freeze. This
# lane builds the compiler under ASan+UBSan and compiles the whole corpus with it.
# tests/generic_many_typaram_names.ty is the in-corpus fixture that makes it
# non-vacuous: restore the [256] bound Phase 37 removed and this lane reddens.
# ~14s total (7s to build the sanitized compiler, 7s for 527 compiles), so it runs
# every time with no subsetting. See scripts/asan_self.sh.
step "[2c/19] make asan-self  (the COMPILER built with ASan+UBSan, compiling the whole corpus)"
make -s asan-self

# `make test` above runs tychoc on the positive lane and tychoc0 only where it must
# REFUSE (tests/run.sh:159/:178/:199/:262), so a program tychoc accepts and tychoc0
# over-rejects scored green -- plan.md Phase 40's eleven and Phase 33's five all did.
# fixpoint below does cover most of this glob, but reports every cause as one string
# ("B differs from the C compiler", stderr discarded at compiler/fixpoint.sh:28) and
# never walks tests/warn/ or tools/*.ty. This lane runs both FRONTENDS only (--emit-c,
# no cc, no run) and names the refusal. ~3s, so it runs unsubsetted and reddens before
# the 3-stage bootstrap does. See scripts/frontparity.sh.
step "[2d/19] make frontparity  (tychoc0's frontend must ACCEPT every program tychoc accepts)"
make -s frontparity

step "[3/19] make fixpoint  (self-host B==C + packages + standalone driver)"
make -s fixpoint

# fixpoint above compares the two compilers only by OUTPUT, so a runtime feature
# present in one runtime and absent from the other is invisible to it.
step "[4/19] make rtparity  (the two runtimes: same env knobs, traps, arena-stats rows)"
make -s rtparity

step "[5/19] make corelib  (corelib packages + examples + the site dogfood: C compiler vs tychoc0 + goldens)"
make -s corelib
make -s corelib-examples
make -s site
make -s raytrace
make -s mandelbrot

step "[6/19] make conc  (spawn/parallel-for/channels: ASan+TSan + tychoc0 parity)"
make -s conc

step "[7/19] make ffi  (extern fn: both compilers vs golden, ASan-clean)"
make -s ffi

if [ "$N" -gt 0 ]; then
    step "[8/19] make fuzz N=$N  (differential tychoc vs tychoc0 + ASan/UBSan)"
    python3 fuzz/run.py "$N"
    step "[9/19] make fuzz-reject N=$N  (malformed input: both compilers must fail closed)"
    python3 fuzz/run_reject.py "$N"
    # leak lane is the slowest (sequential ASan+LeakSanitizer, both compilers per
    # seed) and leak bugs surface fast (seeds <50), so cap it to keep `make ci`
    # practical; `make fuzz-leak N=...` runs a deeper sweep.
    LN="$N"; [ "$LN" -gt 150 ] && LN=150
    step "[10/19] make fuzz-leak N=$LN  (LeakSanitizer: arena / owner-0 leaks)"
    python3 fuzz/run_leak.py "$LN"
    step "[10b/19] make fuzz-pkg N=$N  (cross-package differential: tychoc vs tychoc0 bundle vs standalone)"
    python3 fuzz/run_pkg.py "$N"
else
    step "[8/19] fuzz lanes skipped (N=0)"
fi

step "[11/19] make tools-check  (formatter idempotence + semantic preservation + LSP smoke)"
sh scripts/tools_check.sh

step "[12/19] make typeparity  (binary-op operand types: tychoc and tychoc0 must agree on accept/reject)"
make -s typeparity

step "[13/19] make parforparity  (parallel-for body gates: tychoc and tychoc0 must agree on accept/reject)"
make -s parforparity

step "[14/19] make eqparity  (composite/newtype ==,!= : tychoc and tychoc0 must agree on accept/reject)"
make -s eqparity

step "[15/19] make unaryparity  (unary -, ~, not : tychoc and tychoc0 must agree on accept/reject)"
make -s unaryparity

step "[16/19] bench-guard  (tree-alloc wall: tycho must beat C -- perf regression gate)"
sh bench/guard.sh

step "[17/19] make recursion  (deep input fails closed in both compilers -- no stack-overflow DoS)"
make -s recursion

step "[18/19] make spec-check  (spec: Appendix A grammar == §3/§4 · Appendix E fixtures exist · runnable examples match output on both compilers)"
make -s spec-check

step "[19/19] make check-links  (every relative Markdown link resolves to a real file; every provenance citation still resolves)"
make -s check-links

bar
printf ' CI GREEN -- tree is good\n'
bar
