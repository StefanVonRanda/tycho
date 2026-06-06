#!/bin/sh
# FFI Stage 1 regression harness. Builds the fixture C lib (tests/ffi/demo.c),
# then compiles tests/ffi/main.hi BOTH ways — via the C reference compiler
# (hierc, which links -lffidemo itself from `extern "ffidemo"`) and via the
# self-hosted compiler (hierc0, which emits C that we link) — and asserts both
# produce the golden tests/ffi/expected.out. Also recompiles the emitted C under
# ASan/UBSan to prove the string-return arena-copy is memory-clean (no UAF/leak).
# Re-record the golden with RECORD=1 sh tests/ffi/run.sh.
set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
HIERC=./hierc
[ -x "$HIERC" ] || { echo "no ./hierc — run 'make' first"; exit 2; }
CC="${CC:-cc}"
RECORD="${RECORD:-0}"
golden="tests/ffi/expected.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0

# fixture C library: libffidemo.a (static, so the binary needs no LD path at run time)
$CC -O2 -c tests/ffi/demo.c -o "$T/demo.o" || { echo "FAIL: compiling demo.c"; exit 1; }
ar rcs "$T/libffidemo.a" "$T/demo.o"

# the self-hosted compiler
"$HIERC" compiler/hierc0.hi -o "$T/h0" >/dev/null 2>&1 || { echo "FAIL: could not build hierc0"; exit 1; }

# (1) C reference compiler: it resolves `extern "ffidemo"` -> -lffidemo on its own
# cc line; LIBRARY_PATH points the linker at our static lib.
if ! LIBRARY_PATH="$T" "$HIERC" tests/ffi/main.hi -o "$T/c_bin" >"$T/c.log" 2>&1; then
    echo "FAIL: hierc compile"; sed 's/^/      /' "$T/c.log"; fail=1
else
    "$T/c_bin" > "$T/c.out" 2>&1
fi

# (2) self-hosted compiler: emits C to stdout; we compile+link it ourselves.
if ! { "$T/h0" tests/ffi/main.hi > "$T/h0.c" 2>/dev/null && \
       LIBRARY_PATH="$T" $CC -O2 -std=c11 -o "$T/h0_bin" "$T/h0.c" -lffidemo -lm 2>"$T/h0.log"; }; then
    echo "FAIL: hierc0 compile"; sed 's/^/      /' "$T/h0.log"; fail=1
else
    "$T/h0_bin" > "$T/h0.out" 2>&1
fi

# (3) ASan/UBSan over the emitted C: the str-return arena-copy must be clean.
if ! LIBRARY_PATH="$T" $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 \
        -std=c11 -o "$T/h0_san" "$T/h0.c" -lffidemo -lm 2>"$T/san.log"; then
    echo "FAIL: sanitizer cc"; sed 's/^/      /' "$T/san.log"; fail=1
else
    "$T/h0_san" > "$T/san.out" 2>"$T/san.err"; src=$?
    [ "$src" -eq 0 ] || { echo "FAIL: sanitizer exit $src"; sed 's/^/      /' "$T/san.err"; fail=1; }
    grep -qiE 'runtime error|AddressSanitizer|Sanitizer|ERROR: ' "$T/san.err" && { echo "FAIL: sanitizer report"; sed 's/^/      /' "$T/san.err"; fail=1; }
fi

# the two compilers must agree
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$T/h0.out"; then
    echo "FAIL: hierc vs hierc0 output differ"; diff "$T/c.out" "$T/h0.out" | sed 's/^/      /'; fail=1
fi

if [ "$RECORD" = 1 ]; then cp "$T/c.out" "$golden"; echo "rec  ffi"; fi
if [ "$fail" -eq 0 ] && [ ! -f "$golden" ]; then echo "FAIL: no golden — run RECORD=1"; fail=1; fi
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$golden"; then
    echo "FAIL: output != golden"; diff "$golden" "$T/c.out" | sed 's/^/      /'; fail=1
fi

[ "$fail" -eq 0 ] && echo "ffi: green (hierc + hierc0 agree, ASan-clean, match golden — scalar+string both directions)" || { echo "ffi: FAIL"; exit 1; }
