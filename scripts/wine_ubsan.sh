#!/bin/sh
# Undefined behaviour in the EMITTED code and the runtime, on Windows.
#
# docs/internals/ffi-review-2026-08-14.md named this gap in one line: "the wine
# lanes cover behaviour, not memory safety: there is no ASan equivalent in that
# path." That is true of ASan -- mingw-w64 ships no libasan and no libubsan here,
# so -fsanitize=address and plain -fsanitize=undefined both fail at LINK.
#
# But UBSan does not need its runtime if the checks TRAP instead of reporting:
# -fsanitize=undefined -fsanitize-undefined-trap-on-error compiles every check
# down to an illegal instruction, links against nothing, and dies under wine.
# Measured here, not assumed.
#
# It reuses scripts/wine_test.sh whole -- goldens, package programs, runtime
# aborts, compiler diagnostics -- through WINE_CCF, rather than growing a second
# copy of that harness. So a trap shows up as the failure that lane already
# reports, against the golden it already checks.
#
# WHAT THIS COVERS AND WHAT IT DOES NOT. The subject is the emitted C plus the
# corelib shims plus the runtime, which is the part that ships to a user's
# machine. tychoc.exe itself is not rebuilt sanitized (it is covered on Linux by
# scripts/asan_self.sh). -fwrapv is kept, matching the shipped Windows cc line,
# so signed overflow is DEFINED in this build and is deliberately not among the
# checks -- what remains is out-of-bounds on known-size objects, misaligned and
# null pointer use, bad shifts, bad enum and bool values, and VLA bounds. This is
# not ASan: it cannot see a use-after-free or a heap overflow past an unknown
# bound.
set -eu

cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

MINGWCC="$(command -v x86_64-w64-mingw32-gcc || true)"
WINE="$(command -v wine || command -v wine64 || true)"
[ -n "$MINGWCC" ] || { echo "wine-ubsan: SKIPPED (no x86_64-w64-mingw32-gcc)"; exit 0; }
[ -n "$WINE" ]    || { echo "wine-ubsan: SKIPPED (no wine)"; exit 0; }

TRAP="-fsanitize=undefined -fsanitize-undefined-trap-on-error"
CCF="-O1 -fwrapv -pthread $TRAP"
W="env -u LD_PRELOAD WINEDEBUG=-all $WINE"

# --- the control ------------------------------------------------------------
# A green corpus proves nothing unless a trap would actually have fired, in THIS
# build configuration -- same optimisation level, same -fwrapv, same wine. So the
# identical source is built twice and must disagree: without the flags it exits
# 0, with them it must not. If they agree, the flags are not reaching the
# compiler or wine is not delivering the trap, and the sweep below is decoration.
cat > "$T/ctl.c" <<'EOF'
#include <stdio.h>
int main(void) {
    int a[4] = {0};
    volatile int i = 5;             /* out of bounds on a KNOWN-size object */
    printf("%d\n", a[i]);
    return 0;
}
EOF
"$MINGWCC" -O1 -fwrapv -o "$T/plain.exe" "$T/ctl.c" 2>/dev/null || {
    echo "wine-ubsan: SKIPPED (the control does not build)"; exit 0; }
# shellcheck disable=SC2086
"$MINGWCC" $CCF -o "$T/trap.exe" "$T/ctl.c" 2>"$T/cc.log" || {
    echo "wine-ubsan: SKIPPED (this mingw cannot trap on UB)"; head -2 "$T/cc.log"; exit 0; }

rc=0; $W "$T/plain.exe" >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 0 ] || { echo "wine-ubsan: SKIPPED (wine cannot run a plain exe here, rc=$rc)"; exit 0; }
rc=0; $W "$T/trap.exe" >/dev/null 2>&1 || rc=$?
if [ "$rc" -eq 0 ]; then
    echo "wine-ubsan: CONTROL DEAD -- deliberate out-of-bounds access exited 0 under"
    echo "            wine with the trap flags on. Nothing below would be detected."
    exit 1
fi
echo "  control: the same source exits 0 plain and $rc with the trap flags"

# --- the corpus, through the existing lane -----------------------------------
WINE_CCF="$CCF" sh scripts/wine_test.sh > "$T/log" 2>&1 || {
    echo "wine-ubsan: FAIL -- the corpus reddened with UB checks on."
    echo "            Re-run WITHOUT them to separate a trap from a port failure:"
    echo "            sh scripts/wine_test.sh"
    grep -E "^ +|failed|FAIL" "$T/log" | head -20
    exit 1; }
tail -2 "$T/log" | sed 's/^/  /'

# --- the corelib packages, where the hand-written C actually lives ------------
# The fixture corpus above exercises the emitted C and the runtime. The SHIMS are
# hand-written C over external libraries and are the part most likely to hold a
# real out-of-bounds, so they get the same treatment through the same seam.
if WINE_CCF="$CCF" sh scripts/wine_corelib.sh > "$T/clog" 2>&1; then
    tail -1 "$T/clog" | sed 's/^/  /'
else
    echo "wine-ubsan: FAIL -- the corelib packages reddened with UB checks on."
    echo "            Re-run WITHOUT them to separate a trap from a port failure:"
    echo "            sh scripts/wine_corelib.sh"
    grep -E "FAIL|failed|\(cc\)|\(run\)" "$T/clog" | head -20
    exit 1
fi
echo "wine-ubsan: green (the emitted C, the corelib shims and the runtime built to TRAP on undefined behaviour, over the whole wine corpus AND every corelib package that ports, with a control proving a deliberate out-of-bounds access does die under wine in this exact configuration)"
