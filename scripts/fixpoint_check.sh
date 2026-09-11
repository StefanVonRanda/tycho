#!/bin/sh
# The self-hosted compiler reaches a FIXPOINT. `make fixpoint-check`.
#
# WHY THIS EXISTS. tychoc1 is built in two stages -- ./tychoc emits stage 1,
# stage 1 emits the shipped tychoc1 -- so the binary you run was produced by a
# compiler written in Tycho. The property that makes that trustworthy is that
# one more generation changes nothing: a compiler that compiles ITSELF to the
# same output has no generation-dependent behaviour left in it.
#
# That property was measured twice, by hand, a month apart (2026-08-30 and
# 2026-09-11) and asserted by NOTHING. It is the worst shape for an unpinned
# invariant: if the bootstrap stopped converging, every other lane here would
# stay green and the only symptom would be a difference nobody looked for.
#
# WHAT IT COMPARES, and why it is cheap. Not binaries -- the emitted C, which is
# what the compiler actually produces and is free of link-time and toolchain
# noise. gen2.c comes from the shipped tychoc1; gen2.c is built once (with plain
# flags: it only has to RUN, not to be byte-comparable) and asked to emit the
# compiler again. gen2.c == gen3.c is f(f(x)) == f(x). One cc invocation, ~30s.
set -u
cd "$(dirname "$0")/.." || exit 2
TYCHOC1="${TYCHOC1:-./tychoc1}"
CC="${CC:-cc}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT INT TERM
SRC=compiler/main.ty

# The corelib is resolved relative to the RUNNING BINARY, so gen2 -- which lives
# in a temp dir -- cannot find core:strings on its own. architecture.md names
# this trap for exactly this situation; TYCHO_CORELIB is the documented override.
TYCHO_CORELIB="$PWD/corelib"; export TYCHO_CORELIB

emit_from() {   # emit_from <compiler> <out-base>  -- run from the repo root so paths in the C match
    "$1" "$SRC" --emit-c -o "$2" >"$T/emit.log" 2>&1
}

build_gen2() {
    emit_from "$TYCHOC1" "$T/gen2" || { echo "fixpoint: $TYCHOC1 could not emit C"; sed 's/^/    /' "$T/emit.log"; return 1; }
    shims="$("$TYCHOC1" "$SRC" --print-shims 2>/dev/null)"
    # unquoted on purpose: --print-shims prints one path per line and they are
    # separate arguments. No path in this tree contains a space.
    # shellcheck disable=SC2086
    $CC -O0 -fwrapv -std=c11 -o "$T/gen2bin" "$T/gen2.c" $shims -lm -lpthread 2>"$T/cc.log" \
        || { echo "fixpoint: gen2 did not build"; sed 's/^/    /' "$T/cc.log" | head -5; return 1; }
    return 0
}

if [ "${1:-}" = "--selfcheck" ]; then
    sc=0
    # [1] the comparison must actually run cmp: two KNOWN-different emissions
    #     must be reported as a difference. ./tychoc and ./tychoc1 are two
    #     implementations; their emitted C for the same input is not identical.
    if [ -x ./tychoc ] && [ -x "$TYCHOC1" ]; then
        ./tychoc "$SRC" --emit-c -o "$T/a" >/dev/null 2>&1
        "$TYCHOC1" "$SRC" --emit-c -o "$T/b" >/dev/null 2>&1
        if [ -f "$T/a.c" ] && [ -f "$T/b.c" ] && ! cmp -s "$T/a.c" "$T/b.c"; then
            echo "  [1] two different compilers emit different C    ok (cmp can report a difference)"
        else
            echo "  [1] two different compilers emit different C    FAIL -- cmp reports no difference, so it cannot detect one"; sc=1
        fi
    else
        echo "  [1] needs ./tychoc and $TYCHOC1                     FAIL"; sc=1
    fi
    # [2] a missing generation must fail rather than pass vacuously.
    if cmp -s "$T/nope-a.c" "$T/nope-b.c" 2>/dev/null; then
        echo "  [2] absent files do not compare equal              FAIL"; sc=1
    else
        echo "  [2] absent files do not compare equal              ok"
    fi
    [ "$sc" -eq 0 ] && echo "fixpoint selfcheck: ok" || echo "fixpoint selfcheck: FAILED"
    exit "$sc"
fi

[ -x "$TYCHOC1" ] || { echo "fixpoint: no $TYCHOC1 -- run 'make tychoc1' first"; exit 2; }
build_gen2 || exit 1
emit_from "$T/gen2bin" "$T/gen3" || { echo "fixpoint: gen2 could not emit C"; sed 's/^/    /' "$T/emit.log"; exit 1; }

if cmp -s "$T/gen2.c" "$T/gen3.c"; then
    echo "fixpoint: ok -- gen2.c == gen3.c ($(wc -c < "$T/gen2.c") bytes); one more generation changes nothing"
else
    echo "fixpoint: FAILED -- the self-hosted compiler does not reproduce its own output"
    cmp "$T/gen2.c" "$T/gen3.c" | head -3 | sed 's/^/    /'
    echo "    a compiler whose output depends on which generation compiled it has"
    echo "    generation-dependent behaviour; diff the two to find it."
    exit 1
fi
