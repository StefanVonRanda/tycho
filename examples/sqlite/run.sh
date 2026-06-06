#!/bin/sh
# Dogfood: hier's FFI against real SQLite (in-memory). Builds demo.hi via BOTH
# compilers and runs the hierc0 output under ASan/UBSan too — proving the
# column-text arena-copy is use-after-free-free against a real library whose
# text pointer is only valid until the next step()/finalize(). Asserts both
# compilers agree and match the golden. Skips cleanly if libsqlite3 is absent.
# Re-record the golden with: RECORD=1 sh run.sh
set -u
cd "$(dirname "$0")" || exit 2
HIERC=../../hierc
[ -x "$HIERC" ] || { echo "no ../../hierc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
if ! pkg-config --exists sqlite3 2>/dev/null; then echo "sqlite: SKIP (libsqlite3 not installed)"; exit 0; fi
LIBS="$(pkg-config --libs sqlite3)"
RECORD="${RECORD:-0}"
golden=expected.out
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
"$HIERC" ../../compiler/hierc0.hi -o "$T/h0" >/dev/null 2>&1 || { echo "FAIL: could not build hierc0"; exit 1; }

# (1) C reference compiler: --shim provides sx_*, --pkg links libsqlite3.
if ! "$HIERC" demo.hi -o "$T/c" --shim sqlite_shim.c --pkg sqlite3 >"$T/c.log" 2>&1; then
    echo "FAIL: hierc compile"; sed 's/^/      /' "$T/c.log"; fail=1
else
    "$T/c" > "$T/c.out" 2>&1
fi

# (2) self-hosted compiler: emit C, then link the shim + libsqlite3 ourselves.
if ! { "$HIERC" demo.hi --bundle 2>/dev/null | "$T/h0" > "$T/h0.c" 2>/dev/null && \
       $CC -O2 -std=c11 "$T/h0.c" sqlite_shim.c -o "$T/h0bin" $LIBS 2>"$T/h0.log"; }; then
    echo "FAIL: hierc0 compile"; sed 's/^/      /' "$T/h0.log"; fail=1
else
    "$T/h0bin" > "$T/h0.out" 2>&1
fi

# (3) ASan/UBSan over the emitted C: the transient column-text pointer must have
# been copied into the arena (else use-after-free when we print past the next step).
if ! $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 -std=c11 \
        "$T/h0.c" sqlite_shim.c -o "$T/san" $LIBS 2>"$T/san.log"; then
    echo "FAIL: sanitizer cc"; sed 's/^/      /' "$T/san.log"; fail=1
else
    ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$T/san" > "$T/san.out" 2>"$T/san.err" || { echo "FAIL: sanitizer fault"; sed 's/^/      /' "$T/san.err"; fail=1; }
    if grep -qiE 'runtime error|AddressSanitizer|Sanitizer|ERROR: ' "$T/san.err"; then echo "FAIL: sanitizer report"; sed 's/^/      /' "$T/san.err"; fail=1; fi
fi

if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$T/h0.out"; then
    echo "FAIL: hierc vs hierc0 output differ"; diff "$T/c.out" "$T/h0.out" | sed 's/^/      /'; fail=1
fi
if [ "$RECORD" = 1 ]; then cp "$T/c.out" "$golden"; echo "rec  sqlite"; fi
if [ "$fail" -eq 0 ] && [ ! -f "$golden" ]; then echo "FAIL: no golden -- run RECORD=1"; fail=1; fi
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$golden"; then
    echo "FAIL: output != golden"; diff "$golden" "$T/c.out" | sed 's/^/      /'; fail=1
fi
[ "$fail" -eq 0 ] && echo "sqlite: green (hierc + hierc0 agree, ASan-clean, match golden -- real libsqlite3 via --shim + --pkg)" || { echo "sqlite: FAIL"; exit 1; }
