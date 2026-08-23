#!/bin/sh
# Bounded tychoc1 fixture sweep.
#
# Three sessions were killed running `TYCHOC=./tychoc1 make test`; the third
# returned exit 137 (SIGKILL, the OOM killer) even at TYCHO_THREADS=4, while
# the same command on ./tychoc passed 752/752. So tychoc1 gets a hard memory
# cap and a timeout on every invocation, and runs sequentially. Do not replace
# this with `make test` until Phase 7c has named the fixture that OOMs.
set -u
cd "$(dirname "$0")/.." || exit 2
S="${TMPDIR:-/tmp}/tychoc1_sweep.$$"
mkdir -p "$S" || exit 2
trap 'rm -rf "$S"' EXIT
CAP=2000000                                  # ulimit -v, KB -> ~2 GB
[ -x ./tychoc1 ] || { echo "no ./tychoc1 -- run 'make tychoc1'"; exit 2; }

comp=0; cerr=0; tmo=0; kill=0; link=0; ran=0; match=0
: > "$S/bad"
for f in tests/*.ty; do
    b=$(basename "$f" .ty)
    ( ulimit -v $CAP; timeout 5 ./tychoc1 "$f" --emit-c -o "$S/x" >/dev/null 2>&1 )
    case $? in
        0) comp=$((comp + 1)) ;;
        124) tmo=$((tmo + 1));  echo "TIMEOUT $b" >> "$S/bad"; continue ;;
        137|139) kill=$((kill + 1)); echo "KILLED $b" >> "$S/bad"; continue ;;
        *) cerr=$((cerr + 1)); continue ;;
    esac
    ( ulimit -v $CAP; timeout 20 ${CC:-cc} -O0 -w "$S/x.c" -o "$S/x.bin" \
        -lpthread -lm >/dev/null 2>&1 ) || continue
    link=$((link + 1))
    # stdin from the fixture's .in when it has one -- io_builtins blocks forever
    # without it, which reads as a hang and is not one.
    if [ -f "tests/$b.in" ]; then
        ( ulimit -v $CAP; timeout 5 "$S/x.bin" < "tests/$b.in" > "$S/x.out" 2>/dev/null )
    else
        ( ulimit -v $CAP; timeout 5 "$S/x.bin" < /dev/null > "$S/x.out" 2>/dev/null )
    fi
    case $? in
        124) echo "HANG $b" >> "$S/bad"; continue ;;
        137|139) echo "RUNAWAY $b" >> "$S/bad"; continue ;;
    esac
    ran=$((ran + 1))
    [ -f "tests/$b.out" ] && cmp -s "$S/x.out" "tests/$b.out" && match=$((match + 1))
done
echo "compile=$comp clean-error=$cerr TIMEOUT=$tmo KILLED=$kill"
echo "link=$link RUN=$ran MATCH=$match"
[ -s "$S/bad" ] && { echo "-- not clean --"; cat "$S/bad"; }
exit 0
