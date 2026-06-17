#!/bin/sh
# corelib test harness. For each corelib/test/<name>/main.hi: compile with the C compiler
# (hierc), via the self-hosted hierc0 fed `hierc --bundle`, AND via STANDALONE hierc0
# (which resolves `import "core:X"` itself through the getenv builtin + HIER_CORELIB),
# and assert all three produce the golden corelib/test/<name>.out. Sets HIER_CORELIB.
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
    # FFI: hierc auto-discovers a module's <mod>_shim.c; the hierc0 paths emit C
    # that WE link, so pass the shim here too (convention: test <name> imports core:<name>).
    shim="corelib/$name/${name}_shim.c"; [ -f "$shim" ] || shim=""
    # FFI deps: a `corelib/$name/deps` lists pkg-config names. If any is absent,
    # SKIP this module (keeps `make ci` green on platforms without the lib);
    # otherwise pass its --cflags --libs on the hierc0 link paths (hierc reads
    # the same `deps` itself, so the two stay in lockstep across platforms).
    deps="corelib/$name/deps"; depflags=""
    if [ -f "$deps" ]; then
        pkgs="$(grep -vE '^[[:space:]]*(#|$)' "$deps")"
        missing=""
        for pkg in $pkgs; do pkg-config --exists "$pkg" 2>/dev/null || missing="$missing $pkg"; done
        if [ -n "$missing" ]; then echo "skip $name (missing dependency:$missing)"; continue; fi
        depflags="$(pkg-config --cflags --libs $pkgs 2>/dev/null)"
    fi
    if ! "$HIERC" "$entry" -o "$T/c" >/dev/null 2>&1; then echo "FAIL $name (hierc compile)"; fail=1; continue; fi
    "$T/c" > "$T/co" 2>&1
    if ! { "$HIERC" "$entry" --bundle 2>/dev/null | "$T/h0" > "$T/h0c.c" 2>/dev/null && $CC -O2 -o "$T/h0b" "$T/h0c.c" $shim -lm $depflags 2>/dev/null; }; then echo "FAIL $name (hierc0 compile)"; fail=1; continue; fi
    "$T/h0b" > "$T/ho" 2>&1
    if ! cmp -s "$T/co" "$T/ho"; then echo "FAIL $name (hierc vs hierc0 differ)"; diff "$T/co" "$T/ho" | head | sed 's/^/      /'; fail=1; continue; fi
    # standalone hierc0: resolves `core:` itself via getenv(HIER_CORELIB) -- no `hierc --bundle`
    if ! { "$T/h0" "$entry" > "$T/sdc.c" 2>/dev/null && $CC -O2 -o "$T/sdb" "$T/sdc.c" $shim -lm $depflags 2>/dev/null; }; then echo "FAIL $name (standalone hierc0 compile)"; fail=1; continue; fi
    "$T/sdb" > "$T/sdo" 2>&1
    if ! cmp -s "$T/co" "$T/sdo"; then echo "FAIL $name (standalone hierc0 differs)"; diff "$T/co" "$T/sdo" | head | sed 's/^/      /'; fail=1; continue; fi
    if [ "$RECORD" = 1 ]; then cp "$T/co" "$golden"; echo "rec  $name"; continue; fi
    if [ ! -f "$golden" ]; then echo "FAIL $name (no golden -- run RECORD=1)"; fail=1; continue; fi
    if ! cmp -s "$T/co" "$golden"; then echo "FAIL $name (output != golden)"; diff "$golden" "$T/co" | head | sed 's/^/      /'; fail=1; continue; fi
    echo "ok   $name"
done
[ "$fail" -eq 0 ] && echo "corelib: all green (hierc and hierc0 agree, match goldens)" || { echo "corelib: FAIL"; exit 1; }
