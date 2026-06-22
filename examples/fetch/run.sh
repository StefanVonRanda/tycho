#!/bin/sh
# Dogfood: the `fetch` CLI composes core:http + json + sha256 + io + path. It is
# built by the C compiler AND the self-hosted tychoc0, and run against a local
# file:// fixture so the WHOLE pipeline (GET, parse, hash, cache) is exercised
# deterministically and offline (a real https GET is verified by hand). The
# emitted C is also run under ASan/UBSan to prove the curl response body's
# arena-copy is use-after-free-free. Skips if libcurl is absent.
# Re-record the golden with:  RECORD=1 sh examples/fetch/run.sh
set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
CC="${CC:-cc}"
if ! pkg-config --exists libcurl 2>/dev/null; then echo "fetch: SKIP (libcurl not installed)"; exit 0; fi
DEPF="$(pkg-config --cflags --libs libcurl)"
SHIM=corelib/http/http_shim.c
RECORD="${RECORD:-0}"
golden=examples/fetch/expected.out
URL="file://$PWD/examples/fetch/fixture.json"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
"$TYCHOC" compiler/tychoc0.ty -o "$T/h0" >/dev/null 2>&1 || { echo "FAIL: could not build tychoc0"; exit 1; }

# (1) C reference compiler (auto-discovers the core:http shim + deps)
if ! "$TYCHOC" examples/fetch/main.ty -o "$T/c" >"$T/c.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/c.log"; fail=1
else
    "$T/c" "$URL" > "$T/c.out" 2>&1
fi

# (2) self-hosted tychoc0: emit C, link the shim + libcurl ourselves
if ! { "$TYCHOC" examples/fetch/main.ty --bundle 2>/dev/null | "$T/h0" > "$T/h0.c" 2>/dev/null && \
       $CC -O2 "$T/h0.c" "$SHIM" -o "$T/h0bin" -lm $DEPF 2>"$T/h0.log"; }; then
    echo "FAIL: tychoc0 compile"; sed 's/^/      /' "$T/h0.log"; fail=1
else
    "$T/h0bin" "$URL" > "$T/h0.out" 2>&1
fi

# (3) ASan/UBSan: the curl body pointer is transient, so it must be copied into
# the arena before the handle is released (else use-after-free on print/hash).
if ! $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 "$T/h0.c" "$SHIM" -o "$T/san" -lm $DEPF 2>"$T/san.log"; then
    echo "FAIL: sanitizer cc"; sed 's/^/      /' "$T/san.log"; fail=1
else
    ASAN_OPTIONS=detect_leaks=0 "$T/san" "$URL" > "$T/san.out" 2>"$T/san.err" || { echo "FAIL: sanitizer fault"; sed 's/^/      /' "$T/san.err"; fail=1; }
    if grep -qiE 'runtime error|Sanitizer|ERROR: ' "$T/san.err"; then echo "FAIL: sanitizer report"; sed 's/^/      /' "$T/san.err"; fail=1; fi
fi

if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$T/h0.out"; then
    echo "FAIL: tychoc vs tychoc0 output differ"; diff "$T/c.out" "$T/h0.out" | sed 's/^/      /'; fail=1
fi
if [ "$RECORD" = 1 ]; then cp "$T/c.out" "$golden"; echo "rec  fetch"; fi
if [ "$fail" -eq 0 ] && [ ! -f "$golden" ]; then echo "FAIL: no golden -- run RECORD=1"; fail=1; fi
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$golden"; then
    echo "FAIL: output != golden"; diff "$golden" "$T/c.out" | sed 's/^/      /'; fail=1
fi
[ "$fail" -eq 0 ] && echo "fetch: green (http+json+sha256+io+path compose; tychoc+tychoc0+ASan; real libcurl via file://)" || { echo "fetch: FAIL"; exit 1; }
