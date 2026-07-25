#!/bin/sh
# corelib test harness. For each corelib/test/<name>/main.ty: compile with tychoc and
# assert the program's output matches the golden corelib/test/<name>.out. Sets
# TYCHO_CORELIB.
#
# Until 2026-07-26 each test was ALSO built two more ways -- the self-hosted tychoc0 fed
# `tychoc --bundle`, and standalone tychoc0 resolving `import "core:X"` itself -- with all
# three outputs required to match. tychoc0 is frozen (see compiler/tychoc0.ty) and no gate
# builds it, so those two legs are gone. The golden comparison, the dependency skip, and
# every assertion about tychoc are unchanged; what was lost is the second implementation
# as a cross-check on corelib behaviour.
set -u
cd "$(dirname "$0")/.." || exit 2                      # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
CC="${CC:-cc}"
RECORD="${RECORD:-0}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
for entry in corelib/test/*/main.ty; do
    [ -e "$entry" ] || continue
    name="$(basename "$(dirname "$entry")")"
    golden="corelib/test/$name.out"
    # FFI: tychoc auto-discovers each imported module's <mod>_shim.c and reads its
    # `deps`. We still gather the pkg-config deps of EVERY core:X the test imports
    # (e.g. core:httpd wraps core:net) so that a test whose dependency is absent is
    # SKIPPED rather than failed -- that keeps `make ci` green on platforms without
    # the lib, and tychoc reads the same `deps` itself, so the two stay in lockstep.
    shim=""; allpkgs=""
    for mod in $(grep -E '^[[:space:]]*import' "$entry" | grep -oE 'core:[a-z0-9_]+' | sed 's/core://' | sort -u); do
        s="corelib/$mod/${mod}_shim.c"; [ -f "$s" ] && shim="$shim $s"
        d="corelib/$mod/deps"; [ -f "$d" ] && allpkgs="$allpkgs $(grep -vE '^[[:space:]]*(#|$)' "$d")"
    done
    depflags=""
    if [ -n "$allpkgs" ]; then
        missing=""
        for pkg in $allpkgs; do pkg-config --exists "$pkg" 2>/dev/null || missing="$missing $pkg"; done
        if [ -n "$missing" ]; then echo "skip $name (missing dependency:$missing)"; continue; fi
        depflags="$(pkg-config --cflags --libs $allpkgs 2>/dev/null)"
    fi
    if ! "$TYCHOC" "$entry" -o "$T/c" >/dev/null 2>&1; then echo "FAIL $name (tychoc compile)"; fail=1; continue; fi
    "$T/c" > "$T/co" 2>&1
    if [ "$RECORD" = 1 ]; then cp "$T/co" "$golden"; echo "rec  $name"; continue; fi
    if [ ! -f "$golden" ]; then echo "FAIL $name (no golden -- run RECORD=1)"; fail=1; continue; fi
    if ! cmp -s "$T/co" "$golden"; then echo "FAIL $name (output != golden)"; diff "$golden" "$T/co" | head | sed 's/^/      /'; fail=1; continue; fi
    echo "ok   $name"
done
[ "$fail" -eq 0 ] && echo "corelib: all green (tychoc matches goldens)" || { echo "corelib: FAIL"; exit 1; }
