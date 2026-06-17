#!/bin/sh
# Dogfood: the `site` static-site generator composes EIGHT corelib modules
# (io + path + json + csv + strings + sort + datetime + sha256) with no FFI and
# no external dependency. Built by all three compilers (the C hierc, hierc0 via
# --bundle, and standalone hierc0), each run against the local fixture site so the
# whole pipeline is deterministic; the build report (page list + per-page content
# hashes) is asserted byte-identical against the golden. The emitted C is also run
# under ASan/UBSan -- a heavy string-building / per-scope-arena workload, exactly
# what the thesis claims to handle without manual memory management.
# Re-record the golden with:  RECORD=1 sh examples/site/run.sh
set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
HIERC=./hierc
[ -x "$HIERC" ] || { echo "no ./hierc -- run 'make' first"; exit 2; }
export HIER_CORELIB="$PWD/corelib"
CC="${CC:-cc}"
RECORD="${RECORD:-0}"
golden=examples/site/expected.out
SRC=examples/site/main.hi
SITE=examples/site
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/out"
fail=0
"$HIERC" compiler/hierc0.hi -o "$T/h0" >/dev/null 2>&1 || { echo "FAIL: could not build hierc0"; exit 1; }

# (1) C reference compiler
if ! "$HIERC" "$SRC" -o "$T/c" >"$T/c.log" 2>&1; then
    echo "FAIL: hierc compile"; sed 's/^/      /' "$T/c.log"; fail=1
else
    "$T/c" "$SITE" "$T/out" > "$T/c.out" 2>&1
fi

# (2) self-hosted hierc0 (bundle)
if ! { "$HIERC" "$SRC" --bundle 2>/dev/null | "$T/h0" > "$T/h0.c" 2>/dev/null && \
       $CC -O2 "$T/h0.c" -o "$T/h0bin" -lm 2>"$T/h0.log"; }; then
    echo "FAIL: hierc0 compile"; sed 's/^/      /' "$T/h0.log"; fail=1
else
    "$T/h0bin" "$SITE" "$T/out" > "$T/h0.out" 2>&1
fi

# (3) standalone hierc0 (resolves core: itself)
if ! { "$T/h0" "$SRC" > "$T/sd.c" 2>/dev/null && $CC -O2 "$T/sd.c" -o "$T/sdbin" -lm 2>"$T/sd.log"; }; then
    echo "FAIL: standalone hierc0 compile"; sed 's/^/      /' "$T/sd.log"; fail=1
else
    "$T/sdbin" "$SITE" "$T/out" > "$T/sd.out" 2>&1
fi

# (4) ASan/UBSan over the emitted C
if ! $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 "$T/h0.c" -o "$T/san" -lm 2>"$T/san.log"; then
    echo "FAIL: sanitizer cc"; sed 's/^/      /' "$T/san.log"; fail=1
else
    ASAN_OPTIONS=detect_leaks=0 "$T/san" "$SITE" "$T/out" > "$T/san.out" 2>"$T/san.err" || { echo "FAIL: sanitizer fault"; sed 's/^/      /' "$T/san.err"; fail=1; }
    if grep -qiE 'runtime error|Sanitizer|ERROR: ' "$T/san.err"; then echo "FAIL: sanitizer report"; sed 's/^/      /' "$T/san.err"; fail=1; fi
fi

if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$T/h0.out"; then
    echo "FAIL: hierc vs hierc0 differ"; diff "$T/c.out" "$T/h0.out" | sed 's/^/      /'; fail=1
fi
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$T/sd.out"; then
    echo "FAIL: standalone hierc0 differs"; diff "$T/c.out" "$T/sd.out" | sed 's/^/      /'; fail=1
fi
if [ "$RECORD" = 1 ]; then cp "$T/c.out" "$golden"; echo "rec  site"; fi
if [ "$fail" -eq 0 ] && [ ! -f "$golden" ]; then echo "FAIL: no golden -- run RECORD=1"; fail=1; fi
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$golden"; then
    echo "FAIL: output != golden"; diff "$golden" "$T/c.out" | sed 's/^/      /'; fail=1
fi
[ "$fail" -eq 0 ] && echo "site: green (io+path+json+csv+strings+sort+datetime+sha256 compose; hierc+hierc0+standalone+ASan)" || { echo "site: FAIL"; exit 1; }
