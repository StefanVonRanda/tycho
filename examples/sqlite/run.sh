#!/bin/sh
# Dogfood: tycho's FFI against real SQLite (in-memory). Builds demo.ty with tychoc
# and runs the emitted C under ASan/UBSan — proving the column-text arena-copy is
# use-after-free-free against a real library whose text pointer is only valid
# until the next step()/finalize(). Asserts the output matches the golden. Skips
# cleanly if libsqlite3 is absent.
# Re-record the golden with: RECORD=1 sh run.sh
#
# RETIRED LEG, 2026-07-29. Until then a second leg built the self-hosted tychoc0
# from compiler/tychoc0.ty, piped `tychoc --bundle` through it, linked libsqlite3
# by hand, and required its output to be byte-identical to tychoc's. tychoc0 is
# FROZEN and the breaking loop-syntax change of 2026-07-29 means it can no longer
# parse the corpus, so no lane builds it -- see the header of compiler/fixpoint.sh,
# plus ROADMAP.md and docs/architecture.md. What is lost: this was one of only
# four runners that fed the frozen compiler a real corelib/FFI program, and the
# only one that proved it handled direct `extern fn` bindings with no shim file.
#
# NOTE, a coupling that could have been broken silently: the ASan leg (3) below
# sanitized the C that TYCHOC0 emitted, so removing leg (2) would have removed the
# sanitizer's input with it. It is repointed at tychoc's own `--emit-c` output.
# The use-after-free this lane exists to catch is in the arena copy of a transient
# sqlite3_column_text pointer, not in either compiler's codegen, so sanitizing
# tychoc's C still covers it -- but it is now one implementation, not two.
set -u
cd "$(dirname "$0")" || exit 2
TYCHOC=../../tychoc
[ -x "$TYCHOC" ] || { echo "no ../../tychoc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
if ! pkg-config --exists sqlite3 2>/dev/null; then echo "sqlite: SKIP (libsqlite3 not installed)"; exit 0; fi
LIBS="$(pkg-config --libs sqlite3)"
RECORD="${RECORD:-0}"
golden=expected.out
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
# mingw gcc ships no sanitizer runtime -- see the SKIP at the sanitizer leg below
case "$(uname -s)" in *MSYS*|*MINGW*|*CYGWIN*) IS_WINDOWS=1 ;; *) IS_WINDOWS=0 ;; esac

# (1) C reference compiler: direct sqlite3_* bindings, NO --shim; --pkg links libsqlite3.
if ! "$TYCHOC" demo.ty -o "$T/c" --pkg sqlite3 >"$T/c.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/c.log"; fail=1
else
    "$T/c" > "$T/c.out" 2>&1
fi

# (2) ASan/UBSan over tychoc's emitted C: the transient column-text pointer must
# have been copied into the arena (else use-after-free when we print past the next
# step). --emit-c so we link libsqlite3 ourselves under the sanitizer flags.
# (Before 2026-07-29 this sanitized tychoc0's C; see the header.)
if ! "$TYCHOC" demo.ty --emit-c -o "$T/san_src" >"$T/emit.log" 2>&1; then
    echo "FAIL: tychoc --emit-c"; sed 's/^/      /' "$T/emit.log"; fail=1
elif [ "$IS_WINDOWS" = 1 ]; then
    echo "SKIP sqlite ASan/UBSan leg (mingw gcc ships no sanitizer runtime -- no -lasan/-lubsan; docs/internals/windows-port.md phase 2)"
elif ! $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 -std=c11 \
        "$T/san_src.c" -o "$T/san" $LIBS 2>"$T/san.log"; then
    echo "FAIL: sanitizer cc"; sed 's/^/      /' "$T/san.log"; fail=1
else
    ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$T/san" > "$T/san.out" 2>"$T/san.err" || { echo "FAIL: sanitizer fault"; sed 's/^/      /' "$T/san.err"; fail=1; }
    if grep -qiE 'runtime error|AddressSanitizer|Sanitizer|ERROR: ' "$T/san.err"; then echo "FAIL: sanitizer report"; sed 's/^/      /' "$T/san.err"; fail=1; fi
fi

if [ "$RECORD" = 1 ]; then cp "$T/c.out" "$golden"; echo "rec  sqlite"; fi
if [ "$fail" -eq 0 ] && [ ! -f "$golden" ]; then echo "FAIL: no golden -- run RECORD=1"; fail=1; fi
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$golden"; then
    echo "FAIL: output != golden"; diff "$golden" "$T/c.out" | sed 's/^/      /'; fail=1
fi
if [ "$IS_WINDOWS" = 1 ]; then SAN="ASan SKIPPED (no mingw runtime)"; else SAN="ASan-clean"; fi
[ "$fail" -eq 0 ] && echo "sqlite: green (tychoc $SAN, matches golden -- real libsqlite3 via --pkg, ZERO hand-written shim; the tychoc0 leg was retired 2026-07-29)" || { echo "sqlite: FAIL"; exit 1; }
