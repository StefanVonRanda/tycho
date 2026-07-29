#!/bin/sh
# Statistical CPU-time profiler for a tycho program (see prof_shim.c).
# Usage: tools/prof/profile.sh <program.ty> [input-file] [N-runs] [emitter]
#   program.ty  the tycho program to profile (compiled to C, then sampled)
#   input-file  fed to the program's stdin each run (default: /dev/null)
#   N-runs      how many runs to accumulate samples over (default: 400)
# Prints the CPU-time breakdown by the tycho function that drives each hot leaf.
#
# RETIRED MODE, 2026-07-29: a fourth argument `self` used to build a tychoc0 from
# compiler/tychoc0.ty and have it emit its own C — the real self-hosted codegen
# under the profiler, which is how the emitter's hot paths were measured on a
# 16k-line input. tychoc0 is FROZEN and the breaking loop-syntax change of
# 2026-07-29 means it can no longer parse the corpus, so no lane builds it; see
# the header of compiler/fixpoint.sh, ROADMAP.md and docs/architecture.md. The
# default `tychoc` emitter is unaffected, and `tools/prof/profile.sh
# compiler/tychoc0.ty compiler/tychoc0.ty 600` still profiles tychoc compiling
# that same 16k-line program — what is gone is profiling the SELF-HOSTED codegen.
set -u
cd "$(dirname "$0")/../.." || exit 2
HI="${1:?usage: profile.sh <program.ty> [input] [N]}"
IN="${2:-/dev/null}"; N="${3:-400}"; EMIT="${4:-tychoc}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
[ -x ./tychoc ] || { echo "build ./tychoc first (make tychoc)"; exit 2; }

if [ "$EMIT" = self ]; then
    echo "profile.sh: the 'self' emitter was RETIRED 2026-07-29 -- it built a tychoc0," >&2
    echo "            and no lane builds tychoc0 any more. Drop the 4th argument to" >&2
    echo "            profile with tychoc. See this file's header." >&2
    exit 2
fi
./tychoc "$HI" --emit-c -o "$T/p" >/dev/null 2>&1 || { echo "tychoc emit failed"; exit 1; }
cc -O2 -no-pie -rdynamic -fno-omit-frame-pointer -g "$T/p.c" tools/prof/prof_shim.c -o "$T/prof" -ldl \
    || { echo "compile failed"; exit 1; }

# Samples go to a private per-run file (prof_shim.c honors TYCHO_PROF_OUT), not
# a fixed world-shared /tmp path; it dies with $T's EXIT trap.
SYMS="$T/prof_syms.txt"; export TYCHO_PROF_OUT="$SYMS"
i=0; while [ "$i" -lt "$N" ]; do "$T/prof" < "$IN" > /dev/null; i=$((i + 1)); done
[ -s "$SYMS" ] || { echo "no samples collected ($SYMS missing or empty) -- program too fast or shim failed"; exit 1; }
TOT=$(wc -l < "$SYMS")
echo "samples: $TOT  ($N runs of $HI, emitter=$EMIT)"
echo "--- CPU by tycho function (drives the hot leaf) ---"
awk -F' <- ' '{print $2}' "$SYMS" | sort | uniq -c | sort -rn | head -15 \
    | awk -v t="$TOT" '{printf "%5.1f%% %7d  %s\n", 100*$1/t, $1, $2}'
echo "--- leaf (where the cycles actually burn) ---"
awk -F' <- ' '{print $1}' "$SYMS" | sed 's/+0x.*//' | sort | uniq -c | sort -rn | head -6 \
    | awk -v t="$TOT" '{printf "%5.1f%% %7d  %s\n", 100*$1/t, $1, $2}'
