#!/bin/sh
# entrypoints -- COMPILE every program in the tree that has an entry point, so a
# program cannot stop compiling without something going red.
#
# WHY THIS EXISTS.  `examples/webserver/main.ty` was left uncompilable by a whole
# phase (`error: ordering compares two ints` on `if srv < 0`, against a converted
# `net.listen`): it imports `core:net` but was not in that commit's file list, and
# NO gate built it -- `make ci` runs corelib, corelib-examples, site, raytrace and
# mandelbrot, and nothing else with an entry point.  The per-example runners that
# do build it (`examples/{webserver,weblog,fetch,sqlite}/run.sh`, `make server`)
# are all outside `make ci`, so the breakage surfaced only when a human ran one by
# hand.  FRICTION.md records it; this lane is the fix.
#
# WHAT IT ASSERTS, exactly: `tychoc <entry> --emit-c` exits 0 for every entry
# point below.  --emit-c stops after writing the C: no cc, no link, no libcurl,
# no libsqlite3, no libpng -- which is why this whole lane is milliseconds and can
# sit in `make ci` without being a reason to run `make ci` less often.
#
# WHAT IT DOES **NOT** ASSERT -- stated plainly so the coverage is not read wider
# than it is:
#   * that the emitted C compiles (that is `make test` / the per-example runners),
#   * that the program RUNS or matches a golden (per-example run.sh, corelib/run.sh),
#   * anything about the frozen `compiler/tychoc0.ty`.  Freeze parity for
#     `examples/<dir>/main.ty` is asserted ONLY by the four per-example runners,
#     none of which is in `make ci`; see docs/spec/appendix-e-conformance.md.
#
# Fail-closed (RULE 7 / the phase-1b lesson): the MUST list below is checked to
# exist before anything is compiled, so this gate cannot silently go vacuous by a
# rename or a glob that stops matching.  A gate that cannot ask its question must
# say so, not pass.
set -u
cd "$(dirname "$0")/.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "entrypoints: no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

# The entry points with no gate of their own. Losing one of these to a rename is
# exactly the rot this lane exists to prevent, so they are named, not globbed.
MUST="examples/webserver/main.ty examples/weblog/main.ty examples/fetch/main.ty
      examples/sqlite/demo.ty server/main.ty"
missing=""
for m in $MUST; do [ -f "$m" ] || missing="$missing $m"; done
[ -z "$missing" ] || { echo "entrypoints: MUST-COVER FILE GONE:$missing -- this lane asserts LESS than it claims; fix the list or restore the file"; exit 1; }

# Every entry point under examples/, plus server/. A directory's entry is main.ty
# when it has one, else each .ty in it (examples/{life,minesweeper,snake,sqlite}).
# examples/corelib/* is skipped: examples/corelib/run.sh compiles, RUNS and goldens
# all 38 of them and is in `make ci`, so re-compiling them here buys nothing.
# Globbed, so a NEW example directory is covered the day it is added.
list=""
for d in examples/*/; do
    case "$d" in examples/corelib/) continue ;; esac
    if [ -f "${d}main.ty" ]; then list="$list ${d}main.ty"
    else for f in "$d"*.ty; do [ -f "$f" ] && list="$list $f"; done
    fi
done
list="$list server/main.ty"

n=0; fail=0
for e in $list; do
    n=$((n + 1))
    if "$TYCHOC" "$e" --emit-c -o "$T/e" >"$T/log" 2>&1; then
        echo "ok      $e"
    else
        echo "FAIL    $e"
        sed 's/^/        /' "$T/log" | head -6
        fail=$((fail + 1))
    fi
    rm -f "$T/e.c"
done

# A zero-length sweep is a broken gate, not a green one.
[ "$n" -ge 6 ] || { echo "entrypoints: only $n entry point(s) found -- the glob is broken, this lane asserts NOTHING"; exit 1; }
echo "-----------------------------------------"
[ "$fail" -eq 0 ] || { echo "entrypoints: FAILED ($fail of $n entry points do not compile)"; exit 1; }
echo "entrypoints: ok ($n entry points compile with tychoc)"
