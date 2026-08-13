#!/bin/sh
# Gate for tycho-sim, the entity simulation in tools/tycho-sim/ -- world/ (the
# slot map, the `soa` component pools and the `subscript` projections into
# them) and main.ty (the three-mode driver that stands in for a game loop).
#
# Re-record the golden with:  RECORD=1 sh tools/tycho-sim/run.sh
#
# WHY THIS IS NOT A GOLDEN LANE WITH EXTRA STEPS. The subject is SWAP-REMOVE,
# and a recorded transcript is the one instrument that cannot see the bug it
# exists to catch. Despawn moves the last entity down over the hole and pops;
# forget to re-point the moved entity's slot and the POOL LENGTH IS STILL
# RIGHT -- every count, every field-wise sum over a dense walk, every "N live"
# line still reads correctly, and exactly one id starts addressing somebody
# else. A golden recorded from that build agrees with it and cmp is green by
# construction. So the survivor SET is asserted against values this runner
# computes itself, where RECORD=1 cannot reach it. The golden is leg [1] of
# five and the weakest of them.
#
# WHAT IT ASSERTS
#   [1] THE TRANSCRIPT, twice. --demo, --sweep=24 and --stale are each run
#       twice; the two runs must be cmp-identical to each other and the first
#       to the golden. The program reads no clock, no path and no environment
#       and its one PRNG is seeded from a literal, so a difference between two
#       runs is uninitialised state, not scheduling.
#   [2] THE LIVE COUNT, against arithmetic in this runner. --sweep=N spawns N
#       and despawns the odd-numbered ones, so N - N/2 must survive. The number
#       is computed HERE from N, not sliced out of the golden.
#   [3] EVERY SURVIVOR REACHABLE THROUGH ITS ID, by exact set. The sweep gives
#       entity i an hp of i*7+1, so the survivors' hps are that expression over
#       the even i -- generated here and compared as a whole block. This is the
#       leg [2] cannot be: a swap-remove that dropped one entity and duplicated
#       another leaves the COUNT untouched and moves this block. Both are run
#       against the same transcript so a break can redden either, either
#       separately, or both, and which it does is the diagnosis.
#   [4] A STALE ID IS REFUSED BY THE GENERATION. --stale despawns an entity and
#       respawns into the same slot; the original id must not resolve. Three
#       assertions, because the obvious one is vacuous on its own:
#         - the freed-but-unreused id is refused as a DESPAWNED entity,
#         - the slot really was reused (same idx, generation moved), which is
#           what makes the third refusal the generation and not an empty slot,
#         - the reused id is refused as STALE, naming both generations.
#       If the slot were not reused, the program prints a FLOOR line and this
#       runner fails on it.
#   [5] EVERY SimErr VARIANT exits NON-ZERO with its own whole message and an
#       empty stdout. The driver REPORTS a refusal and exits 0 by design -- a
#       refusal is the observation, not a crash -- so this needs a different
#       caller: the runner copies world/ into its temp dir and builds a probe
#       whose `main` returns Err(world.err_str(e)). Nothing is written into the
#       repo. Each arm reaches the variant through the API that owns it where
#       there is one, and the variant list is READ out of the enum, so a
#       variant added tomorrow reddens here instead of arriving ungated.
#
# WHAT IT DELIBERATELY DOES NOT ASSERT
#   Any timing. There is no measurement in this slice.
#   The systems -- movement, combat, decay. There are none yet; that is the
#   next slice, not an omission here.
#
# NO HOST DETAIL REACHES THE GOLDEN -- the program prints no paths and reads no
# environment. Every run below is bounded by $TO where a timeout(1) exists: a
# gate that HANGS tells a reader nothing.
set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "tycho-sim: no ./tychoc -- run 'make' first"; exit 2; }
TYCHOC="$PWD/tychoc"          # absolute: the probe in [5] is built after a cd
export TYCHO_CORELIB="$PWD/corelib"
RECORD="${RECORD:-0}"
golden="$PWD/tools/tycho-sim/expected.out"
src="$PWD/tools/tycho-sim"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT INT TERM
fail=0
bad() { echo "FAIL: $*"; fail=1; }

# What can hang is a run, not a wait: despawn pops inside a loop over ids and a
# swap-remove that stopped shrinking the pool is an infinite loop, not a wrong
# answer. An unbounded gate would sit there with no verdict.
if command -v timeout >/dev/null 2>&1; then TO="timeout 60"
elif command -v gtimeout >/dev/null 2>&1; then TO="gtimeout 60"
else TO=""; fi

SIM="$T/tycho-sim"
if ! "$TYCHOC" "$src/main.ty" -o "$SIM" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/build.log"
    echo "tycho-sim: FAIL"; exit 1
fi

out="$T/all.out"
: > "$out"

# `simrun <label> <file> <arg>` -- one bounded run, exit 0 and a silent stderr
# required. A run that dies or warns is a failure whatever its stdout says.
simrun() {
    _lbl=$1; _f=$2; shift 2
    $TO "$SIM" "$@" > "$_f" 2> "$T/e.err"
    _rc=$?
    [ "$_rc" -eq 0 ] || { bad "$_lbl: exited $_rc, expected 0"; sed 's/^/      /' "$T/e.err"; }
    [ -s "$T/e.err" ] && { bad "$_lbl: wrote to stderr"; sed 's/^/      /' "$T/e.err"; }
    return 0
}

ln_() {
    grep -qxF "$1" "$2" || bad "expected line missing from $(basename "$2") -- '$1'"
}

# ---------------------------------------------------------------------------
# [1] the three modes, each twice
# ---------------------------------------------------------------------------
N=24                              # the sweep's size; every number in [2] and [3] derives from it

simrun "demo run 1"  "$T/demo.1"  --demo
simrun "demo run 2"  "$T/demo.2"  --demo
simrun "sweep run 1" "$T/sweep.1" "--sweep=$N"
simrun "sweep run 2" "$T/sweep.2" "--sweep=$N"
simrun "stale run 1" "$T/stale.1" --stale
simrun "stale run 2" "$T/stale.2" --stale

for m in demo sweep stale; do
    cmp -s "$T/$m.1" "$T/$m.2" || {
        bad "the $m transcript is not deterministic (run 1 vs run 2)"
        diff "$T/$m.1" "$T/$m.2" | sed 's/^/      /'
    }
done

cat "$T/demo.1" "$T/sweep.1" "$T/stale.1" >> "$out"

# ---------------------------------------------------------------------------
# [2] the live count, computed here
#
# N spawned, the odd-numbered ones despawned: N/2 despawns, so N - N/2 survive.
# Asserted from N rather than read off the golden, because RECORD=1 rewrites
# the golden and "the pool still holds the right number of entities" is exactly
# the claim that must not be blessable from a broken build.
# ---------------------------------------------------------------------------
want_live=$(( N - N / 2 ))
ln_ "live=$want_live" "$T/sweep.1"
# The floor: a sweep that despawned NOTHING would also print a consistent
# count. The odd ones must really be gone, and the demo's own cross-check --
# ids that resolve vs pool length -- must agree.
[ "$want_live" -lt "$N" ] || bad "sweep: N=$N despawns nothing -- [2] and [3] assert nothing"
demo_live=$(sed -n 's/^demo: handed out [0-9]* ids, \([0-9]*\) resolve, live=\([0-9]*\)$/\1 \2/p' "$T/demo.1")
[ -n "$demo_live" ] || bad "demo: no summary line -- the cross-check below asserts nothing"
set -- $demo_live
[ "${1:-x}" = "${2:-y}" ] || bad "demo: $1 ids resolve but the pool holds $2 -- an id addresses an entity that is not there, or an entity has no id"

# ---------------------------------------------------------------------------
# [3] the survivor SET, generated here
#
# Entity i is spawned with hp = i*7+1 and the odd ones are despawned, so the
# survivors are exactly the even i. This block is what a lost or duplicated
# entity moves while leaving [2] alone.
# ---------------------------------------------------------------------------
awk -v n="$N" 'BEGIN { for (i = 0; i < n; i += 2) printf "  hp %d\n", i * 7 + 1 }' > "$T/want.hp"
grep '^  hp ' "$T/sweep.1" > "$T/got.hp"
cmp -s "$T/want.hp" "$T/got.hp" || {
    bad "sweep: the survivors are not the entities that were spawned and not despawned"
    diff "$T/want.hp" "$T/got.hp" | sed 's/^/      /'
}
# The independent sum, so a survivor set that is right by coincidence of order
# still has to be right by content.
want_sum=$(awk -v n="$N" 'BEGIN { s = 0; for (i = 0; i < n; i += 2) s += i * 7 + 1; print s }')
ln_ "sweep: hp total $want_sum" "$T/sweep.1"
got_n=$(wc -l < "$T/got.hp" | tr -d ' ')
[ "$got_n" = "$want_live" ] || bad "sweep: $got_n survivors resolve through their ids but the pool reports $want_live live"

# ---------------------------------------------------------------------------
# [4] a stale id is refused, and the generation is what refuses it
#
# Against literals for the same reason as [2]. The middle assertion is the one
# that matters: without it, a slot that was never reused would produce a
# refusal too, and this leg would be gating an empty slot rather than a
# generation counter.
# ---------------------------------------------------------------------------
ln_ 'spawned 0#0'                                                  "$T/stale.1"
ln_ 'freed: id 0#0 names a despawned entity'                       "$T/stale.1"
ln_ 'respawned 0#1 into slot 0'                                    "$T/stale.1"
ln_ 'stale: id 0#0 is stale (slot 0 is at generation 1)'           "$T/stale.1"
ln_ 'fresh 0#1 hp=44'                                              "$T/stale.1"
grep -n 'BUG\|FLOOR' "$T/stale.1" | sed 's/^/      /' | grep . && \
    bad "stale: the program itself reported a BUG or a vacuous FLOOR"
grep -n 'BUG ' "$T/demo.1" "$T/sweep.1" | sed 's/^/      /' | grep . && \
    bad "a mode reported a BUG line"

# ---------------------------------------------------------------------------
# [5] every SimErr variant, exiting non-zero with its own whole message
#
# The driver REPORTS these and exits 0 by design. A caller that propagates one
# has to die by it, and this is that caller. world/ is COPIED into the temp dir
# -- nothing is written into the repo, and a renamed package reddens here.
# ---------------------------------------------------------------------------
P="$T/pkg"; mkdir -p "$P"
[ -d "$src/world" ] || bad "probe: $src/world is gone -- this leg asserts NOTHING"
cp -R "$src/world" "$P/" 2>/dev/null
cat > "$P/probe.ty" <<'EOF'
package main

import "world"

# One variant per run, named on the command line. Dead and Stale are reached
# through the API that owns them -- despawn, then despawn-and-respawn. BadSlot
# has no API path by construction: every id the API hands out names a slot that
# exists, so the only way to hold one that does not is to build it, and that is
# what this arm does.
fn main() -> Result(void, string):
    a := args()
    if len(a) < 2:
        return Err("usage: probe <variant>")
    w := world.new()
    if a[1] == "BadSlot":
        match world.get(w, world.Id(99, 0)):
            Ok(e): return Err("get ACCEPTED a slot the world does not have")
            Err(er): return Err(world.err_str(er))
    if a[1] == "Dead":
        id := world.spawn_at(&w, 1, 2, 30)
        match world.despawn(&w, id):
            Ok(): id = id
            Err(er): return Err("despawn refused a live id: " + world.err_str(er))
        match world.get(w, id):
            Ok(e): return Err("get RESOLVED a despawned id")
            Err(er): return Err(world.err_str(er))
    if a[1] == "Stale":
        id := world.spawn_at(&w, 1, 2, 30)
        match world.despawn(&w, id):
            Ok(): id = id
            Err(er): return Err("despawn refused a live id: " + world.err_str(er))
        b := world.spawn_at(&w, 3, 4, 40)
        if b.idx != id.idx:
            return Err("the slot was not reused -- this arm would test Dead, not Stale")
        match world.get(w, id):
            Ok(e): return Err("get RESOLVED an id whose slot was handed on")
            Err(er): return Err(world.err_str(er))
    return Err("unknown variant " + a[1])
EOF
if ! "$TYCHOC" "$P/probe.ty" -o "$T/probe" >"$T/probe.log" 2>&1; then
    bad "probe: tychoc could not build the SimErr probe"
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
        printf '=== err %s (via the world API)\n' "$_v" >> "$out"
        cat "$T/c.err" >> "$out"
    }
    errcase BadSlot 'no slot 99 (world has 0)'
    errcase Dead    'id 0#0 names a despawned entity'
    errcase Stale   'id 0#0 is stale (slot 0 is at generation 1)'
fi

# The coverage floor: the enum is READ, not remembered.
COVERED='BadSlot Stale Dead'
found=0
for v in $(awk '
        $0 == "enum SimErr:" { on = 1; next }
        on && /^[^ \t]/ { on = 0 }
        on && $1 ~ /^#/ { next }
        on && NF { v = $1; sub(/\(.*/, "", v); print v }
    ' "$src/world/world.ty"); do
    found=$((found + 1))
    hit=0
    for c in $COVERED; do [ "$v" = "$c" ] && hit=1; done
    [ "$hit" -eq 1 ] || bad "SimErr variant $v has no leg in this runner -- it is UNGATED"
done
[ "$found" -eq 3 ] || bad "found $found SimErr variant(s) in world.ty, expected 3 -- the scan is broken and [5]'s floor asserts nothing"

# ---------------------------------------------------------------------------
# the golden
# ---------------------------------------------------------------------------
if [ "$RECORD" = 1 ]; then
    if [ "$fail" -ne 0 ]; then echo "FAIL: refusing to RECORD from a failed run"; exit 1; fi
    cp "$out" "$golden"; echo "rec  tycho-sim"
fi
if [ ! -f "$golden" ]; then
    bad "no golden -- run RECORD=1 sh tools/tycho-sim/run.sh"
elif ! cmp -s "$out" "$golden"; then
    bad "transcript != golden"; diff "$golden" "$out" | sed 's/^/      /'
fi

if [ "$fail" -eq 0 ]; then
    echo "tycho-sim: green (demo, sweep=$N and stale each byte-identical over 2 runs and equal to the golden; $want_live of $N entities survive the sweep and every one of them resolves through its id to the hp it was spawned with, totalling $want_sum; a despawned id is refused as dead, its slot is reused with the generation moved, and the original id is then refused as stale; $found SimErr variants each exit non-zero with their own whole message and an empty stdout)"
else
    echo "tycho-sim: FAIL"; exit 1
fi
