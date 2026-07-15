#!/bin/sh
# corelib test harness. For each corelib/test/<name>/main.ty: compile with the C compiler
# (tychoc), via the self-hosted tychoc0 fed `tychoc --bundle`, AND via STANDALONE tychoc0
# (which resolves `import "core:X"` itself through the getenv builtin + TYCHO_CORELIB),
# and assert all three produce the golden corelib/test/<name>.out. Sets TYCHO_CORELIB.
set -u
cd "$(dirname "$0")/.." || exit 2                      # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
CC="${CC:-cc}"
RECORD="${RECORD:-0}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
"$TYCHOC" compiler/tychoc0.ty -o "$T/h0" >/dev/null 2>&1 || { echo "FAIL: could not build tychoc0"; exit 1; }
fail=0
for entry in corelib/test/*/main.ty; do
    [ -e "$entry" ] || continue
    name="$(basename "$(dirname "$entry")")"
    golden="corelib/test/$name.out"
    # FFI: tychoc auto-discovers each imported module's <mod>_shim.c and reads its
    # `deps`; the tychoc0 paths emit C that WE link, so gather the shim AND the
    # pkg-config deps of EVERY core:X the test imports -- not just the same-name one
    # (e.g. core:httpd wraps core:net, so net_shim.c is needed even though the test
    # is named "httpd"). If any dep is absent, SKIP (keeps `make ci` green on
    # platforms without the lib); tychoc reads the same `deps` itself, so the two
    # stay in lockstep across platforms.
    shim=""; allpkgs=""
    for mod in $(grep -oE 'core:[a-z0-9_]+' "$entry" | sed 's/core://' | sort -u); do
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
    if ! { "$TYCHOC" "$entry" --bundle 2>/dev/null | "$T/h0" > "$T/h0c.c" 2>/dev/null && $CC -O2 -o "$T/h0b" "$T/h0c.c" $shim -lm $depflags 2>/dev/null; }; then echo "FAIL $name (tychoc0 compile)"; fail=1; continue; fi
    "$T/h0b" > "$T/ho" 2>&1
    if ! cmp -s "$T/co" "$T/ho"; then echo "FAIL $name (tychoc vs tychoc0 differ)"; diff "$T/co" "$T/ho" | head | sed 's/^/      /'; fail=1; continue; fi
    # standalone tychoc0: resolves `core:` itself via getenv(TYCHO_CORELIB) -- no `tychoc --bundle`
    if ! { "$T/h0" "$entry" > "$T/sdc.c" 2>/dev/null && $CC -O2 -o "$T/sdb" "$T/sdc.c" $shim -lm $depflags 2>/dev/null; }; then echo "FAIL $name (standalone tychoc0 compile)"; fail=1; continue; fi
    "$T/sdb" > "$T/sdo" 2>&1
    if ! cmp -s "$T/co" "$T/sdo"; then echo "FAIL $name (standalone tychoc0 differs)"; diff "$T/co" "$T/sdo" | head | sed 's/^/      /'; fail=1; continue; fi
    if [ "$RECORD" = 1 ]; then cp "$T/co" "$golden"; echo "rec  $name"; continue; fi
    if [ ! -f "$golden" ]; then echo "FAIL $name (no golden -- run RECORD=1)"; fail=1; continue; fi
    if ! cmp -s "$T/co" "$golden"; then echo "FAIL $name (output != golden)"; diff "$golden" "$T/co" | head | sed 's/^/      /'; fail=1; continue; fi
    echo "ok   $name"
done
[ "$fail" -eq 0 ] && echo "corelib: all green (tychoc and tychoc0 agree, match goldens)" || { echo "corelib: FAIL"; exit 1; }
