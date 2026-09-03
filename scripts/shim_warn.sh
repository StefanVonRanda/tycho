# Reads WARNINGS out of the corelib C shims and compares them to a locked
# baseline, the way `scripts/entrypoints.warn` does for Tycho-level warnings.
#
# Why a baseline and not -Werror: the cc here is the HOST's, so a new gcc can
# invent a warning that has nothing to do with the change being gated. A diff
# against a baseline names the exact line and prints the compiler version, which
# separates "new defect" from "new gcc"; -Werror cannot tell them apart.
#
# Why -Wall -Wextra and no further: MEASURED 2026-09-03, that set emits ZERO
# lines over all 14 shims, so the baseline is empty and any warning at all is a
# failure. A wider set would need an exemption list, and an exemption list is
# how a warning lane becomes decoration.
#
# Deps parsing lives in scripts/deps_pkgs.sh and is shared with shim_check.sh,
# which used to feed the `_WIN32:` link flags to pkg-config and skip 4 shims
# that build fine here.

set -eu

CC="${CC:-cc}"
WARNBASE="${WARNBASE:-scripts/shim.warn}"
WFLAGS="-Wall -Wextra -Wdeprecated-declarations"

# A shim compiles here unless a pkg-config package it names is genuinely absent.
# 10 is a floor, not the count: it is what stops an empty warning file passing
# because nothing was compiled at all.
MIN_COMPILED=10

# One spelling of the `deps` parse, shared with scripts/shim_check.sh.
. "$(dirname "$0")/deps_pkgs.sh"

T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT


selfcheck() {
    rc=0
    cat > "$T/probe.c" <<'EOF'
int f(int a, int b) { int u; return a; }
EOF
    n=$($CC -std=c11 -fsyntax-only $WFLAGS "$T/probe.c" 2>&1 | grep -c ': warning: ' || true)
    if [ "$n" -ge 2 ]; then
        echo "selfcheck ok   unused parameter + unused variable seen ($n warning lines)"
    else
        echo "selfcheck FAIL the warning grep matched $n lines on a file with 2 warnings"
        rc=1
    fi

    cat > "$T/clean.c" <<'EOF'
int g(void) { return 0; }
EOF
    n=$($CC -std=c11 -fsyntax-only $WFLAGS "$T/clean.c" 2>&1 | grep -c ': warning: ' || true)
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
selfcheck > "$T/self" 2>&1 || { cat "$T/self"; echo "shim-warn: the instrument is broken, not the tree." >&2; exit 1; }

: > "$T/warn"
compiled=0
skipped=0

for shim in corelib/*/*_shim.c; do
    dir="$(dirname "$shim")"
    depflags=""

    if [ -f "$dir/deps" ]; then
        pkgs="$(pkgs_of "$dir/deps")"
        missing=""
        for pkg in $pkgs; do
            pkg-config --exists "$pkg" 2>/dev/null || missing="$missing $pkg"
        done
        if [ -n "$missing" ]; then
            echo "skip $shim (missing dependency:$missing)"
            skipped=$((skipped + 1))
            continue
        fi
        depflags="$(pkg-config --cflags $pkgs 2>/dev/null || true)"
    fi

    $CC -std=c11 -fsyntax-only -Icorelib $WFLAGS $depflags "$shim" > "$T/log" 2>&1 || true
    grep -E ': warning: ' "$T/log" >> "$T/warn" || true
    compiled=$((compiled + 1))
done

sort "$T/warn" > "$T/warn.s"

if [ "$compiled" -lt "$MIN_COMPILED" ]; then
    echo "shim-warn: FAILED (only $compiled shim(s) compiled, expected at least $MIN_COMPILED)"
    echo "  An empty warning file means nothing when nothing was compiled." >&2
    exit 1
fi

if [ "${RECORD:-0}" = "1" ]; then
    cp "$T/warn.s" "$WARNBASE"
    echo "rec     $WARNBASE ($(wc -l < "$WARNBASE" | tr -d ' ') warning line(s))"
    exit 0
fi

if [ ! -f "$WARNBASE" ]; then
    echo "shim-warn: FAILED (no baseline at $WARNBASE; RECORD=1 to write one)"
    exit 1
fi

if ! cmp -s "$T/warn.s" "$WARNBASE"; then
    echo "shim-warn: FAILED (the warnings the corelib shims emit moved)"
    echo "  cc: $($CC --version 2>/dev/null | head -1)"
    echo "  A new compiler and a new defect both land here. Read the diff:"
    diff -u "$WARNBASE" "$T/warn.s" | sed -n '3,25p'
    exit 1
fi

echo "shim-warn: ok ($compiled shim(s) compiled with $WFLAGS, $skipped skipped; $(wc -l < "$WARNBASE" | tr -d ' ') warning line(s), matching $WARNBASE)"
