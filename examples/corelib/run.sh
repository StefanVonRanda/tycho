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
ran=0
nskip=0
skipped=""
for entry in examples/corelib/*/main.ty; do
    [ -e "$entry" ] || continue
    name="$(basename "$(dirname "$entry")")"
    golden="examples/corelib/$name.out"
    # If any pkg-config dependency is absent, SKIP (keeps the deps-gated modules
    # green on platforms without them). ASK THE COMPILER which ones there are:
    # `--print-deps` prints the pkg-config names of the whole transitive import
    # closure, which is the set that matters -- an httpd example wraps core:net,
    # and a module's dependency can be reached through an intermediate. tychoc
    # links the shims and the resolved flags itself, so nothing else is needed
    # here. Same derivation as the corelib test harness; see corelib/run.sh's
    # comment for what this replaced and what was measured about it.
    if ! allpkgs="$("$TYCHOC" "$entry" --print-deps 2>"$T/deps.log")"; then
        echo "FAIL $name (tychoc --print-deps)"; sed 's/^/      /' "$T/deps.log"; fail=1; continue
    fi
    if [ -n "$allpkgs" ]; then
        missing=""
        for pkg in $allpkgs; do pkg-config --exists "$pkg" 2>/dev/null || missing="$missing $pkg"; done
        if [ -n "$missing" ]; then
            echo "skip $name (missing dependency:$missing)"
            nskip=$((nskip + 1)); skipped="$skipped $name(missing:$missing)"; continue
        fi
    fi
    if ! "$TYCHOC" "$entry" -o "$T/c" >/dev/null 2>&1; then echo "FAIL $name (tychoc compile)"; fail=1; continue; fi
    "$T/c" > "$T/co" 2>&1
    if [ "$RECORD" = 1 ]; then cp "$T/co" "$golden"; echo "rec  $name"; ran=$((ran + 1)); continue; fi
    if [ ! -f "$golden" ]; then echo "FAIL $name (no golden -- run RECORD=1)"; fail=1; continue; fi
    if ! cmp -s "$T/co" "$golden"; then echo "FAIL $name (output != golden)"; diff "$golden" "$T/co" | head | sed 's/^/      /'; fail=1; continue; fi
    echo "ok   $name"
    ran=$((ran + 1))
done
[ "$fail" -eq 0 ] || { echo "corelib examples: FAIL"; exit 1; }
# A skipped example was NOT run, so this is not "all green" -- name it and why,
# or the log is indistinguishable from a host that ran every example.
if [ "$nskip" -ne 0 ]; then
    echo "corelib examples: $ran ok, $nskip SKIPPED --$skipped -- NOT all green (those examples were not run)"
else
    echo "corelib examples: all green ($ran ok, tychoc matches goldens)"
fi
