#!/bin/sh
# Gate for tycho-flow, the concurrent pipeline engine in tools/tycho-flow/ --
# stage/ (generic stage combinators over bounded channels) and main.ty (the
# demo pipeline wired end to end).
#
# Re-record the golden with:  RECORD=1 sh tools/tycho-flow/run.sh
#
# WHY THIS IS NOT A GOLDEN LANE WITH EXTRA STEPS. Everything this program
# claims is a claim about CONCURRENCY, and a diff against a recorded
# transcript cannot see any of it: a pipeline that lost its ordering, that
# stopped being bounded, or that races on the ring can all still print the
# expected bytes on the run that happens to schedule kindly. So the golden is
# leg [1] of five, and the other four each assert something a golden cannot.
#
# WHAT IT ASSERTS
#   [1] DETERMINISM UNDER SCHEDULING PRESSURE. The demo is run 8 times with the
#       default pool width and once each at TYCHO_THREADS=1 and 2; all ten
#       transcripts must be cmp-identical, and the first one must equal the
#       golden. Thread count is the variable the answer must not depend on --
#       reorder-by-index and the commutative fold are the two mechanisms that
#       make that true, and this is the only leg that can catch either
#       breaking.
#   [2] THE RACE IS REAL, which is what stops [1] being vacuous. `--race N`
#       runs the exact `run_words` pipeline N times and counts how often the
#       pool DRAINED out of source order. With an equal-cost transform that
#       count was 7 in 2000 (47b6d5b7's own measurement) -- output that is
#       ordered because nothing raced, not because the index put it back. So:
#       at the default width, 200 runs must be out of order at least 190
#       times; and as the negative control that this is measuring the pool and
#       not the phase of the moon, 25 runs at TYCHO_THREADS=1 must be out of
#       order EXACTLY 0 times. The counts are races and never reach the golden.
#       `--race 2000` is 2000/2000 here and takes 28s; the lane buys the same
#       claim at 200 for a seventh of the wall clock.
#   [3] BACKPRESSURE, against literals in this runner rather than a slice of
#       the golden -- a re-record must not be able to bless a channel that
#       stopped being bounded. `source_probed` posts a marker only after a data
#       send RETURNS, so with a 4-slot ring and no receiver exactly 4 markers
#       can exist and the 5th cannot: the producer is parked inside send #5.
#       No timing, no sleep -- the ring forces both halves, which is why the
#       three lines are byte-stable.
#   [4] every variant of stage.FlowErr exits NON-ZERO with its own whole
#       message and an empty stdout. The demo prints them as ok lines and
#       exits 0, so this needs a caller that returns them: the runner copies
#       stage/ into its temp dir and builds a probe whose main returns
#       Err(stage.err_str(e)). Nothing is written into the repo. The variant
#       list is READ out of the enum and checked against what the probe
#       covers, so a variant added tomorrow reddens here instead of arriving
#       ungated.
#   [5] TSAN over the whole demo, and over a short --race. A capture bug, a
#       ring index published without a release, or a `parallel for` reduction
#       folded unsafely is a DATA RACE -- it produces the right answer on this
#       machine and the wrong one on the next, so legs [1]-[3] can all be green
#       while the program is broken. `make conc` is the precedent. Skips
#       loudly, exit 0, where cc has no TSan runtime. It found one race on its
#       first outing and that race is NOT in this program: tychoc emits string
#       literals as lazily interned function statics, which the pool's workers
#       write and read unordered. Those reports are classified, noted out loud
#       and tolerated; every other report fails the lane. See the leg itself.
#
# WHAT IT DELIBERATELY DOES NOT ASSERT
#   Timings. `--bench` measures the deep-copy cost at the thread boundary and
#   is nondeterministic by construction; its numbers are recorded in main.ty's
#   header with the date they were measured, and nothing here runs it.
#   The pool's WIDTH, or which worker got which element. Neither is a promise;
#   the promise is that the answer does not depend on either.
#
# NO HOST DETAIL REACHES THE GOLDEN -- the program takes no paths and prints
# none. Every run below is bounded by $TO where a timeout(1) exists: a
# concurrency gate that HANGS tells a reader nothing, and tools/tycho-db/run.sh
# hit exactly that with a bare `wait`.
set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "tycho-flow: no ./tychoc -- run 'make' first"; exit 2; }
TYCHOC="$PWD/tychoc"          # absolute: the probe in [4] is built after a cd
export TYCHO_CORELIB="$PWD/corelib"
CC="${CC:-cc}"
RECORD="${RECORD:-0}"
golden="$PWD/tools/tycho-flow/expected.out"
src="$PWD/tools/tycho-flow"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT INT TERM
fail=0
bad() { echo "FAIL: $*"; fail=1; }

# Nothing here spawns a background process, so there is no `wait` to hang on.
# What can hang is a run: a lost wakeup on the ring parks the pipeline forever
# and an unbounded gate would sit there until CI's own timeout killed it with
# no verdict. Every invocation below goes through $TO.
if command -v timeout >/dev/null 2>&1; then TO="timeout 120"
elif command -v gtimeout >/dev/null 2>&1; then TO="gtimeout 120"
else TO=""; fi

FLOW="$T/tycho-flow"
if ! "$TYCHOC" "$src/main.ty" -o "$FLOW" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/build.log"
    echo "tycho-flow: FAIL"; exit 1
fi

out="$T/all.out"
: > "$out"

# `demo <label> <file> [env-assignment...]` -- one bounded run, exit 0 and a
# silent stderr required. A run that dies or warns is a failure whatever its
# stdout says.
demo() {
    _lbl=$1; _f=$2; shift 2
    env "$@" $TO "$FLOW" > "$_f" 2> "$T/e.err"
    _rc=$?
    [ "$_rc" -eq 0 ] || { bad "$_lbl: exited $_rc, expected 0"; sed 's/^/      /' "$T/e.err"; }
    [ -s "$T/e.err" ] && { bad "$_lbl: wrote to stderr"; sed 's/^/      /' "$T/e.err"; }
    return 0
}

# ---------------------------------------------------------------------------
# [1] determinism: 8 runs at the default width, then width 1 and width 2
#
# The middle of the first pipeline is a fan-out pool emitting in COMPLETION
# order, so identical transcripts are not a property of the input -- they are
# reorder-by-index and the commutative fold doing their job. Varying
# TYCHO_THREADS is the sharpest form of the question: an answer that depends on
# the pool width is an answer that depends on the machine.
# ---------------------------------------------------------------------------
demo "run 1" "$T/d.1"
for n in 2 3 4 5 6 7 8; do
    demo "run $n" "$T/d.$n"
    cmp -s "$T/d.1" "$T/d.$n" || {
        bad "the demo transcript is not deterministic (run 1 vs run $n)"
        diff "$T/d.1" "$T/d.$n" | sed 's/^/      /'
    }
done
for w in 1 2; do
    demo "run TYCHO_THREADS=$w" "$T/d.t$w" "TYCHO_THREADS=$w"
    cmp -s "$T/d.1" "$T/d.t$w" || {
        bad "the answer depends on the pool width (default vs TYCHO_THREADS=$w)"
        diff "$T/d.1" "$T/d.t$w" | sed 's/^/      /'
    }
done

printf '=== demo\n' >> "$out"
cat "$T/d.1" >> "$out"

# ---------------------------------------------------------------------------
# [2] is the middle actually unordered?
#
# `race: X of N runs drained out of source order`. X near N means reorder is
# load-bearing on essentially every run, so [1] proved something. X == 0 at the
# default width would mean the pool never delivers out of order here and [1]
# would be proving that a sequential program is deterministic.
#
# The floor is 95%, not 100%: the claim is "this races on essentially every
# run", and pinning the last handful of runs would make the lane a coin toss on
# a machine with fewer cores than this one. A single-core box cannot race at
# all -- `parallel for` is one chunk -- so it SKIPS loudly rather than
# reddening for its own hardware.
# ---------------------------------------------------------------------------
racecount() {
    _lbl=$1; _n=$2; shift 2
    env "$@" $TO "$FLOW" --race "$_n" > "$T/r.out" 2> "$T/r.err"
    _rc=$?
    [ "$_rc" -eq 0 ] || { bad "$_lbl: --race exited $_rc"; sed 's/^/      /' "$T/r.err"; return 1; }
    RC=$(sed -n "s/^race: \([0-9][0-9]*\) of $_n runs drained out of source order$/\1/p" "$T/r.out")
    [ -n "$RC" ] || { bad "$_lbl: --race printed no count this runner could read"; sed 's/^/      /' "$T/r.out"; return 1; }
    return 0
}

ncpu=$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || echo 1)
if [ "$ncpu" -le 1 ] 2>/dev/null; then
    echo "SKIP tycho-flow race leg (1 cpu online -- a pool of one cannot deliver out of order)"
else
    if racecount "race" 200; then
        if [ "$RC" -lt 190 ]; then
            bad "the pool drained out of order only $RC of 200 runs -- reorder is barely load-bearing, so the determinism above proves little"
        fi
    fi
fi
# The negative control. If this is not 0, the count above is not measuring the
# pool -- it is measuring something that happens with one worker too, and the
# 200-run number stops being evidence of anything.
if racecount "race(1 thread)" 25 "TYCHO_THREADS=1"; then
    [ "$RC" -eq 0 ] || bad "TYCHO_THREADS=1 drained out of order $RC of 25 runs -- a single worker cannot reorder, so --race is not measuring the pool"
fi

# ---------------------------------------------------------------------------
# [3] backpressure, against literals
#
# A 512-element source into a 4-slot ring. These three lines are the whole
# proof and they are asserted HERE, not left to the golden: RECORD=1 rewrites
# the golden, and "the channel is still bounded" is precisely the claim that
# must not be blessable. An unbounded channel would let all 512 sends complete
# and marker 5 would be sitting there.
# ---------------------------------------------------------------------------
bp() {
    grep -qxF "$1" "$T/d.1" || bad "backpressure: expected line missing -- '$1'"
}
# The cap first, because every line under it is derived from that one number in
# run_backpressure. Loosen the ring and all four lines move together; that is
# what makes them worth asserting, and until 2026-08-12 they did not -- the
# marker loop counted to a hand-written 4, so a ring of 8 printed "filled with
# 4" and this leg was green over 200 runs of a program that was no longer
# proving anything.
bp 'backpressure: a 512-element source into a 4-slot channel'
bp '  ring  filled with 4 element(s) and no receiver'
bp '  park  send 5 is blocked: no marker for it exists'
bp '  drain 512 element(s) and 508 marker(s) after release, producer sent 512'
# The demo prints FAIL lines rather than dying, so a silent green is possible
# if one ever fires. It must not.
grep -n '  FAIL  ' "$T/d.1" | sed 's/^/      /' | grep . && bad "the demo printed a FAIL line"

# ---------------------------------------------------------------------------
# [4] every FlowErr variant, exiting non-zero with its own whole message
#
# The demo REPORTS these variants and exits 0. A caller that propagates one has
# to die by it, and this is that caller: `main() -> Result(void, string)`
# returning Err(stage.err_str(e)) puts the message on stderr and the failure in
# the exit status. stage/ is COPIED into the temp dir -- nothing is written
# into the repo, and a renamed package reddens here.
# ---------------------------------------------------------------------------
P="$T/pkg"; mkdir -p "$P"
[ -d "$src/stage" ] || bad "probe: $src/stage is gone -- this leg asserts NOTHING"
cp -R "$src/stage" "$P/" 2>/dev/null
cat > "$P/probe.ty" <<'EOF'
package main

import "stage"

# One variant per run, named on the command line. Each arm calls the API that
# owns the variant rather than constructing the enum -- a probe that built
# FlowErr by hand would assert err_str and nothing else.
fn main() -> Result(void, string):
    a := args()
    if len(a) < 2:
        return Err("usage: probe <variant>")
    if a[1] == "Truncated":
        short := [stage.Item(0, 7), stage.Item(1, 8)]
        match stage.reorder(short, 3):
            Ok(vs): return Err("reorder ACCEPTED a collection short of its expected count")
            Err(e): return Err(stage.err_str(e))
    if a[1] == "DuplicateIndex":
        dup := [stage.Item(0, "x"), stage.Item(0, "y")]
        match stage.reorder(dup, 2):
            Ok(vs): return Err("reorder ACCEPTED two items in the same slot")
            Err(e): return Err(stage.err_str(e))
    if a[1] == "Dropped":
        match stage.check_link("filter", 9, 7):
            Ok(): return Err("check_link ACCEPTED a link that lost elements")
            Err(e): return Err(stage.err_str(e))
    return Err("unknown variant " + a[1])
EOF
if ! "$TYCHOC" "$P/probe.ty" -o "$T/probe" >"$T/probe.log" 2>&1; then
    bad "probe: tychoc could not build the FlowErr probe"
    sed 's/^/      /' "$T/probe.log" | head -8
else
    # <variant> <the whole message it must die with>
    errcase() {
        _v=$1; _msg=$2
        $TO "$T/probe" "$_v" > "$T/c.out" 2> "$T/c.err"
        _rc=$?
        if [ "$_rc" -eq 0 ]; then
            bad "$_v: EXITED 0 -- the API accepted what the variant exists to refuse"
        elif ! grep -qxF "$_msg" "$T/c.err"; then
            bad "$_v: failed but not with its own whole message"; sed 's/^/      /' "$T/c.err"
        fi
        [ -s "$T/c.out" ] && bad "$_v: wrote to STDOUT"
        printf '=== err %s (via the stage API)\n' "$_v" >> "$out"
        cat "$T/c.err" >> "$out"
    }
    errcase Truncated      'truncated: expected 3 item(s), collected 2'
    errcase DuplicateIndex 'duplicate index 0: the index set is not a permutation'
    errcase Dropped        'filter dropped 2: 9 sent, 7 received'
fi

# The coverage floor: the enum is READ, not remembered.
COVERED='Truncated DuplicateIndex Dropped'
found=0
for v in $(awk '
        $0 == "enum FlowErr:" { on = 1; next }
        on && /^[^ \t]/ { on = 0 }
        on && $1 ~ /^#/ { next }
        on && NF { v = $1; sub(/\(.*/, "", v); print v }
    ' "$src/stage/stage.ty"); do
    found=$((found + 1))
    hit=0
    for c in $COVERED; do [ "$v" = "$c" ] && hit=1; done
    [ "$hit" -eq 1 ] || bad "FlowErr variant $v has no leg in this runner -- it is UNGATED"
done
[ "$found" -eq 3 ] || bad "found $found FlowErr variant(s) in stage.ty, expected 3 -- the scan is broken and [4]'s floor asserts nothing"

# ---------------------------------------------------------------------------
# [5] TSan
#
# The one leg that can see a bug legs [1]-[4] are structurally blind to. Those
# ask what the program printed; this asks whether two threads touched the same
# word without an ordering between them, which is the failure mode of a ring,
# a capture set and a parallel reduction alike -- and which prints the right
# answer right up until the machine changes.
#
# --emit-c then one cc line, because that is how the real build works: the
# shims come from `--print-shims`. A TSan report goes to stderr and does NOT
# by itself change the exit status, so the assertion is a SILENT stderr, same
# as tests/conc/run.sh.
# ---------------------------------------------------------------------------
printf 'int main(void){return 0;}\n' > "$T/probe.c"
if ! $CC -fsanitize=thread -o "$T/probe.tsan" "$T/probe.c" >/dev/null 2>&1; then
    echo "SKIP tycho-flow TSan leg ($CC has no ThreadSanitizer runtime)"
else
    if ! "$TYCHOC" "$src/main.ty" --emit-c -o "$T/flowc" >"$T/emit.log" 2>&1; then
        bad "tsan: tychoc --emit-c failed"; sed 's/^/      /' "$T/emit.log" | head -8
    else
        SHIMS=$("$TYCHOC" "$src/main.ty" --print-shims 2>/dev/null | tr '\n' ' ')
        if ! $CC -fsanitize=thread -g -O1 -fwrapv -pthread -o "$T/flow.tsan" \
                "$T/flowc.c" $SHIMS -lm 2>"$T/tsan.cc"; then
            bad "tsan: cc failed"; sed 's/^/      /' "$T/tsan.cc" | head -8
        else
            # ONE KNOWN RACE IS TOLERATED, and it is not this program's.
            # tychoc emits every string literal as a lazily interned function
            # `static`:
            #
            #     ({ static char *_l = 0; if (!_l) _l = tycho_str_intern("#"); _l; })
            #
            # and `tycho_str_intern` (runtime/tycho_rt.c@tycho_str_intern) is a
            # plain malloc+memcpy, not a table. So the FIRST worker to reach a
            # literal mallocs a copy and publishes the pointer in `_l` while its
            # siblings read both, with nothing ordering any of it. Two reports
            # come out of the one bug: the racy `_l` pointer, and the racy READ
            # of the heap block another thread interned. Roughly 3 runs in 12
            # here (measured 2026-08-12, on `h_stamp` -- the pool's own per-item
            # work). It is tree-wide codegen reachable from any literal a
            # spawned function evaluates, the publish has no release ordering,
            # so it is a portability bug and not only a formality; fixing it
            # belongs in src/tychoc.c, not in this lane -- see plan.md.
            #
            # So the leg CLASSIFIES rather than counts. A report that names one
            # of those `_l` statics or `tycho_str_intern` is noted out loud and
            # tolerated; any other report fails the lane, which is the whole
            # point of running TSan here. Suppressing `h_stamp` wholesale, which
            # is what a TSan suppressions file would have bought, would have
            # hidden a real race in the same function.
            tsan_run() {
                _lbl=$1; shift
                $TO "$@" > "$T/ts.out" 2> "$T/ts.err"
                _rc=$?
                # 66 is TSan's exit status when it filed a report; the program
                # itself still ran to completion.
                [ "$_rc" -eq 0 ] || [ "$_rc" -eq 66 ] || {
                    bad "tsan: $_lbl exited $_rc"; sed 's/^/      /' "$T/ts.err" | head -20; return 1; }
                set -- $(awk '
                    /WARNING: ThreadSanitizer/ { n++; known[n] = 0 }
                    n && (/Location is global ._l/ || /tycho_str_intern/) { known[n] = 1 }
                    END { for (i = 1; i <= n; i++) if (known[i]) k++; else u++
                          printf "%d %d\n", k + 0, u + 0 }' "$T/ts.err")
                if [ "$2" -gt 0 ]; then
                    bad "tsan: $_lbl reported $2 race(s) that are NOT the known interned-literal statics"
                    sed 's/^/      /' "$T/ts.err" | head -40
                elif [ "$1" -gt 0 ]; then
                    echo "note tycho-flow: $_lbl -- $1 known interned-literal race(s) tolerated (tychoc codegen, see plan.md)"
                fi
                return 0
            }
            # The demo: every stage, the pool, the bounded ring and the
            # parallel reduction, under the race detector.
            tsan_run "the demo" "$T/flow.tsan"
            cmp -s "$T/ts.out" "$T/d.1" || {
                bad "tsan: the instrumented build printed a different answer"
                diff "$T/d.1" "$T/ts.out" | sed 's/^/      /'
            }
            # And under scheduling pressure: 15 more runs of the same pipeline,
            # which is where a rare ring race gets its chances.
            tsan_run "--race 15" "$T/flow.tsan" --race 15
        fi
    fi
fi

# ---------------------------------------------------------------------------
# the golden
# ---------------------------------------------------------------------------
if [ "$RECORD" = 1 ]; then
    if [ "$fail" -ne 0 ]; then echo "FAIL: refusing to RECORD from a failed run"; exit 1; fi
    cp "$out" "$golden"; echo "rec  tycho-flow"
fi
if [ ! -f "$golden" ]; then
    bad "no golden -- run RECORD=1 sh tools/tycho-flow/run.sh"
elif ! cmp -s "$out" "$golden"; then
    bad "transcript != golden"; diff "$golden" "$out" | sed 's/^/      /'
fi

if [ "$fail" -eq 0 ]; then
    echo "tycho-flow: green (8 default runs plus TYCHO_THREADS=1 and 2 all byte-identical; the pool drained out of source order on essentially every one of 200 runs and on 0 of 25 at one thread; the 4-slot ring parked send 5 with no marker for it; $found FlowErr variants each exit non-zero with their own whole message and an empty stdout; TSan over the demo and 15 more pipelines reported nothing but the known interned-literal races; transcript == golden)"
else
    echo "tycho-flow: FAIL"; exit 1
fi
