#!/bin/sh
# Dogfood: a small ray tracer (diffuse + specular + one reflection bounce) rendered
# to a QOI image via core:raster. It stresses a corner the benchmarks don't --
# FLOAT-heavy struct VALUE SEMANTICS: every Vec3 op returns a fresh struct by value,
# copied through deep call chains and recursion (reflect), so the implicit arena
# carries the whole render with zero manual memory management. It is deterministic,
# so the C tychoc, the self-hosted tychoc0, and an ASan/UBSan build must all print
# the SAME summary line (a real-program float differential); the line is locked to
# examples/raytrace/expected.out, and the emitted out.qoi must be a valid QOI file.
# Re-record the golden with:  RECORD=1 sh examples/raytrace/run.sh
set -u
cd "$(dirname "$0")/../.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
CC="${CC:-cc}"
RECORD="${RECORD:-0}"
golden=examples/raytrace/expected.out
SRC=examples/raytrace/main.ty
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
"$TYCHOC" compiler/tychoc0.ty -o "$T/h0" >/dev/null 2>&1 || { echo "FAIL: could not build tychoc0"; exit 1; }

# (1) C reference compiler
if ! "$TYCHOC" "$SRC" -o "$T/c" >"$T/c.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/c.log"; fail=1
else
    ( cd "$T" && ./c ) > "$T/c.out" 2>&1
fi

# (2) standalone tychoc0 (resolves core: itself, emits the C we link)
if ! { "$T/h0" "$SRC" > "$T/sd.c" 2>/dev/null && $CC -O2 -fwrapv "$T/sd.c" -o "$T/sdbin" -lm 2>"$T/sd.log"; }; then
    echo "FAIL: tychoc0 compile"; sed 's/^/      /' "$T/sd.log"; fail=1
else
    ( cd "$T" && ./sdbin ) > "$T/sd.out" 2>&1
fi

# (3) ASan/UBSan over the emitted C (the render must be leak-free too)
if ! { "$TYCHOC" "$SRC" --emit-c -o "$T/a" >/dev/null 2>&1 && \
       $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 "$T/a.c" -o "$T/asan" -lm 2>"$T/a.log"; }; then
    echo "FAIL: sanitizer cc"; sed 's/^/      /' "$T/a.log"; fail=1
else
    ( cd "$T" && ASAN_OPTIONS=detect_leaks=1 ./asan ) > "$T/asan.out" 2>"$T/asan.err" || { echo "FAIL: sanitizer fault"; sed 's/^/      /' "$T/asan.err"; fail=1; }
    if grep -qiE 'runtime error|Sanitizer|ERROR: ' "$T/asan.err"; then echo "FAIL: sanitizer report"; sed 's/^/      /' "$T/asan.err"; fail=1; fi
fi

# The emitted out.qoi must be a real QOI file (magic 'qoif' = 113 111 105 102).
if [ "$fail" -eq 0 ]; then
    magic=$(od -A n -t u1 -N 4 "$T/out.qoi" 2>/dev/null | tr -s ' ')
    [ "$magic" = " 113 111 105 102" ] || { echo "FAIL: out.qoi is not a QOI file (magic:$magic)"; fail=1; }
fi

if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$T/sd.out"; then
    echo "FAIL: tychoc vs tychoc0 differ"; diff "$T/c.out" "$T/sd.out" | sed 's/^/      /'; fail=1
fi
if [ "$RECORD" = 1 ]; then cp "$T/c.out" "$golden"; echo "rec  raytrace"; fi
if [ "$fail" -eq 0 ] && [ ! -f "$golden" ]; then echo "FAIL: no golden -- run RECORD=1"; fail=1; fi
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$golden"; then
    echo "FAIL: output != golden"; diff "$golden" "$T/c.out" | sed 's/^/      /'; fail=1
fi
[ "$fail" -eq 0 ] && echo "raytrace: green (float-heavy Vec3 value semantics; tychoc == tychoc0 == ASan; valid QOI)" || { echo "raytrace: FAIL"; exit 1; }
