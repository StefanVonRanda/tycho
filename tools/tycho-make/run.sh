#!/bin/sh
# Gate for tycho-make, the build tool in tools/tycho-make/ -- graph/ (the
# rulefile parser, the DAG, the topological order and the cycle namer), build/
# (staleness, the work-queue executor and the log) and main.ty (the driver:
# `--graph` prints the report, no flag builds).
#
# Re-record the golden with:  RECORD=1 sh tools/tycho-make/run.sh
#
# WHY THIS IS NOT A GOLDEN LANE WITH EXTRA STEPS. The subject is a TOPOLOGICAL
# ORDER, and a recorded transcript is the one instrument that cannot see the bug
# it exists to catch. Drop an edge in the parser and the order is still an
# order: the same nodes, each once, in a sequence that looks entirely plausible
# and that a golden re-recorded from that build agrees with byte for byte. The
# only thing that moved is a constraint nobody printed. So the order is checked
# HERE, three ways RECORD=1 cannot reach: against exact literals, against the
# edges the program itself printed, and against a second rulefile that differs
# from the first by exactly one edge.
#
# WHAT IT ASSERTS
#   [1] THE REPORT, twice. demo.mk is run twice and the two runs must be
#       cmp-identical to each other and the first to the golden. The program
#       reads no clock, no environment and prints no path, so a difference
#       between two runs is uninitialised state.
#   [2] THE ORDER OF THE DIAMOND, EXACT, AND THE TIE-BREAK BY NAME. demo.mk's
#       order is a literal below. It is not enough that it reproduces: two
#       nodes are ready at once in the middle of it, and the tie is broken by
#       DECLARATION ORDER (tools/tycho-make/graph/graph.ty@Kahn). demo.mk is
#       built so the two candidate rules disagree -- zeta.o is declared before
#       alpha.o but sorts after it -- so this leg reddens if the tie-break is
#       ever quietly changed to alphabetical, which an order that merely
#       reproduced would not.
#   [3] THE ORDER IS A TOPOLOGICAL ORDER OF THE EDGES THE PROGRAM PRINTED.
#       Computed here, from the report's own `node ... deps=[...]` lines: every
#       dependency must appear strictly before every node that names it. This
#       is the leg that does not depend on any literal, so it survives a change
#       to demo.mk, and it is what catches an order that is internally
#       consistent but wrong. It cannot see a DROPPED edge -- an edge missing
#       from the graph is missing from these lines too -- which is why [4]
#       exists and why the two are not the same leg.
#   [4] AN EDGE IS LOAD-BEARING. Two rulefiles written here differ by exactly
#       one edge: chain.mk is top->mid->bot->leaf, dropped.mk is the same four
#       nodes with `mid: bot` removed. Both orders are literals, they must
#       DIFFER, and in the intact one `bot` must be immediately followed by
#       `mid`. Without the second file this leg would pass on a parser that
#       ignored dependencies entirely.
#   [5] A CYCLE IS NAMED, NOT MERELY DETECTED, and never hangs. Three shapes:
#       a three-node cycle, a self-edge (`a -> a`), and a cycle with an
#       innocent node stuck behind it. The third is the one that matters --
#       `app` depends on the cycle but is not in it, so a namer that printed
#       "everything still unfinished" would redden here and nowhere else. Every
#       run in this file is bounded by $TO: a cycle detector that recursed
#       forever is exactly what this leg exists to catch, and a gate that hung
#       would report nothing.
#   [6] A SOURCE AND A SINK EACH APPEAR EXACTLY ONCE. common.h has no
#       dependencies and is depended on by two nodes; app is depended on by
#       nothing. Both must appear exactly once in the order, and the order must
#       be a permutation of the node list -- no name repeated, none missing.
#       Emitting a shared dependency twice is the classic Kahn bug and it
#       leaves every other line of the report correct.
#   [7] EVERY MakeErr VARIANT exits NON-ZERO with its own whole message and an
#       EMPTY STDOUT. The driver propagates rather than reporting, so no probe
#       is needed -- it dies by them itself. The variant list is READ out of the
#       enum, so a variant added tomorrow reddens here instead of arriving
#       ungated.
#
#   [8] A COLD BUILD RUNS EVERY RULE, IN AN ORDER THAT RESPECTS THE DAG. Two
#       instruments, because the log cannot be one of them: build.ty reassembles
#       it into topological order, so reading the DAG off it is circular. The
#       real order is `trace`, which every recipe appends its own name to. Only
#       PAIRS are asserted there -- three rules sit at one depth and race, and
#       pinning their order between themselves would pin the race.
#   [8b] A NODE STARTS WHEN ITS OWN DEPS FINISH, NOT WHEN ITS LEVEL DOES. The
#       one leg that separates the work queue from the wavefront it replaced,
#       and invisible to the golden: both designs print the same reassembled
#       log. race.mk sits a 3-node instant chain beside 3 one-second sleepers
#       at the same depth, and `start c2` must precede the first `end w`. The
#       wavefront at 027bc1d8 put it after all three; measured 2026-08-13.
#   [9] A NO-OP REBUILD RUNS ZERO RULES AND SAYS SO, and `trace` stays empty.
#   [10] TOUCHING ONE INPUT REBUILDS EXACTLY ITS DEPENDENTS. alpha.c's content
#       moves; alpha.o and app must run and zeta.o and docs must not. The
#       expected set is a literal, so RECORD=1 cannot widen it.
#   [11] CONTENT-HASH STALENESS IS NOT MTIME STALENESS. common.h's mtime is
#       moved to a fixed future stamp with its bytes untouched. make(1) would
#       rebuild three rules; this must rebuild NONE and must count the file as
#       `touched`, which is what says the move was seen and then dismissed on
#       content rather than never seen. This is the only leg that can tell a
#       real hash from a stat.
#   [12] THE LOG IS BYTE-IDENTICAL at TYCHO_THREADS 1, 2 and 8, and over two
#       runs at each. What is compared is a SEQUENCE -- cold, no-op, one input
#       changed -- not a cold build: on a cold build every outcome is the same
#       shape, so filing them under the wrong nodes yields the same bytes and
#       the leg cannot see the bug it exists for. Measured 2026-08-13: filing by
#       arrival position instead of node index reddens the sequence and does not
#       redden a cold-only comparison. [8]'s trace is what makes this worth
#       running at all: the pool really does finish out of source order.
#   [13] EVERY BuildErr VARIANT exits non-zero with its own whole message and an
#       empty stdout, the list read out of the enum. WorkLost is the one
#       variant no rulefile reaches -- it guards the reassembly itself -- and is
#       pinned to a single construction site instead.
#
# WHAT IT DELIBERATELY DOES NOT ASSERT
#   Any timing, and any wall-clock duration. Whether the pool is FASTER is a
#   measurement, and a gate asserting a measurement is a coin toss; that the
#   pool is real is asserted structurally instead, by `trace` coming out in an
#   order the source does not have.
#
# NO HOST DETAIL REACHES THE GOLDEN -- the program prints no path and reads no
# environment, and every fixture is written into a private mktemp -d.
set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "tycho-make: no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
RECORD="${RECORD:-0}"
golden="$PWD/tools/tycho-make/expected.out"
src="$PWD/tools/tycho-make"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT INT TERM
fail=0
bad() { echo "FAIL: $*"; fail=1; }

if command -v timeout >/dev/null 2>&1; then TO="timeout 60"
elif command -v gtimeout >/dev/null 2>&1; then TO="gtimeout 60"
else TO=""; fi

MAKE="$T/tycho-make"
if ! "$TYCHOC" "$src/main.ty" -o "$MAKE" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/build.log"
    echo "tycho-make: FAIL"; exit 1
fi

out="$T/all.out"
: > "$out"

# `ok <label> <outfile> <rulefile>` -- one bounded run that must succeed with a
# silent stderr. A run that dies or warns is a failure whatever its stdout says.
ok() {
    _lbl=$1; _f=$2; _mk=$3
    $TO "$MAKE" --graph "$_mk" > "$_f" 2> "$T/e.err"
    _rc=$?
    [ "$_rc" -eq 0 ] || { bad "$_lbl: exited $_rc, expected 0"; sed 's/^/      /' "$T/e.err"; }
    [ -s "$T/e.err" ] && { bad "$_lbl: wrote to stderr"; sed 's/^/      /' "$T/e.err"; }
    return 0
}

ln_() {
    grep -qxF "$1" "$2" || {
        bad "expected line missing from $(basename "$2") -- '$1'"
        grep -h '^order:\|^sources:\|^sinks:\|^=== ' "$2" | sed 's/^/      got: /'
    }
}

# ---------------------------------------------------------------------------
# [1] the report, twice
# ---------------------------------------------------------------------------
ok "demo run 1" "$T/demo.1" "$src/demo.mk"
ok "demo run 2" "$T/demo.2" "$src/demo.mk"
cmp -s "$T/demo.1" "$T/demo.2" || {
    bad "the demo report is not deterministic (run 1 vs run 2)"
    diff "$T/demo.1" "$T/demo.2" | sed 's/^/      /'
}
cat "$T/demo.1" >> "$out"

# ---------------------------------------------------------------------------
# [2] the diamond's order, exact, and the tie-break spelled out
#
# zeta.o and alpha.o are both ready once common.h is emitted. Declaration order
# puts zeta.o first; alphabetical order would put alpha.o first. Asserting the
# whole line pins the answer, and the two greps below pin WHICH RULE produced it
# -- an order that merely reproduced would satisfy neither.
# ---------------------------------------------------------------------------
DEMO_ORDER='order: zeta.c common.h zeta.o alpha.c alpha.o app README.md docs'
ln_ "$DEMO_ORDER" "$T/demo.1"
ln_ 'sources: zeta.c common.h alpha.c README.md' "$T/demo.1"
ln_ 'sinks: app docs' "$T/demo.1"
ln_ '=== graph: 8 nodes, 7 edges, 4 rules, 4 recipe lines' "$T/demo.1"
ord_line=$(sed -n 's/^order: //p' "$T/demo.1")
case " $ord_line " in
    *" zeta.o "*" alpha.o "*) : ;;
    *) bad "the tie between zeta.o and alpha.o did not break by DECLARATION order -- got: $ord_line" ;;
esac
# The floor: if the two names ever stop disagreeing, the leg above is vacuous.
case "$(printf '%s\n%s\n' alpha.o zeta.o | LC_ALL=C sort | head -1)" in
    alpha.o) : ;;
    *) bad "demo.mk no longer distinguishes declaration order from alphabetical -- [2] asserts nothing" ;;
esac

# ---------------------------------------------------------------------------
# [3] the order is topological with respect to the printed edges
#
# No literal here: the edges are read off the report's own `node` lines, so this
# leg still means something after demo.mk changes. It cannot see a dropped edge
# -- that is [4]'s job.
# ---------------------------------------------------------------------------
awk '
    /^node /  { name = $2
                line = $0
                sub(/^.*deps=\[/, "", line); sub(/\].*$/, "", line)
                deps[name] = line; nodes[++nn] = name }
    /^order: /{ o = $0; sub(/^order: /, "", o); n = split(o, seq, " ")
                for (i = 1; i <= n; i++) { if (seq[i] in at) dup[seq[i]] = 1; at[seq[i]] = i }
                got = n }
    END {
        for (i = 1; i <= nn; i++) {
            v = nodes[i]
            if (!(v in at)) { printf "node %s is missing from the order\n", v; continue }
            if (v in dup)   printf "node %s appears more than once in the order\n", v
            m = split(deps[v], ds, " ")
            for (j = 1; j <= m; j++)
                if (at[ds[j]] >= at[v])
                    printf "%s depends on %s but is ordered at %d, before %s at %d\n", v, ds[j], at[v], ds[j], at[ds[j]]
        }
        if (got != nn) printf "the order lists %d names but the graph has %d nodes\n", got, nn
        if (nn == 0)   print "no node lines at all -- this leg asserts NOTHING"
    }
' "$T/demo.1" > "$T/topo.bad"
[ -s "$T/topo.bad" ] && { bad "the order is not a topological order of the edges the program printed"; sed 's/^/      /' "$T/topo.bad"; }

# ---------------------------------------------------------------------------
# [4] an edge is load-bearing
#
# Two rulefiles, one edge apart. Both orders are literals; they must differ, and
# the intact chain must keep bot immediately before mid. A parser that dropped
# `mid: bot` produces dropped.mk's answer from chain.mk's text.
# ---------------------------------------------------------------------------
cat > "$T/chain.mk" <<'EOF'
top: mid
mid: bot
bot: leaf
EOF
cat > "$T/dropped.mk" <<'EOF'
top: mid
mid:
bot: leaf
EOF
ok "chain"   "$T/chain.out"   "$T/chain.mk"
ok "dropped" "$T/dropped.out" "$T/dropped.mk"
ln_ 'order: leaf bot mid top' "$T/chain.out"
ln_ 'order: mid top leaf bot' "$T/dropped.out"
grep -q '^order: .*bot mid' "$T/chain.out" || \
    bad "chain.mk: 'bot' is not immediately followed by 'mid' -- the edge mid->bot is not constraining the order"
if cmp -s "$T/chain.out" "$T/dropped.out"; then
    bad "removing the edge 'mid: bot' changed NOTHING -- dependencies are not reaching the order, so [2] and [3] are vacuous"
fi
cat "$T/chain.out" "$T/dropped.out" >> "$out"

# ---------------------------------------------------------------------------
# [5] a cycle is named, and the naming is not "everything left over"
#
# `cycrun <label> <whole stderr line>` -- must exit non-zero, say exactly that,
# and print nothing at all on stdout.
# ---------------------------------------------------------------------------
cycrun() {
    _lbl=$1; _msg=$2
    $TO "$MAKE" --graph "$T/$_lbl.mk" > "$T/c.out" 2> "$T/c.err"
    _rc=$?
    if [ "$_rc" -eq 0 ]; then
        bad "$_lbl: EXITED 0 -- a cycle was ordered rather than refused"
    elif ! grep -qxF "$_msg" "$T/c.err"; then
        bad "$_lbl: refused, but not by naming the cycle"; sed 's/^/      /' "$T/c.err"
    fi
    [ -s "$T/c.out" ] && bad "$_lbl: wrote to STDOUT"
    printf '=== cycle %s\n' "$_lbl" >> "$out"
    cat "$T/c.err" >> "$out"
}

printf 'a: c\nb: a\nc: b\n'                        > "$T/cyc3.mk"
printf 'a: a\n'                                    > "$T/self.mk"
printf 'app: lib\nlib: core\ncore: lib\nutil: s.c\n' > "$T/behind.mk"
cycrun cyc3   'dependency cycle: a -> c -> b -> a'
cycrun self   'dependency cycle: a -> a'
cycrun behind 'dependency cycle: lib -> core -> lib'
# The leg that makes `behind` worth running: app and util are unfinished too, and
# neither is in the cycle. A namer that dumped the remaining set would fail this.
grep -q 'app\|util\|s\.c' "$T/c.err" && \
    bad "behind: the reported cycle names a node that is not in the cycle -- it is reporting the unfinished SET, not the loop"

# ---------------------------------------------------------------------------
# [6] a source and a sink each appear exactly once
#
# common.h has no dependencies and two dependents; app has no dependents. The
# whole-permutation check is in [3]; these two are named separately because a
# double-emit of a shared dependency is the specific Kahn bug, and the failure
# should say which node it was.
# ---------------------------------------------------------------------------
for name in common.h app; do
    n=$(printf '%s\n' $ord_line | grep -cxF "$name")
    [ "$n" = 1 ] || bad "'$name' appears $n time(s) in the order, expected exactly 1"
done

# ---------------------------------------------------------------------------
# [7] every MakeErr variant, exiting non-zero with its own whole message
# ---------------------------------------------------------------------------
# `errcase <variant> <rulefile text> <the whole message it must die with>`
errcase() {
    _v=$1; _text=$2; _msg=$3
    printf '%s' "$_text" > "$T/bad.mk"
    $TO "$MAKE" --graph "$T/bad.mk" > "$T/c.out" 2> "$T/c.err"
    _rc=$?
    if [ "$_rc" -eq 0 ]; then
        bad "$_v: EXITED 0 -- the parser accepted what the variant exists to refuse"
    elif ! grep -qxF "$_msg" "$T/c.err"; then
        bad "$_v: failed but not with its own whole message"; sed 's/^/      /' "$T/c.err"
    fi
    [ -s "$T/c.out" ] && bad "$_v: wrote to STDOUT"
    printf '=== err %s\n' "$_v" >> "$out"
    cat "$T/c.err" >> "$out"
}

errcase NoColon      'app zeta
'   'line 1: not a rule and not a recipe -- no '"'"':'"'"' in [app zeta]'
errcase EmptyTarget  ': a b
'   "line 1: a rule with no target -- nothing before the ':'"
errcase MultiTarget  'a b: c
'   'line 1: [a b] declares more than one target -- one target per rule'
errcase DupTarget    'app: a
app: b
'   "line 2: target 'app' is declared again (first at line 1)"
errcase OrphanRecipe '	cc -o x
app: a
'   'line 1: an indented recipe line before any rule'
errcase ColonInDep   'app: b:c
'   "line 1: dependency 'b:c' contains a ':'"
errcase Continuation 'app: a \
'   'line 1: a trailing backslash -- line continuations are not supported'
errcase Cycle        'a: b
b: a
'   'dependency cycle: a -> b -> a'

# The coverage floor: the enum is READ, not remembered.
COVERED='NoColon EmptyTarget MultiTarget DupTarget OrphanRecipe ColonInDep Continuation Cycle'
found=0
for v in $(awk '
        $0 == "enum MakeErr:" { on = 1; next }
        on && /^[^ \t]/ { on = 0 }
        on && $1 ~ /^#/ { next }
        on && NF { v = $1; sub(/\(.*/, "", v); print v }
    ' "$src/graph/graph.ty"); do
    found=$((found + 1))
    hit=0
    for c in $COVERED; do [ "$v" = "$c" ] && hit=1; done
    [ "$hit" -eq 1 ] || bad "MakeErr variant $v has no leg in this runner -- it is UNGATED"
done
[ "$found" -eq 8 ] || bad "found $found MakeErr variant(s) in graph.ty, expected 8 -- the scan is broken and [7]'s floor asserts nothing"

# The floor under [12]: the scheduler must actually BE concurrent. A `parallel
# for` that was quietly turned into a `for` would make every determinism leg
# below pass by being sequential, which is the one way they can go vacuous.
grep -q 'parallel for' "$src/build/build.ty" || \
    bad "build.ty has no 'parallel for' -- nothing is scheduled, so [12] asserts NOTHING"
# And the pool must be a pool: one spawned task pulling from a shared jobs
# channel, not a fan-out per level. Without the spawn the coordinator and the
# workers are the same thread and [8b] could only ever pass by luck.
grep -q 'spawn pool(' "$src/build/build.ty" || \
    bad "build.ty no longer spawns the worker pool -- the coordinator cannot run concurrently with it, so [8b] asserts NOTHING"

# ---------------------------------------------------------------------------
# THE EXECUTOR. Everything from here down builds for real, in a private work
# directory: the recipes in build.mk write files, so the runner cds into $W and
# the program is handed relative names only. No path reaches the golden.
#
# `bld <label> <logfile> [env...]` -- one bounded build that must exit 0 with a
# silent stderr.
# ---------------------------------------------------------------------------
W="$T/w"
mkdir -p "$W"
cp "$src/build.mk" "$W/build.mk"

# The tree the demo builds from. Written here rather than committed: they are
# inputs whose CONTENT this gate edits, and an edited fixture in the repo would
# be a dirty tree after a run.
seed() {
    rm -f "$W/app" "$W/zeta.o" "$W/alpha.o" "$W/docs" "$W/.tycho-make.stamp" "$W/trace"
    printf 'common\n'  > "$W/common.h"
    printf 'zeta\n'    > "$W/zeta.c"
    printf 'alpha\n'   > "$W/alpha.c"
    printf 'abc doc\n' > "$W/README.md"
    : > "$W/trace"
}

bld() {
    _lbl=$1; _f=$2; shift 2
    ( cd "$W" && env "$@" $TO ./tycho-make build.mk ) > "$_f" 2> "$T/b.err"
    _rc=$?
    [ "$_rc" -eq 0 ] || { bad "$_lbl: exited $_rc, expected 0"; sed 's/^/      /' "$T/b.err"; }
    [ -s "$T/b.err" ] && { bad "$_lbl: wrote to stderr"; sed 's/^/      /' "$T/b.err"; }
    return 0
}
cp "$MAKE" "$W/tycho-make"

# `ranset <label> <file> <space-separated names, in DAG order>` -- the set of
# rules that RAN must be exactly this. The expected set is a literal here, which
# is the whole point: RECORD=1 rewrites the golden and cannot touch this line.
ranset() {
    _lbl=$1; _f=$2; _want=$3
    _got=$(sed -n 's/^run \([^ ]*\) .*/\1/p' "$_f" | tr '\n' ' ' | sed 's/ $//')
    [ "$_got" = "$_want" ] || bad "$_lbl: ran [$_got], expected exactly [$_want]"
}

# ---------------------------------------------------------------------------
# [8] A COLD BUILD RUNS EVERY RULE, AND THE ORDER IT REALLY RAN IN RESPECTS THE
#     DAG. Two different claims, and they need two instruments.
#
#     The log is REASSEMBLED into topological order by build.ty, so reading the
#     DAG off the log would be circular -- it is ordered by construction. The
#     real order is in `trace`, which each recipe appends its own name to as it
#     runs. Specific PAIRS are asserted there, not the whole list: zeta.o and
#     alpha.o and docs are at one depth and their order between themselves is a
#     race, which is exactly what must NOT be pinned.
# ---------------------------------------------------------------------------
seed
bld "cold" "$T/cold.log"
ranset "cold" "$T/cold.log" 'zeta.o alpha.o app docs'
ln_ 'targets 4: 4 run, 0 up to date' "$T/cold.log"
ln_ 'sources 4: 4 new, 0 changed, 0 touched, 0 unchanged' "$T/cold.log"
ln_ 'group all' "$T/cold.log"
ln_ '| linking app' "$T/cold.log"
# `before A B` -- A must appear before B in the REAL execution order.
before() {
    _a=$(grep -n "^$1\$" "$W/trace" | head -1 | cut -d: -f1)
    _b=$(grep -n "^$2\$" "$W/trace" | head -1 | cut -d: -f1)
    if [ -z "$_a" ] || [ -z "$_b" ]; then
        bad "trace: '$1' or '$2' never ran -- $(tr '\n' ' ' < "$W/trace")"
    elif [ "$_a" -ge "$_b" ]; then
        bad "trace: '$1' ran at $_a, after '$2' at $_b -- the executor ignored a dependency"
    fi
}
before zeta.o app
before alpha.o app
[ "$(wc -l < "$W/trace")" = 4 ] || bad "trace has $(wc -l < "$W/trace") lines, expected 4 recipes to have run"
# The floor under `before`: the two names must be at DIFFERENT depths, or the
# assertion is about a pair that could never have raced anyway.
grep -q '^app: zeta.o alpha.o$' "$W/build.mk" || \
    bad "build.mk no longer has app depending on zeta.o -- the trace pairs assert nothing"
printf '=== build cold\n' >> "$out"; cat "$T/cold.log" >> "$out"

# ---------------------------------------------------------------------------
# [8b] A NODE STARTS WHEN ITS OWN DEPENDENCIES FINISH, NOT WHEN ITS LEVEL DOES.
#      This is the one leg that separates a work queue from the wavefront that
#      preceded it, and it is asserted HERE rather than in the golden because a
#      transcript cannot see scheduling at all: both designs build the same
#      files and print the same reassembled log.
#
#      race.mk puts a 3-node chain (c1 -> c2 -> c3, each instant) beside a wide
#      level of 3 sleepers, all four at depth 1 behind one source. Under a
#      wavefront c2 is at depth 2 and CANNOT start until every depth-1 node has
#      finished, so `start c2` lands after all three `end w`. Under a work queue
#      c1's completion releases c2 immediately, a second or so before the first
#      sleeper wakes.
#
#      Measured against the wavefront build at 027bc1d8 on 2026-08-13: it put
#      `start c2` at trace line 9, after `end w1`/`end w3`/`end w2` at 6/7/8 --
#      this leg reddens on it, which is what makes it worth running.
#
#      TYCHO_THREADS=8 because the claim needs a worker free for the chain while
#      the sleepers hold theirs; at 2 the pool is legitimately saturated and the
#      chain waits, which is scheduling working, not failing.
# ---------------------------------------------------------------------------
R="$T/r"; mkdir -p "$R"
cp "$src/race.mk" "$R/race.mk"; cp "$MAKE" "$R/tycho-make"
printf 'b\n' > "$R/base"; : > "$R/rtrace"
( cd "$R" && env TYCHO_THREADS=8 $TO ./tycho-make race.mk ) > "$T/race.log" 2> "$T/race.err"
_rc=$?
[ "$_rc" -eq 0 ] || { bad "race: exited $_rc, expected 0"; sed 's/^/      /' "$T/race.err"; }
[ -s "$T/race.err" ] && { bad "race: wrote to stderr"; sed 's/^/      /' "$T/race.err"; }
# The floor: every node really ran, or the ordering claim below is about a trace
# with nothing in it.
[ "$(grep -c '^end ' "$R/rtrace")" = 6 ] || \
    bad "race: $(grep -c '^end ' "$R/rtrace") of 6 recipes finished -- [8b] asserts nothing"
_c2=$(grep -n '^start c2$' "$R/rtrace" | head -1 | cut -d: -f1)
_ew=$(grep -n '^end w' "$R/rtrace" | head -1 | cut -d: -f1)
if [ -z "$_c2" ] || [ -z "$_ew" ]; then
    bad "race: 'start c2' or an 'end w' never appeared -- $(tr '\n' ' ' < "$R/rtrace")"
elif [ "$_c2" -ge "$_ew" ]; then
    bad "race: c2 started at trace line $_c2, AFTER the wide level began finishing at $_ew -- a node is still waiting for its whole level, which is the wavefront this replaced"
    sed 's/^/      /' "$R/rtrace"
fi
# The floor under the fixture: the chain must be deeper than the wide level, or
# c2 was never behind it in the first place.
grep -q '^c2: c1$' "$R/race.mk" || bad "race.mk no longer has c2 behind c1 -- [8b] asserts nothing"
grep -q '^w1: base$' "$R/race.mk" || bad "race.mk no longer has w1 at the same depth as c1 -- [8b] asserts nothing"
printf '=== build race\n' >> "$out"; cat "$T/race.log" >> "$out"

# ---------------------------------------------------------------------------
# [9] A NO-OP REBUILD RUNS ZERO RULES AND SAYS SO. Nothing on disk moved, so
#     every target is up to date and `trace` must not grow.
# ---------------------------------------------------------------------------
: > "$W/trace"
bld "noop" "$T/noop.log"
ranset "noop" "$T/noop.log" ''
ln_ 'targets 4: 0 run, 4 up to date' "$T/noop.log"
ln_ 'nothing to do' "$T/noop.log"
[ -s "$W/trace" ] && bad "noop: a recipe ran -- trace: $(tr '\n' ' ' < "$W/trace")"
printf '=== build noop\n' >> "$out"; cat "$T/noop.log" >> "$out"

# ---------------------------------------------------------------------------
# [10] TOUCHING ONE INPUT REBUILDS EXACTLY ITS DEPENDENTS. alpha.c's CONTENT
#      changes. alpha.o depends on it and app depends on alpha.o, so both must
#      run; zeta.o shares common.h with alpha.o but not alpha.c, and docs is in
#      the other component, so neither may. The expected set is written out in
#      full, in DAG order, as a literal.
# ---------------------------------------------------------------------------
: > "$W/trace"
printf 'ALPHA2\n' > "$W/alpha.c"
bld "one-input" "$T/one.log"
ranset "one-input" "$T/one.log" 'alpha.o app'
ln_ 'src alpha.c changed' "$T/one.log"
ln_ 'skip zeta.o (up to date)' "$T/one.log"
ln_ 'skip docs (up to date)' "$T/one.log"
ln_ 'run app (alpha.o changed)' "$T/one.log"
ln_ 'targets 4: 2 run, 2 up to date' "$T/one.log"
[ "$(tr '\n' ' ' < "$W/trace")" = "alpha.o app " ] || \
    bad "one-input: trace is [$(tr '\n' ' ' < "$W/trace")], expected exactly alpha.o then app"
printf '=== build one-input\n' >> "$out"; cat "$T/one.log" >> "$out"

# ---------------------------------------------------------------------------
# [11] CONTENT-HASH STALENESS IS NOT MTIME STALENESS. This is the leg that
#      separates a real hash from a stat, and nothing else in this file can see
#      the difference.
#
#      common.h's mtime is moved to a fixed future stamp and its bytes are left
#      alone. `make(1)` would rebuild zeta.o, alpha.o and app. This must rebuild
#      NOTHING, and must say the word: `touched (content identical)`. A fixed
#      timestamp rather than a bare `touch` because a bare touch inside the same
#      second as the last build sets the same mtime, and then the leg would be
#      asserting that nothing happened for the wrong reason.
# ---------------------------------------------------------------------------
: > "$W/trace"
touch -t 203001010000 "$W/common.h"
bld "mtime-only" "$T/mt.log"
ranset "mtime-only" "$T/mt.log" ''
ln_ 'src common.h touched (content identical)' "$T/mt.log"
ln_ 'targets 4: 0 run, 4 up to date' "$T/mt.log"
# Exactly one source touched: the count is what says the move was NOTICED and
# then dismissed on content, rather than never seen at all.
ln_ 'sources 4: 0 new, 0 changed, 1 touched, 3 unchanged' "$T/mt.log"
[ -s "$W/trace" ] && bad "mtime-only: a recipe ran on an unchanged file -- staleness is mtime, not content"
# The floor: the mtime really did move. If it did not, the paragraph above is a
# story about a file nobody touched.
[ "$(date -r "$W/common.h" +%Y 2>/dev/null)" = 2030 ] || \
    bad "mtime-only: common.h's mtime is not 2030 -- touch(1) did not move it and [11] asserts nothing"
printf '=== build mtime-only\n' >> "$out"; cat "$T/mt.log" >> "$out"

# ---------------------------------------------------------------------------
# [12] THE LOG IS BYTE-IDENTICAL AT TYCHO_THREADS=1 AND 2, AND OVER TWO RUNS.
#      A level of this graph holds three rules that run at once and finish in
#      whatever order they finish in -- `trace` above proves they are not in
#      source order. The log is assembled by node index afterwards, so it must
#      not move. Cold every time, so all four runs do the maximum work.
# ---------------------------------------------------------------------------
#      A COLD build alone is not enough here and that is not a detail: on a cold
#      build every outcome is the same shape -- ran, reason `missing` -- so
#      filing them under the wrong nodes produces the same bytes and the leg
#      goes blind. Each run is therefore the whole SEQUENCE cold -> no-op ->
#      one-input-changed, where the outcomes differ per node and a misfiled one
#      shows up as the wrong verdict against the wrong name.
# `bld` assigns _lbl and _f, and a shell function has no locals -- hence the
# distinct names here.
seq_run() {
    _tag=$1; _dst=$2; _th=$3
    seed
    bld "$_tag cold" "$T/s1" TYCHO_THREADS="$_th"
    bld "$_tag noop" "$T/s2" TYCHO_THREADS="$_th"
    printf 'ALPHA2\n' > "$W/alpha.c"
    bld "$_tag one"  "$T/s3" TYCHO_THREADS="$_th"
    cat "$T/s1" "$T/s2" "$T/s3" > "$_dst"
}
for t in 1 2 8; do
    seq_run "threads=$t" "$T/th.$t" "$t"
    seq_run "threads=$t rerun" "$T/th.$t.b" "$t"
    cmp -s "$T/th.$t" "$T/th.$t.b" || {
        bad "the build log is not deterministic at TYCHO_THREADS=$t (two runs of the same sequence differ)"
        diff "$T/th.$t" "$T/th.$t.b" | sed 's/^/      /'
    }
done
cmp -s "$T/th.1" "$T/th.2" || {
    bad "the build log DEPENDS ON THE POOL WIDTH (TYCHO_THREADS=1 vs 2) -- the ordered reassembly is broken"
    diff "$T/th.1" "$T/th.2" | sed 's/^/      /'
}
cmp -s "$T/th.1" "$T/th.8" || {
    bad "the build log differs at TYCHO_THREADS=8"
    diff "$T/th.1" "$T/th.8" | sed 's/^/      /'
}
printf '=== build threads\n' >> "$out"; cat "$T/th.1" >> "$out"

# ---------------------------------------------------------------------------
# [13] EVERY BuildErr VARIANT exits NON-ZERO with its own whole message and an
#      EMPTY STDOUT. No probe is needed: the driver propagates, so the program
#      dies by them itself. The variant list is READ out of the enum.
# ---------------------------------------------------------------------------
# `berr <variant> <rulefile text> <the whole message>` -- run in a scratch tree
# so a failed build cannot leave the demo's work directory half-written.
berr() {
    _v=$1; _text=$2; _msg=$3
    rm -rf "$T/e"; mkdir -p "$T/e/d"
    printf 'a\n' > "$T/e/a"
    printf '%s' "$_text" > "$T/e/bad.mk"
    [ "$_v" = StampBroken ] && printf 'garbage\n' > "$T/e/.tycho-make.stamp"
    ( cd "$T/e" && $TO "$MAKE" bad.mk ) > "$T/c.out" 2> "$T/c.err"
    _rc=$?
    if [ "$_rc" -eq 0 ]; then
        bad "$_v: EXITED 0 -- the build accepted what the variant exists to refuse"
    elif ! grep -qxF "$_msg" "$T/c.err"; then
        bad "$_v: failed but not with its own whole message"; sed 's/^/      /' "$T/c.err"
    fi
    [ -s "$T/c.out" ] && bad "$_v: wrote to STDOUT"
    printf '=== berr %s\n' "$_v" >> "$out"
    cat "$T/c.err" >> "$out"
}

berr MissingSource 'x: nosuch.c
	cat nosuch.c > x
'   "no rule to make 'nosuch.c' and no such file"
berr RecipeFailed  'x: a
	false
'   "recipe for 'x' failed with exit 1: false"
berr NoOutput      'x: a
	true
'   "recipe for 'x' succeeded but did not create it: true"
berr Unreadable    'x: d
	cp a x
'   "cannot read 'd' to hash it"
berr StampBroken   'x: a
	cp a x
'   'stamp line 1 is not <name> <mtime> <hash>: [garbage]'

# WorkLost is the reassembly's own invariant and has no rulefile that reaches
# it: it fires when a node the graph contains never reported under its own
# index, which is a runtime bug, not an input. It is held the way
# tools/tycho-sheet/run.sh holds CellErr.NoText -- pinned to ONE construction
# site, and that site asserted to be the accounting guard in `build`.
n_ll=$(grep -c 'WorkLost(' "$src/build/build.ty")
[ "$n_ll" = 3 ] || bad "WorkLost appears $n_ll time(s) in build.ty, expected 3 (enum, err_str, the one guard)"
grep -q 'if filed != n or ran != sent:' "$src/build/build.ty" || \
    bad "build.ty no longer checks that every node reported and that the pool did every job it was sent -- WorkLost is now dead AND the reassembly is unguarded"

BCOVERED='MissingSource RecipeFailed NoOutput Unreadable StampBroken WorkLost'
bfound=0
for v in $(awk '
        $0 == "enum BuildErr:" { on = 1; next }
        on && /^[^ \t]/ { on = 0 }
        on && $1 ~ /^#/ { next }
        on && NF { v = $1; sub(/\(.*/, "", v); print v }
    ' "$src/build/build.ty"); do
    bfound=$((bfound + 1))
    hit=0
    for c in $BCOVERED; do [ "$v" = "$c" ] && hit=1; done
    [ "$hit" -eq 1 ] || bad "BuildErr variant $v has no leg in this runner -- it is UNGATED"
done
[ "$bfound" -eq 6 ] || bad "found $bfound BuildErr variant(s) in build.ty, expected 6 -- the scan is broken and [13]'s floor asserts nothing"

# ---------------------------------------------------------------------------
# the golden
# ---------------------------------------------------------------------------
if [ "$RECORD" = 1 ]; then
    if [ "$fail" -ne 0 ]; then echo "FAIL: refusing to RECORD from a failed run"; exit 1; fi
    cp "$out" "$golden"; echo "rec  tycho-make"
fi
if [ ! -f "$golden" ]; then
    bad "no golden -- run RECORD=1 sh tools/tycho-make/run.sh"
elif ! cmp -s "$out" "$golden"; then
    bad "transcript != golden"; diff "$golden" "$out" | sed 's/^/      /'
fi

if [ "$fail" -eq 0 ]; then
    echo "tycho-make: green (demo.mk's report byte-identical over 2 runs and equal to the golden; its 8 nodes come out in declaration order, zeta.o before alpha.o where alphabetical would disagree, and every one of the 7 printed edges is respected with each node listed exactly once; removing one edge from a 4-node chain moves the order, and with it in place bot is immediately followed by mid; 3 cycles are NAMED -- a 3-cycle, a self-edge, and one with two innocent nodes stuck behind it that the message does not mention; $found MakeErr variants each exit non-zero with their own whole message and an empty stdout; build.mk cold-builds all 4 rules with zeta.o and alpha.o really running before app, a no-op rebuild runs 0 and says 'nothing to do', changing alpha.c reruns exactly alpha.o and app, moving common.h's mtime with its bytes intact reruns NOTHING and reports it 'touched (content identical)', a chain node starts while the wide level beside it is still sleeping -- which the wavefront this replaced could not do -- the log is byte-identical over 6 cold runs at TYCHO_THREADS 1, 2 and 8 although the recipes finish out of source order, and $bfound BuildErr variants are accounted for)"
else
    echo "tycho-make: FAIL"; exit 1
fi
