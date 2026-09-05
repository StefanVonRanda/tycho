# Reads the C-compiler WARNINGS out of the mingw64 CROSS build and compares them
# to a locked baseline, the way scripts/shim_warn.sh does for the corelib shims.
#
# Why it exists: `scripts/release.sh <ver> --mingw` cross-builds four .exe files
# that SHIP, and nothing in this tree ever read that compiler's warnings. They
# scrolled past in the release log. `make shim-warn` cannot see them -- its
# subject is hand-written shim C, compiled by the HOST cc -- and no other lane
# runs the cross compiler at all.
#
# Why the release cc line and not -Wall -Wextra: the subject here is EMITTED C,
# where -Wall is noise by construction (a code generator names variables it does
# not always read). MEASURED 2026-09-05: the release line's DEFAULT warning set
# -- which is where -Wstringop-overread, -Wstringop-overflow and the rest of the
# access diagnostics live -- emits ZERO lines over all four programs, so the
# baseline is empty and any warning at all is a failure. A wider set would need
# an exemption list, and an exemption list is how a warning lane becomes
# decoration.
#
# Why a baseline and not -Werror: the cross gcc is the host's, so a new one can
# invent a warning unrelated to the change being gated. A diff names the line and
# prints the compiler version, which separates "new defect" from "new gcc".
#
# Skips loudly with exit 0 when the cross compiler is absent.

set -eu

MINGWCC="${MINGWCC:-x86_64-w64-mingw32-gcc}"
WARNBASE="${WARNBASE:-scripts/mingw.warn}"
TYCHOC="${TYCHOC:-./tychoc}"
# The release cc line, verbatim (scripts/release.sh, the tools loop).
CCFLAGS="-O2 -fwrapv -static -pthread"

# 4 programs + their shims. A floor, not the count: it is what stops an empty
# warning file passing because nothing was compiled at all.
MIN_COMPILED=4

if ! command -v "$MINGWCC" > /dev/null 2>&1; then
    echo "mingw-warn: SKIPPED ($MINGWCC not on PATH; install mingw-w64 to run this lane)"
    exit 0
fi

T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT

selfcheck() {
    rc=0
    # A read past a literal, of the family this lane exists for. The buffer is
    # PRINTED, not returned: with `return b[0]` gcc drops the memcpy as dead and
    # the probe emits nothing, which reads as a broken grep. A control is code
    # the optimiser is entitled to delete.
    cat > "$T/probe.c" <<'EOF'
#include <string.h>
#include <stdio.h>
static const char lit[3] = "  ";
int main(void) { char b[8]; memcpy(b, lit, 17); printf("%s\n", b); return 0; }
EOF
    n=$("$MINGWCC" $CCFLAGS -c -o "$T/probe.o" "$T/probe.c" 2>&1 | grep -c ': warning: ' || true)
    if [ "$n" -ge 1 ]; then
        echo "selfcheck ok   a read past a 3-byte literal is seen ($n warning line(s))"
    else
        echo "selfcheck FAIL the warning grep matched $n lines on a file that overreads a literal"
        rc=1
    fi

    cat > "$T/clean.c" <<'EOF'
int main(void) { return 0; }
EOF
    n=$("$MINGWCC" $CCFLAGS -c -o "$T/clean.o" "$T/clean.c" 2>&1 | grep -c ': warning: ' || true)
    if [ "$n" -eq 0 ]; then
        echo "selfcheck ok   a clean file emits 0 warning lines"
    else
        echo "selfcheck FAIL a clean file emitted $n warning lines"
        rc=1
    fi
    return $rc
}

if [ "${1:-}" = "--selfcheck" ]; then
    selfcheck
    exit $?
fi

# The grep is only trustworthy if it has been shown to match; do that every run.
selfcheck > "$T/self" 2>&1 || { cat "$T/self"; echo "mingw-warn: the instrument is broken, not the tree." >&2; exit 1; }

[ -x "$TYCHOC" ] || { echo "mingw-warn: FAILED (no $TYCHOC; run make tychoc first)" >&2; exit 1; }

: > "$T/warn"
compiled=0

# name entry shim  -- the four programs scripts/release.sh --mingw stages.
for spec in "tychoc compiler/main.ty -" \
            "tychofmt tools/tychofmt.ty -" \
            "tycho-lsp tools/lsp.ty tools/lsp_shim.c" \
            "tycho-debug tools/tycho-debug/main.ty tools/tycho-debug/debug_shim.c"; do
    # shellcheck disable=SC2086
    set -- $spec; tname="$1"; tentry="$2"; tshim="$3"
    shimarg=""
    [ "$tshim" != "-" ] && shimarg="--shim $tshim"

    # shellcheck disable=SC2086
    "$TYCHOC" "$tentry" $shimarg --emit-c -o "$T/$tname" > /dev/null 2>"$T/emit.log" \
        || { echo "mingw-warn: FAILED ($tname did not emit C)"; sed -n '1,10p' "$T/emit.log"; exit 1; }

    # shellcheck disable=SC2086
    shims="$("$TYCHOC" "$tentry" $shimarg --print-shims 2>/dev/null | tr '\n' ' ')"
    [ "$tshim" != "-" ] && shims="$shims $tshim"

    for src in "$T/$tname.c" $shims; do
        # shellcheck disable=SC2086
        "$MINGWCC" $CCFLAGS -c -o "$T/obj.o" "$src" > "$T/log" 2>&1 || {
            echo "mingw-warn: FAILED ($src did not cross-compile)"
            sed -n '1,10p' "$T/log"
            exit 1
        }
        # Strip the temp directory so the baseline is stable across runs.
        grep -E ': warning: ' "$T/log" | sed "s#$T/#emitted/#g" >> "$T/warn" || true
    done
    compiled=$((compiled + 1))
done

sort "$T/warn" > "$T/warn.s"

if [ "$compiled" -lt "$MIN_COMPILED" ]; then
    echo "mingw-warn: FAILED (only $compiled program(s) cross-compiled, expected $MIN_COMPILED)"
    echo "  An empty warning file means nothing when nothing was compiled." >&2
    exit 1
fi

if [ "${RECORD:-0}" = "1" ]; then
    cp "$T/warn.s" "$WARNBASE"
    echo "rec     $WARNBASE ($(wc -l < "$WARNBASE" | tr -d ' ') warning line(s))"
    exit 0
fi

if [ ! -f "$WARNBASE" ]; then
    echo "mingw-warn: FAILED (no baseline at $WARNBASE; RECORD=1 to write one)"
    exit 1
fi

if ! cmp -s "$T/warn.s" "$WARNBASE"; then
    echo "mingw-warn: FAILED (the warnings the mingw cross-build emits moved)"
    echo "  cc: $("$MINGWCC" --version 2>/dev/null | head -1)"
    echo "  A new cross compiler and a new codegen defect both land here. Read the diff:"
    diff -u "$WARNBASE" "$T/warn.s" | sed -n '3,25p'
    exit 1
fi

echo "mingw-warn: ok ($compiled program(s) cross-compiled with $CCFLAGS; $(wc -l < "$WARNBASE" | tr -d ' ') warning line(s), matching $WARNBASE)"
