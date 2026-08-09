#!/bin/sh
# RETIRED 2026-08-09 -- owner decision: "we're done with it". This file is kept
# as the record of what it proved, the same way compiler/fixpoint.sh and
# scripts/frontparity.sh are; nothing runs it. `make selfhost-check` is gone,
# and with it the last thing in the tree that BUILT compiler/tychoc0.ty --
# which finally makes ROADMAP.md's "as of 2026-07-29 nothing builds it at all"
# true. It was step [3n/20] of `make ci` and cost ~50s of every sweep.
#
# WHAT IS GIVEN UP, stated plainly: the byte-identity below is no longer
# re-asserted at each HEAD. It was never a check on today's compiler -- the
# chain runs the FROZEN compiler against its own FROZEN source, so the only
# thing that could redden it is tychoc changing how it compiles tychoc0.ty.
# That half is still covered: scripts/asan_self.sh feeds compiler/tychoc0.ty to
# tychoc as INPUT under ASan/UBSan, so the largest single Tycho source in the
# tree is still compiled by every sweep. What is gone is the assertion that the
# RESULT of that compile reproduces itself.
#
# WHAT STAYS TRUE: self-hosting is a fact about the commit that proved it, and
# retiring the lane does not undo it. Run this script by hand to re-check.
#
# ---- original header ----
# selfhost-check: RE-PROVE the self-hosting fixed point at HEAD.
#
# The marquee claim in the README is that Tycho compiles itself: the frozen
# compiler/tychoc0.ty, compiled by itself, reproduces its own emitted C
# byte-for-byte. The bootstrap lane (compiler/fixpoint.sh) proved this and was
# retired 2026-07-29 because its CORPUS differential could no longer run -- the
# breaking loop-syntax change means the frozen compiler cannot parse today's
# tests/examples. But the corpus differential is not the claim. The claim is
# the SELF-EMISSION chain, which involves only tychoc0 and its own frozen
# source, and it still holds (docs/bootstrap.md stages 2-4):
#
#   A = tychoc(tychoc0.ty)          the C compiler builds the Tycho compiler
#   B = cc(A(tychoc0.ty))           a Tycho-built compiler builds it again
#   C = cc(B(tychoc0.ty))           and again
#
# Asserting A(tychoc0.ty) == B(tychoc0.ty) byte-for-byte is the fixed point:
# the Tycho-built compiler reproduces its own emission exactly, so nothing
# about the C compiler's own compilation leaks into the result. This lane runs
# exactly that comparison and nothing else -- no corpus, no differential -- so
# it cannot drift the way the retired lane did.
#
# Cost: ~50s measured 2026-08-04 (stage A ~25s, stage B cc ~23s, stage C ~1s).
# In `make ci` as step [3n/20]. See docs/bootstrap.md.
set -u
cd "$(dirname "$0")/.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "selfhost: no ./tychoc -- run 'make' first"; exit 2; }
H=compiler/tychoc0.ty
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL: $*"; fail=1; }

# tychoc0 is FROZEN and predates the Windows port: it emits its own miniature
# runtime as string literals, and that copy carries the POSIX-only
# `sysconf(_SC_NPROCESSORS_ONLN)` spelling of tycho_ncpu with no _WIN32 branch
# and no <windows.h> (compiler/tychoc0.ty:10688 -- the live runtime grew its
# branch in phase 1, runtime/tycho_rt.c:1099). Stage B's C therefore does not
# COMPILE under mingw: "'_SC_NPROCESSORS_ONLN' undeclared", measured
# 2026-08-08. Nothing about the Windows port turns on this -- the fixed point
# is a property of the frozen compiler and its own frozen source, and the Linux
# lane proves it at every HEAD. Making it run here means editing a frozen
# artifact, which is an owner decision and not a port chore.
case "$(uname -s)" in *MSYS*|*MINGW*|*CYGWIN*)
    echo "selfhost: SKIP (Windows: frozen tychoc0 emits POSIX-only C -- compiler/tychoc0.ty:10688 has no _WIN32 tycho_ncpu; un-freezing it is an owner decision)"
    exit 0 ;;
esac

if ! "$TYCHOC" "$H" -o "$T/A" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc could not build compiler/tychoc0.ty"; sed 's/^/      /' "$T/build.log"
    echo "selfhost: FAIL"; exit 1
fi

# stage B: the Tycho-built compiler emits its own C
if ! "$T/A" "$H" > "$T/B.c" 2>"$T/A.err"; then
    bad "stage B: the Tycho-built compiler failed to compile its own source"
    sed 's/^/      /' "$T/A.err"
fi
if ! cc -O2 -fwrapv -std=c11 -o "$T/B" "$T/B.c" -lm 2>"$T/cc.err"; then
    bad "stage B cc: the emitted C failed to compile"
    sed 's/^/      /' "$T/cc.err"
fi

# stage C: the second-generation compiler emits its own C again
if [ "$fail" -eq 0 ]; then
    if ! "$T/B" "$H" > "$T/C.c" 2>"$T/B.err"; then
        bad "stage C: the second-generation compiler failed to compile its own source"
        sed 's/^/      /' "$T/B.err"
    fi
fi

# the fixed point: the two emissions are byte-identical
if [ "$fail" -eq 0 ]; then
    if cmp -s "$T/B.c" "$T/C.c"; then
        echo "selfhost: green (tychoc0 compiled by itself emits byte-identical C -- the fixed point holds at HEAD)"
    else
        bad "the fixed point broke: A(tychoc0.ty) != B(tychoc0.ty)"
        diff "$T/B.c" "$T/C.c" | head -5 | sed 's/^/      /'
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo "selfhost: FAIL"; exit 1
fi
