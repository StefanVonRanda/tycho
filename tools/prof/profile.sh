#!/bin/sh
# Statistical CPU-time profiler for a tycho program (see prof_shim.c).
# Usage: tools/prof/profile.sh <program.ty> [input-file] [N-runs] [emitter]
#   program.ty  the tycho program to profile (compiled to C, then sampled)
#   input-file  fed to the program's stdin each run (default: /dev/null)
#   N-runs      how many runs to accumulate samples over (default: 400)
#   emitter     compiler that emits program.ty to C: "tychoc" (default) or "self"
#               ("self" = tychoc0 emitting itself — the real self-hosted codegen).
# Prints the CPU-time breakdown by the tycho function that drives each hot leaf.
#
# Example — profile the self-hosted compiler self-compiling:
#   tools/prof/profile.sh compiler/tychoc0.ty compiler/tychoc0.ty 600 self
set -u
cd "$(dirname "$0")/../.." || exit 2
HI="${1:?usage: profile.sh <program.ty> [input] [N] [tychoc|self]}"
IN="${2:-/dev/null}"; N="${3:-400}"; EMIT="${4:-tychoc}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
[ -x ./tychoc ] || { echo "build ./tychoc first (make tychoc)"; exit 2; }

if [ "$EMIT" = self ]; then
    ./tychoc compiler/tychoc0.ty -o "$T/h0" >/dev/null 2>&1 || { echo "build tychoc0 failed"; exit 1; }
    "$T/h0" < "$HI" > "$T/p.c" 2>/dev/null || { echo "self-emit failed"; exit 1; }
else
    ./tychoc "$HI" --emit-c -o "$T/p" >/dev/null 2>&1 || { echo "tychoc emit failed"; exit 1; }
fi
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
