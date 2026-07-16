#!/bin/sh
# Dogfood: a parallel Mandelbrot. Stresses a corner neither the int-only
# concurrency benchmarks nor the sequential ray tracer reach -- FLOAT compute
# inside a `parallel for` reduction. Each pixel's escape count is a pure function
# of its coordinates, so the row-sum reduction is interleaving-independent: stdout
# (the ASCII render + deterministic counts) is machine-independent and golden-
# locked, while the worker count goes to stderr. The C tychoc, the self-hosted
# tychoc0, a ThreadSanitizer build (no data race on the reduction), and an
# ASan/UBSan build must all print the SAME stdout (examples/mandelbrot/expected.out).
# Re-record the golden with:  RECORD=1 sh examples/mandelbrot/run.sh
set -u
cd "$(dirname "$0")/../.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
CC="${CC:-cc}"
RECORD="${RECORD:-0}"
golden=examples/mandelbrot/expected.out
SRC=examples/mandelbrot/main.ty
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
"$TYCHOC" compiler/tychoc0.ty -o "$T/h0" >/dev/null 2>&1 || { echo "FAIL: could not build tychoc0"; exit 1; }

# (1) C reference compiler
if ! "$TYCHOC" "$SRC" -o "$T/c" >"$T/c.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/c.log"; fail=1
else
    "$T/c" > "$T/c.out" 2>/dev/null
fi

# (2) standalone tychoc0 (resolves core: itself)
if ! { "$T/h0" "$SRC" > "$T/sd.c" 2>/dev/null && $CC -O2 -fwrapv -pthread "$T/sd.c" -o "$T/sdbin" -lm 2>"$T/sd.log"; }; then
    echo "FAIL: tychoc0 compile"; sed 's/^/      /' "$T/sd.log"; fail=1
else
    "$T/sdbin" > "$T/sd.out" 2>/dev/null
fi

# (3) ThreadSanitizer: the parallel-for reduction must be data-race-free
if ! { "$TYCHOC" "$SRC" --emit-c -o "$T/t" >/dev/null 2>&1 && \
       $CC -fsanitize=thread -g -O1 -pthread "$T/t.c" -o "$T/tsan" -lm 2>"$T/t.log"; }; then
    echo "FAIL: TSan cc"; sed 's/^/      /' "$T/t.log"; fail=1
else
    "$T/tsan" >/dev/null 2>"$T/tsan.err" || { echo "FAIL: TSan run"; sed 's/^/      /' "$T/tsan.err"; fail=1; }
    if grep -qiE 'data race|ThreadSanitizer' "$T/tsan.err"; then echo "FAIL: TSan data race"; sed 's/^/      /' "$T/tsan.err"; fail=1; fi
fi

# (4) ASan/UBSan over the emitted C
if ! { "$TYCHOC" "$SRC" --emit-c -o "$T/a" >/dev/null 2>&1 && \
       $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 -pthread "$T/a.c" -o "$T/asan" -lm 2>"$T/a.log"; }; then
    echo "FAIL: ASan cc"; sed 's/^/      /' "$T/a.log"; fail=1
else
    ASAN_OPTIONS=detect_leaks=1 "$T/asan" >/dev/null 2>"$T/asan.err" || { echo "FAIL: ASan fault"; sed 's/^/      /' "$T/asan.err"; fail=1; }
    if grep -qiE 'runtime error|Sanitizer|ERROR: ' "$T/asan.err"; then echo "FAIL: ASan report"; sed 's/^/      /' "$T/asan.err"; fail=1; fi
fi

if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$T/sd.out"; then
    echo "FAIL: tychoc vs tychoc0 differ"; diff "$T/c.out" "$T/sd.out" | sed 's/^/      /'; fail=1
fi
if [ "$RECORD" = 1 ]; then cp "$T/c.out" "$golden"; echo "rec  mandelbrot"; fi
if [ "$fail" -eq 0 ] && [ ! -f "$golden" ]; then echo "FAIL: no golden -- run RECORD=1"; fail=1; fi
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$golden"; then
    echo "FAIL: output != golden"; diff "$golden" "$T/c.out" | sed 's/^/      /'; fail=1
fi
[ "$fail" -eq 0 ] && echo "mandelbrot: green (float in a parallel-for reduction; tychoc == tychoc0 == TSan == ASan; deterministic)" || { echo "mandelbrot: FAIL"; exit 1; }
