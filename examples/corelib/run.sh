#!/bin/sh
# corelib EXAMPLES harness. Each examples/corelib/<name>.ty is a small, readable
# program that demonstrates `core:<name>` (usage as documentation, not assertions
# like corelib/test/). Validated exactly like the corelib tests: compiled by
# tychoc, whose output must match the golden examples/corelib/<name>.out. (Until
# 2026-07-26 two more legs ran -- `tychoc --bundle | tychoc0` and standalone
# tychoc0 -- with all three outputs required to match; tychoc0 is frozen, see
# compiler/tychoc0.ty, and no gate builds it.) A module with an
# external dependency (corelib/<name>/deps) is SKIPPED where the lib is absent.
# Re-record goldens with `RECORD=1 sh examples/corelib/run.sh`.
set -u
cd "$(dirname "$0")/../.." || exit 2                   # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
CC="${CC:-cc}"
RECORD="${RECORD:-0}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
for entry in examples/corelib/*/main.ty; do
    [ -e "$entry" ] || continue
    name="$(basename "$(dirname "$entry")")"
    golden="examples/corelib/$name.out"
    # Gather the C shim AND pkg-config deps of EVERY core:X the example imports --
    # not just the same-name one (e.g. an httpd example wraps core:net, so
    # net_shim.c is needed). Same lookup the corelib test harness uses. If any dep
    # is absent, SKIP (keeps the deps-gated modules green on platforms without them).
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
[ "$fail" -eq 0 ] && echo "corelib examples: all green" || { echo "corelib examples: FAIL"; exit 1; }
