#!/bin/sh
# corelib test harness. For each corelib/test/<name>/main.hi: compile with the C compiler
# (hierc) AND via the self-hosted hierc0 (hierc --bundle | hierc0), and assert both produce
# the golden corelib/test/<name>.out. Sets HIER_CORELIB so `import "core:X"` resolves.
set -u
cd "$(dirname "$0")/.." || exit 2                      # repo root
HIERC=./hierc
[ -x "$HIERC" ] || { echo "no ./hierc -- run 'make' first"; exit 2; }
export HIER_CORELIB="$PWD/corelib"
CC="${CC:-cc}"
RECORD="${RECORD:-0}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
"$HIERC" compiler/hierc0.hi -o "$T/h0" >/dev/null 2>&1 || { echo "FAIL: could not build hierc0"; exit 1; }
fail=0
for entry in corelib/test/*/main.hi; do
    [ -e "$entry" ] || continue
    name="$(basename "$(dirname "$entry")")"
    golden="corelib/test/$name.out"
    if ! "$HIERC" "$entry" -o "$T/c" >/dev/null 2>&1; then echo "FAIL $name (hierc compile)"; fail=1; continue; fi
    "$T/c" > "$T/co" 2>&1
    if ! { "$HIERC" "$entry" --bundle 2>/dev/null | "$T/h0" > "$T/h0c.c" 2>/dev/null && $CC -O2 -o "$T/h0b" "$T/h0c.c" -lm 2>/dev/null; }; then echo "FAIL $name (hierc0 compile)"; fail=1; continue; fi
    "$T/h0b" > "$T/ho" 2>&1
    if ! cmp -s "$T/co" "$T/ho"; then echo "FAIL $name (hierc vs hierc0 differ)"; diff "$T/co" "$T/ho" | head | sed 's/^/      /'; fail=1; continue; fi
    if [ "$RECORD" = 1 ]; then cp "$T/co" "$golden"; echo "rec  $name"; continue; fi
    if [ ! -f "$golden" ]; then echo "FAIL $name (no golden -- run RECORD=1)"; fail=1; continue; fi
    if ! cmp -s "$T/co" "$golden"; then echo "FAIL $name (output != golden)"; diff "$golden" "$T/co" | head | sed 's/^/      /'; fail=1; continue; fi
    echo "ok   $name"
done
[ "$fail" -eq 0 ] && echo "corelib: all green (hierc and hierc0 agree, match goldens)" || { echo "corelib: FAIL"; exit 1; }
