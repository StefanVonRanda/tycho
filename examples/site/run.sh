#!/bin/sh
# Dogfood: the `site` static-site generator composes EIGHT corelib modules
# (io + path + json + csv + strings + sort + datetime + sha256). The only FFI is
# core:datetime's tz-offset shim (dtx_*, pure libc, no external deps); the ASan
# path below links the emitted C itself, so we gather that shim (mirroring
# corelib/run.sh) -- tychoc auto-discovers it for its own cc line. Built by tychoc
# and run against the local fixture site so the whole pipeline is deterministic;
# the build report (page list + per-page content hashes) is asserted byte-identical
# against the golden. (Until 2026-07-26 the site was also built by tychoc0 via
# --bundle and by standalone tychoc0, all three outputs required to match; tychoc0
# is frozen -- see compiler/tychoc0.ty -- and no gate builds it.) The emitted C is also run
# under ASan/UBSan -- a heavy string-building / per-scope-arena workload, exactly
# what the thesis claims to handle without manual memory management.
# Re-record the golden with:  RECORD=1 sh examples/site/run.sh
set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
CC="${CC:-cc}"
RECORD="${RECORD:-0}"
golden=examples/site/expected.out
SRC=examples/site/main.ty
SITE=examples/site
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/out"
fail=0

# The C shims the ASan leg must link. ASK THE COMPILER: `--print-shims` prints the
# transitive <pkg>_shim.c closure, which is exactly what tychoc splices onto its
# own cc line in leg (1).
#
# This used to grep $SRC for `core:` imports and map each to corelib/<m>/<m>_shim.c.
# That found DIRECT imports only, and the difference is not theoretical: it is the
# 2026-08-01 break in examples/fetch/run.sh, where the missing shim (core:strings)
# was reached through core:json and no grep of the program's own source could see
# it. This lane happened to survive because it imports core:strings directly --
# survival by coincidence, not by the mechanism working.
shim="$("$TYCHOC" "$SRC" --print-shims)" \
    || { echo "site: FAIL (tychoc --print-shims)"; exit 1; }

# (1) C reference compiler
if ! "$TYCHOC" "$SRC" -o "$T/c" >"$T/c.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/c.log"; fail=1
else
    "$T/c" "$SITE" "$T/out" > "$T/c.out" 2>&1
fi

# (2) ASan/UBSan over the emitted C
if ! "$TYCHOC" "$SRC" --emit-c -o "$T/e" >"$T/e.log" 2>&1; then
    echo "FAIL: tychoc --emit-c"; sed 's/^/      /' "$T/e.log"; fail=1
fi
if ! $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 "$T/e.c" $shim -o "$T/san" -lm 2>"$T/san.log"; then
    echo "FAIL: sanitizer cc"; sed 's/^/      /' "$T/san.log"; fail=1
else
    ASAN_OPTIONS=detect_leaks=0 "$T/san" "$SITE" "$T/out" > "$T/san.out" 2>"$T/san.err" || { echo "FAIL: sanitizer fault"; sed 's/^/      /' "$T/san.err"; fail=1; }
    if grep -qiE 'runtime error|Sanitizer|ERROR: ' "$T/san.err"; then echo "FAIL: sanitizer report"; sed 's/^/      /' "$T/san.err"; fail=1; fi
fi

if [ "$RECORD" = 1 ]; then cp "$T/c.out" "$golden"; echo "rec  site"; fi
if [ "$fail" -eq 0 ] && [ ! -f "$golden" ]; then echo "FAIL: no golden -- run RECORD=1"; fail=1; fi
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$golden"; then
    echo "FAIL: output != golden"; diff "$golden" "$T/c.out" | sed 's/^/      /'; fail=1
fi
[ "$fail" -eq 0 ] && echo "site: green (io+path+json+csv+strings+sort+datetime+sha256 compose; tychoc+ASan, matches golden)" || { echo "site: FAIL"; exit 1; }
