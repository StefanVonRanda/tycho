#!/bin/sh
# Dogfood: a parallel Mandelbrot. Stresses a corner neither the int-only
# concurrency benchmarks nor the sequential ray tracer reach -- FLOAT compute
# inside a `parallel for` reduction. Each pixel's escape count is a pure function
# of its coordinates, so the row-sum reduction is interleaving-independent: stdout
# (the ASCII render + deterministic counts) is machine-independent and golden-
# locked, while the worker count goes to stderr. tychoc's native build, a
# ThreadSanitizer build (no data race on the reduction), and an ASan/UBSan build
# must all print the SAME stdout (examples/mandelbrot/expected.out). Until
# 2026-07-26 the self-hosted tychoc0 was a fourth leg; it is frozen (see
# compiler/tychoc0.ty) and no gate builds it.
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
# mingw gcc ships no sanitizer runtime; Apple ASan ships no LeakSanitizer.
case "$(uname -s)" in
    *MSYS*|*MINGW*|*CYGWIN*) IS_WINDOWS=1; TYCHO_LSAN=0 ;;
    Darwin) IS_WINDOWS=0; TYCHO_LSAN=0 ;;
    *) IS_WINDOWS=0; TYCHO_LSAN=1 ;;
esac

# (1) C reference compiler
if ! "$TYCHOC" "$SRC" -o "$T/c" >"$T/c.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/c.log"; fail=1
else
    "$T/c" > "$T/c.out" 2>/dev/null
fi

# (2) ThreadSanitizer: the parallel-for reduction must be data-race-free
if [ "$IS_WINDOWS" = 1 ]; then
    echo "SKIP mandelbrot TSan leg (gcc has no Windows-target ThreadSanitizer at all; docs/internals/windows-port.md phase 2)"
elif ! { "$TYCHOC" "$SRC" --emit-c -o "$T/t" >/dev/null 2>&1 && \
       $CC -fsanitize=thread -g -O1 -pthread "$T/t.c" -o "$T/tsan" -lm 2>"$T/t.log"; }; then
    echo "FAIL: TSan cc"; sed 's/^/      /' "$T/t.log"; fail=1
else
    "$T/tsan" >/dev/null 2>"$T/tsan.err" || { echo "FAIL: TSan run"; sed 's/^/      /' "$T/tsan.err"; fail=1; }
    if grep -qiE 'data race|ThreadSanitizer' "$T/tsan.err"; then echo "FAIL: TSan data race"; sed 's/^/      /' "$T/tsan.err"; fail=1; fi
fi

# (3) ASan/UBSan over the emitted C
if [ "$IS_WINDOWS" = 1 ]; then
    echo "SKIP mandelbrot ASan/UBSan leg (mingw gcc ships no sanitizer runtime -- no -lasan/-lubsan; docs/internals/windows-port.md phase 2)"
elif ! { "$TYCHOC" "$SRC" --emit-c -o "$T/a" >/dev/null 2>&1 && \
       $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 -pthread "$T/a.c" -o "$T/asan" -lm 2>"$T/a.log"; }; then
    echo "FAIL: ASan cc"; sed 's/^/      /' "$T/a.log"; fail=1
else
    [ "$TYCHO_LSAN" = 1 ] || echo "SKIP mandelbrot LeakSanitizer (unavailable on macOS; ASan+UBSan still run)"
    ASAN_OPTIONS=detect_leaks=$TYCHO_LSAN "$T/asan" >/dev/null 2>"$T/asan.err" || { echo "FAIL: ASan fault"; sed 's/^/      /' "$T/asan.err"; fail=1; }
    if grep -qiE 'runtime error|Sanitizer|ERROR: ' "$T/asan.err"; then echo "FAIL: ASan report"; sed 's/^/      /' "$T/asan.err"; fail=1; fi
fi

if [ "$RECORD" = 1 ]; then cp "$T/c.out" "$golden"; echo "rec  mandelbrot"; fi
if [ "$fail" -eq 0 ] && [ ! -f "$golden" ]; then echo "FAIL: no golden -- run RECORD=1"; fail=1; fi
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$golden"; then
    echo "FAIL: output != golden"; diff "$golden" "$T/c.out" | sed 's/^/      /'; fail=1
fi
if [ "$IS_WINDOWS" = 1 ]; then SAN="TSan+ASan SKIPPED (no mingw runtime)"; else SAN="TSan == ASan"; fi
[ "$fail" -eq 0 ] && echo "mandelbrot: green (float in a parallel-for reduction; tychoc + $SAN; deterministic)" || { echo "mandelbrot: FAIL"; exit 1; }
