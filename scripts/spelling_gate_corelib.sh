#!/bin/sh
# src_in_corelib (src/tychoc.c@src_in_corelib) must answer a question about
# LOCATION, not about SPELLING.
#
# corelib is exempt from the unused-local check. That exemption used to be
# `strstr(g_srcname, "corelib/")`, which was wrong in BOTH directions: every
# program under `examples/corelib/` lost the check, and a corelib reached by
# any other path lost the exemption. under_corelib canonicalises through the
# filesystem instead.
#
# The version this replaces compiled a file in a bare `mktemp -d` and said so
# in its own comment -- "the path deliberately does NOT contain corelib/".
# That is precisely what made it vacuous: with no "corelib/" anywhere in the
# path, the strstr regression and the filesystem test return the SAME answer,
# so it passed under the bug it existed to catch.
set -u
cd "$(dirname "$0")/.." || exit 2

TYCHOC="${TYCHOC:-./tychoc}"
[ -x "$TYCHOC" ] || { echo "SKIP  spelling_gate_corelib  ($TYCHOC not built)"; exit 0; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
rc=0

# [1] the discriminating input: a path that CONTAINS "corelib/" while sitting
#     outside every corelib root. The unused-local error must fire; under a
#     strstr exemption it does not.
mkdir -p "$TMP/examples/corelib/sub"
cat > "$TMP/examples/corelib/sub/unused_local.ty" << 'EOF'
fn main():
    x := 42
    println("hello")
EOF
err1=$("$TYCHOC" "$TMP/examples/corelib/sub/unused_local.ty" 2>&1)
echo "$err1" | grep -q "declared and not used" || {
    echo "FAIL  [1] a path CONTAINING 'corelib/' but outside a corelib root was exempted"
    echo "      -- src_in_corelib is answering about spelling, not location"
    echo "      got: $err1"
    rc=1
}

# [2] the negative control, and the reason this file is not decoration: patch
#     the strstr regression into a COPY of src/tychoc.c, build it, and require
#     [1] to redden under it. Without this leg [1] only asserts that the
#     unused-local check fires at all, which [3] already covers.
#     Loud-skips without a cc: it is the only leg that compiles.
if command -v cc >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1; then
    cp src/tychoc.c "$TMP/mut.c"
    if python3 scripts/spelling_gate_mutate.py "$TMP/mut.c" 2>/dev/null; then
        if cc -O0 -w -Ibuild -o "$TMP/tychoc_mut" "$TMP/mut.c" 2>"$TMP/cc.err"; then
            errm=$("$TMP/tychoc_mut" "$TMP/examples/corelib/sub/unused_local.ty" 2>&1)
            echo "$errm" | grep -q "declared and not used" && {
                echo "FAIL  [2] CONTROL DEAD: the strstr regression did NOT redden [1]."
                echo "      This gate cannot see the bug it exists to catch."
                rc=1
            }
        else
            echo "SKIP  [2] control not built (cc failed):"
            head -3 "$TMP/cc.err"
        fi
    else
        echo "FAIL  [2] could not patch src_in_corelib -- has it been renamed?"
        rc=1
    fi
else
    echo "SKIP  [2] control needs cc and python3"
fi

# [3] the baseline: an ordinary path with no 'corelib' anywhere still reports.
#     Separates "the check is broken" from "the exemption is broken" when [1]
#     reddens.
cat > "$TMP/plain.ty" << 'EOF'
fn main():
    x := 42
    println("hello")
EOF
err3=$("$TYCHOC" "$TMP/plain.ty" 2>&1)
echo "$err3" | grep -q "declared and not used" \
    || { echo "FAIL  [3] the unused-local check does not fire at all -- got: $err3"; rc=1; }

[ "$rc" = 0 ] && echo "PASS  spelling_gate_corelib (3 legs)" || echo "FAIL  spelling_gate_corelib"
exit $rc
