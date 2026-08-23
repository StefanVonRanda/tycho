#!/bin/sh
# Bounded tychoc1 fixture sweep.
#
# Three sessions were killed running `TYCHOC=./tychoc1 make test`; the third
# returned exit 137 (SIGKILL, the OOM killer) even at TYCHO_THREADS=4, while
# the same command on ./tychoc passed 752/752. So tychoc1 gets a hard memory
# cap and a timeout on every invocation, and runs sequentially.
#
# Phase 7d widened the corpus, and that widening is the point: the fixture that
# actually killed the box (tests/reject/generic_recur_grow.ty, an unbounded
# monomorphisation) sat OUTSIDE the original tests/*.ty-only sweep, so every
# bounded run reported green while the machine died. A sweep narrower than
# tests/run.sh's own corpus is a sweep that can miss the next one.
set -u
cd "$(dirname "$0")/.." || exit 2
S="${TMPDIR:-/tmp}/tychoc1_sweep.$$"
mkdir -p "$S" || exit 2
trap 'rm -rf "$S"' EXIT
CAP=2000000                                  # ulimit -v, KB -> ~2 GB
[ -x ./tychoc1 ] || { echo "no ./tychoc1 -- run 'make tychoc1'"; exit 2; }
: > "$S/bad"

# Compile one file under the cap. Echoes the outcome word; never lets a runaway
# outlive the timeout.
try_compile() {
    ( ulimit -v $CAP; timeout 5 ./tychoc1 "$1" --emit-c -o "$S/x" >/dev/null 2>&1 )
    case $? in
        0) echo ok ;;
        124) echo TIMEOUT ;;
        137|139) echo KILLED ;;
        *) echo cerr ;;
    esac
}

# --- corpora that must COMPILE, then run and match a golden ------------------
# $1 = label, $2 = dir holding <name>.out / <name>.in, $3... = files.
# The golden is NOT beside the source: tests/run.sh:158 keys both off
# tests/<basename>.out for examples/*.ty as well as tests/*.ty.
sweep_run() {
    label=$1; gdir=$2; shift 2
    comp=0; cerr=0; tmo=0; kil=0; link=0; ran=0; match=0
    for f in "$@"; do
        [ -f "$f" ] || continue
        case "$f" in */main.ty) b=$(basename "$(dirname "$f")") ;;
                     *) b=$(basename "$f" .ty) ;; esac
        case $(try_compile "$f") in
            ok) comp=$((comp + 1)) ;;
            TIMEOUT) tmo=$((tmo + 1)); echo "TIMEOUT $f" >> "$S/bad"; continue ;;
            KILLED) kil=$((kil + 1)); echo "KILLED $f" >> "$S/bad"; continue ;;
            *) cerr=$((cerr + 1)); continue ;;
        esac
        ( ulimit -v $CAP; timeout 20 ${CC:-cc} -O0 -w "$S/x.c" -o "$S/x.bin" \
            -lpthread -lm >/dev/null 2>&1 ) || continue
        link=$((link + 1))
        # stdin from the fixture's .in when it has one -- io_builtins blocks
        # forever without it, which reads as a hang and is not one.
        in=/dev/null; [ -f "$gdir/$b.in" ] && in="$gdir/$b.in"
        ( ulimit -v $CAP; timeout 5 "$S/x.bin" < "$in" > "$S/x.out" 2>/dev/null )
        case $? in
            124) echo "HANG $f" >> "$S/bad"; continue ;;
            137|139) echo "RUNAWAY $f" >> "$S/bad"; continue ;;
        esac
        ran=$((ran + 1))
        [ -f "$gdir/$b.out" ] && cmp -s "$S/x.out" "$gdir/$b.out" && match=$((match + 1))
    done
    echo "$label: compile=$comp clean-error=$cerr link=$link RUN=$ran MATCH=$match TIMEOUT=$tmo KILLED=$kil"
    T_TMO=$((T_TMO + tmo)); T_KIL=$((T_KIL + kil))
}

# --- corpora that must be REFUSED -------------------------------------------
# A reject fixture only has to fail; a parser/checker cannot see most of them.
# What matters HERE is that none of them runs away -- generic_recur_grow did.
sweep_reject() {
    label=$1; shift
    n=0; ref=0; acc=0; tmo=0; kil=0
    for f in "$@"; do
        [ -f "$f" ] || continue
        n=$((n + 1))
        case $(try_compile "$f") in
            ok) acc=$((acc + 1)) ;;
            TIMEOUT) tmo=$((tmo + 1)); echo "TIMEOUT $f" >> "$S/bad" ;;
            KILLED) kil=$((kil + 1)); echo "KILLED $f" >> "$S/bad" ;;
            *) ref=$((ref + 1)) ;;
        esac
    done
    echo "$label: n=$n refused=$ref accepted=$acc TIMEOUT=$tmo KILLED=$kil"
    T_TMO=$((T_TMO + tmo)); T_KIL=$((T_KIL + kil))
}

T_TMO=0; T_KIL=0
sweep_run    "tests      " tests      tests/*.ty
sweep_run    "examples   " tests      examples/*.ty
sweep_run    "tests/pkg  " tests/pkg  tests/pkg/*/main.ty
# abort/diag/warn compare STDERR and exit status, which this sweep does not
# model -- they are swept for runaways only, and their MATCH is meaningless.
sweep_reject "abort(cmp) " tests/abort/*.ty
sweep_reject "diag (cmp) " tests/diag/*.ty
sweep_reject "warn (cmp) " tests/warn/*.ty tests/warn/pkg/*/main.ty
sweep_reject "reject     " tests/reject/*.ty
sweep_reject "reject/pkg " tests/reject/pkg/*/main.ty

echo "TOTAL TIMEOUT=$T_TMO KILLED=$T_KIL"
if [ -s "$S/bad" ]; then
    echo "-- not clean --"
    cat "$S/bad"
fi
# A runaway is the one outcome that can take the machine down, so it is the one
# outcome that fails the sweep. Everything else is a count to read.
[ "$T_TMO" -eq 0 ] && [ "$T_KIL" -eq 0 ] || exit 1
exit 0
