set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "tycho-sim: no ./tychoc -- run 'make' first"; exit 2; }
TYCHOC="$PWD/tychoc"          # absolute: the probe in [8] is built after a cd
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
# [1] the four modes, each twice
# ---------------------------------------------------------------------------
N=24                              # the sweep's size; every number in [2] and [3] derives from it
T_TICKS=24                        # the sim's length; every number in [5] derives from it
SEED_N=8                          # tools/tycho-sim/main.ty@seed_n -- the initial population
PER_TICK=3                        # tools/tycho-sim/main.ty@per_tick -- reinforcements per tick
ORDER='spawn,move,combat,decay,reap'      # the canonical order, spelled out here for [7]
SWAPPED='spawn,combat,move,decay,reap'    # combat before move: it reads stale positions

simrun "demo run 1"  "$T/demo.1"  --demo
simrun "demo run 2"  "$T/demo.2"  --demo
simrun "sweep run 1" "$T/sweep.1" "--sweep=$N"
simrun "sweep run 2" "$T/sweep.2" "--sweep=$N"
simrun "stale run 1" "$T/stale.1" --stale
simrun "stale run 2" "$T/stale.2" --stale
simrun "sim run 1"   "$T/sim.1"   "--sim=$T_TICKS"
simrun "sim run 2"   "$T/sim.2"   "--sim=$T_TICKS"

for m in demo sweep stale sim; do
    cmp -s "$T/$m.1" "$T/$m.2" || {
        bad "the $m transcript is not deterministic (run 1 vs run 2)"
        diff "$T/$m.1" "$T/$m.2" | sed 's/^/      /'
    }
done

cat "$T/demo.1" "$T/sweep.1" "$T/stale.1" "$T/sim.1" >> "$out"

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
# [5] conservation, computed here
#
# `fld <name> <file>` reads one field off the sim's summary line. Every number
# it is compared against below is arithmetic on the four literals above.
# ---------------------------------------------------------------------------
fld() {
    sed -n "s/^sim:.* $1=\(-\{0,1\}[0-9]\{1,\}\).*/\1/p" "$2"
}

want_spawned=$(( SEED_N + PER_TICK * T_TICKS ))
got_spawned=$(fld spawned "$T/sim.1")
got_reaped=$(fld reaped "$T/sim.1")
got_live=$(fld live "$T/sim.1")
if [ -z "$got_spawned" ] || [ -z "$got_reaped" ] || [ -z "$got_live" ]; then
    bad "sim: no summary line -- [5] asserts NOTHING"
else
    [ "$got_spawned" = "$want_spawned" ] || \
        bad "sim: $got_spawned entities spawned, but $SEED_N seeded + $PER_TICK per tick over $T_TICKS ticks is $want_spawned"
    [ "$(( got_reaped + got_live ))" = "$got_spawned" ] || \
        bad "sim: $got_spawned spawned but $got_reaped reaped + $got_live live = $(( got_reaped + got_live )) -- entities were lost or invented"
    # The floors: a run where nothing died, or where nothing survived, would
    # satisfy the identity above while asserting nothing about either half.
    [ "$got_reaped" -gt 0 ] || bad "sim: nothing was ever reaped -- the conservation identity is vacuous"
    [ "$got_live" -gt 0 ] || bad "sim: the world emptied -- the conservation identity is vacuous"
fi
ln_ "sim seed: spawned=$SEED_N live=$SEED_N" "$T/sim.1"

# The same identity per tick, walked from the seeded population. The whole-run
# sum above would balance even if one tick lost an entity and the next invented
# one; this is the leg that localises it.
awk -v start="$SEED_N" -v ticks="$T_TICKS" '
    BEGIN { prev = start }
    /^tick / {
        sp = 0; rp = 0; lv = 0
        for (i = 1; i <= NF; i++) {
            split($i, kv, "=")
            if (kv[1] == "spawned") sp = kv[2] + 0
            else if (kv[1] == "reaped") rp = kv[2] + 0
            else if (kv[1] == "live") lv = kv[2] + 0
        }
        want = prev + sp - rp
        if (lv != want)
            printf "tick %s: live=%d, but %d live + %d spawned - %d reaped = %d\n", $2, lv, prev, sp, rp, want
        prev = lv
        n++
    }
    END { if (n != ticks) printf "found %d tick lines, expected %d\n", n, ticks }
' "$T/sim.1" > "$T/cons.bad"
[ -s "$T/cons.bad" ] && { bad "sim: entities are not conserved tick to tick"; sed 's/^/      /' "$T/cons.bad"; }

sysoff() {
    _s=$1; _c=$2; _want=$3
    simrun "sim --off=$_s" "$T/off.$_s" "--sim=$T_TICKS" "--off=$_s"
    _on=$(fld "$_c" "$T/sim.1")
    _off=$(fld "$_c" "$T/off.$_s")
    if [ -z "$_on" ] || [ -z "$_off" ]; then
        bad "--off=$_s: no $_c on a summary line -- this leg asserts NOTHING"
        return 0
    fi
    [ "$_on" -gt "$_want" ] || bad "$_c=$_on with every system ON, floor is $_want -- the '$_s' system is not running at all"
    [ "$_off" = "$_want" ] || bad "$_c=$_off with --off=$_s, expected $_want -- withholding the '$_s' system did not stop it"
    cmp -s "$T/sim.1" "$T/off.$_s" && bad "--off=$_s changed nothing in the transcript"
}

sysoff spawn  spawned "$SEED_N"
sysoff move   moved   0
sysoff combat hits    0
sysoff decay  decayed 0
sysoff reap   reaped  0

# The consequences, each a column the system in question does not itself count.
[ "$(fld live "$T/off.combat")" -gt "$got_live" ] || \
    bad "--off=combat: no more entities survive without combat than with it -- combat counts hits without dealing damage"
[ "$(fld hp "$T/off.decay")" -gt "$(fld hp "$T/sim.1")" ] || \
    bad "--off=decay: total hp is no higher without decay than with it -- decay counts entities without draining them"
[ "$(fld live "$T/off.reap")" = "$want_spawned" ] || \
    bad "--off=reap: $(fld live "$T/off.reap") live, expected every one of the $want_spawned ever spawned to still be in the pool"

# The claim in the header, held to the source. Comments are stripped first, and
# `spawn` must be a call in its own right: `despawn(`, `sys_spawn(` and
# `world.spawn_at(` are the pool's own vocabulary, not the thread keyword.
cat "$src/main.ty" "$src/sys/sys.ty" "$src/world/world.ty" | sed 's/#.*//' | \
    grep -n 'parallel for\|\(^\|[^a-z_]\)spawn *(' > "$T/par.hits"
[ -s "$T/par.hits" ] && {
    bad "a parallel construct landed in tycho-sim -- this runner owes a TYCHO_THREADS leg it does not have"
    sed 's/^/      /' "$T/par.hits"
}

# ---------------------------------------------------------------------------
# [7] the tick order is fixed, and this is the order that produced the golden
# ---------------------------------------------------------------------------
simrun "sim --order=canonical" "$T/ord.same" "--sim=$T_TICKS" "--order=$ORDER"
simrun "sim --order=swapped"   "$T/ord.diff" "--sim=$T_TICKS" "--order=$SWAPPED"
cmp -s "$T/sim.1" "$T/ord.same" || {
    bad "the goldened transcript was NOT produced by the order '$ORDER'"
    diff "$T/sim.1" "$T/ord.same" | sed 's/^/      /' | head -8
}
cmp -s "$T/sim.1" "$T/ord.diff" && \
    bad "reordering the systems to '$SWAPPED' changed nothing -- --order is being ignored, so the leg above is vacuous"
ln_ "=== sim: $T_TICKS ticks, order $ORDER" "$T/sim.1"

$TO "$SIM" "--sim=$T_TICKS" --order=spawn,mvoe,combat,decay,reap > "$T/o.out" 2> "$T/o.err"
[ $? -eq 0 ] && bad "--order accepted a system name that does not exist"
grep -qxF "no system named 'mvoe'" "$T/o.err" || \
    { bad "--order: a bad system name did not fail by name"; sed 's/^/      /' "$T/o.err"; }

# ---------------------------------------------------------------------------
# [8] every SimErr variant, exiting non-zero with its own whole message
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
[ "$found" -eq 3 ] || bad "found $found SimErr variant(s) in world.ty, expected 3 -- the scan is broken and [8]'s floor asserts nothing"

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
    echo "tycho-sim: green (demo, sweep=$N, stale and sim=$T_TICKS each byte-identical over 2 runs and equal to the golden; $want_live of $N entities survive the sweep and every one of them resolves through its id to the hp it was spawned with, totalling $want_sum; over $T_TICKS ticks $want_spawned entities are spawned and $got_reaped + $got_live = $want_spawned are reaped or still live, tick by tick as well as in total; each of the 5 systems takes its own counter to its floor when withheld and moves a column it does not count; the transcript is reproduced by the order '$ORDER' and not by '$SWAPPED'; a despawned id is refused as dead, its slot is reused with the generation moved, and the original id is then refused as stale; $found SimErr variants each exit non-zero with their own whole message and an empty stdout)"
else
    echo "tycho-sim: FAIL"; exit 1
fi
