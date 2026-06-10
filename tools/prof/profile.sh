#!/bin/sh
# Statistical CPU-time profiler for a hier program (see prof_shim.c).
# Usage: tools/prof/profile.sh <program.hi> [input-file] [N-runs] [emitter]
#   program.hi  the hier program to profile (compiled to C, then sampled)
#   input-file  fed to the program's stdin each run (default: /dev/null)
#   N-runs      how many runs to accumulate samples over (default: 400)
#   emitter     compiler that emits program.hi to C: "hierc" (default) or "self"
#               ("self" = hierc0 emitting itself — the real self-hosted codegen).
# Prints the CPU-time breakdown by the hier function that drives each hot leaf.
#
# Example — profile the self-hosted compiler self-compiling:
#   tools/prof/profile.sh compiler/hierc0.hi compiler/hierc0.hi 600 self
set -u
cd "$(dirname "$0")/../.." || exit 2
HI="${1:?usage: profile.sh <program.hi> [input] [N] [hierc|self]}"
IN="${2:-/dev/null}"; N="${3:-400}"; EMIT="${4:-hierc}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
[ -x ./hierc ] || { echo "build ./hierc first (make hierc)"; exit 2; }

if [ "$EMIT" = self ]; then
    ./hierc compiler/hierc0.hi -o "$T/h0" >/dev/null 2>&1 || { echo "build hierc0 failed"; exit 1; }
    "$T/h0" < "$HI" > "$T/p.c" 2>/dev/null || { echo "self-emit failed"; exit 1; }
else
    ./hierc "$HI" --emit-c -o "$T/p" >/dev/null 2>&1 || { echo "hierc emit failed"; exit 1; }
fi
cc -O2 -no-pie -rdynamic -fno-omit-frame-pointer -g "$T/p.c" tools/prof/prof_shim.c -o "$T/prof" -ldl \
    || { echo "compile failed"; exit 1; }

# Samples go to a private per-run file (prof_shim.c honors HIER_PROF_OUT), not
# a fixed world-shared /tmp path; it dies with $T's EXIT trap.
SYMS="$T/prof_syms.txt"; export HIER_PROF_OUT="$SYMS"
i=0; while [ "$i" -lt "$N" ]; do "$T/prof" < "$IN" > /dev/null; i=$((i + 1)); done
[ -s "$SYMS" ] || { echo "no samples collected ($SYMS missing or empty) -- program too fast or shim failed"; exit 1; }
TOT=$(wc -l < "$SYMS")
echo "samples: $TOT  ($N runs of $HI, emitter=$EMIT)"
echo "--- CPU by hier function (drives the hot leaf) ---"
awk -F' <- ' '{print $2}' "$SYMS" | sort | uniq -c | sort -rn | head -15 \
    | awk -v t="$TOT" '{printf "%5.1f%% %7d  %s\n", 100*$1/t, $1, $2}'
echo "--- leaf (where the cycles actually burn) ---"
awk -F' <- ' '{print $1}' "$SYMS" | sed 's/+0x.*//' | sort | uniq -c | sort -rn | head -6 \
    | awk -v t="$TOT" '{printf "%5.1f%% %7d  %s\n", 100*$1/t, $1, $2}'
