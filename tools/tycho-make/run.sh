#!/bin/sh
# Gate for tycho-make, the dependency-graph half of the build tool in
# tools/tycho-make/ -- graph/ (the rulefile parser, the DAG, the topological
# order and the cycle namer) and main.ty (the driver that prints the report).
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
# WHAT IT DELIBERATELY DOES NOT ASSERT
#   Any timing, and any thread count. Slice 1 has no `spawn` and no
#   `parallel for` -- there is nothing to schedule yet -- so a TYCHO_THREADS
#   leg could not fail, and a leg that cannot fail is worse than no leg. The
#   grep below holds that claim to the source: when the scheduler lands, this
#   runner reddens and whoever added it owes the leg.
#   Staleness, mtimes and recipe execution. A recipe line is counted and never
#   run; slice 2 owns all three.
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
    $TO "$MAKE" "$_mk" > "$_f" 2> "$T/e.err"
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
    $TO "$MAKE" "$T/$_lbl.mk" > "$T/c.out" 2> "$T/c.err"
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
    $TO "$MAKE" "$T/bad.mk" > "$T/c.out" 2> "$T/c.err"
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

# The claim in the header, held to the source: slice 1 has nothing to schedule,
# so it must contain no concurrency, and this runner owes no thread leg.
sed 's/#.*//' "$src/main.ty" "$src/graph/graph.ty" | \
    grep -n 'parallel for\|\(^\|[^a-z_]\)spawn *(\|wait *(' > "$T/par.hits"
[ -s "$T/par.hits" ] && {
    bad "a concurrency construct landed in tycho-make -- this runner owes a TYCHO_THREADS leg it does not have"
    sed 's/^/      /' "$T/par.hits"
}

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
    echo "tycho-make: green (demo.mk's report byte-identical over 2 runs and equal to the golden; its 8 nodes come out in declaration order, zeta.o before alpha.o where alphabetical would disagree, and every one of the 7 printed edges is respected with each node listed exactly once; removing one edge from a 4-node chain moves the order, and with it in place bot is immediately followed by mid; 3 cycles are NAMED -- a 3-cycle, a self-edge, and one with two innocent nodes stuck behind it that the message does not mention; $found MakeErr variants each exit non-zero with their own whole message and an empty stdout)"
else
    echo "tycho-make: FAIL"; exit 1
fi
