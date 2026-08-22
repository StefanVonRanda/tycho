#!/bin/sh
# The Phase 3 parse sweep, as a runnable lane. `make parse-check`.
#
# Four legs. The first three are counts; the fourth is the one that carries the
# wrong-tree class, and it is the reason this lane is not two `wc -l`s:
#
#   [1]  every tests/*.ty that ./tychoc accepts must parse       274/274
#   [1b] every corelib/**.ty must parse                           91/91
#   [2]  tests/reject/, SPLIT by the committed classifier table:
#        a SYNTAX rejection must be rejected, a SEMANTIC one must be ACCEPTED.
#        "everything in tests/reject/ was rejected" is the failure this leg
#        exists to catch -- 290 of the 337 are type errors a parser cannot see.
#   [1c] every tools/ examples/ server/ bench/ .ty must parse            179/179
#   [3]  the AST node-kind census, compared to a recorded golden. An accept/
#        reject verdict is blind to a parse that SUCCEEDS with the wrong tree:
#        dropping the ast.Named wrapper from a `name: value` call argument left
#        legs 1 and 2 fully green on all 611 files and only moved the census.
#   [4]  the declaration rules that NO fixture in tests/reject/ covers, each
#        written here as a whole program. Every refusal is paired with an
#        accepting twin one token away -- a leg that only ever refuses is
#        satisfied by a parser that refuses everything.
#   [5]  every .ty in the tree, 1078 of them, scored against ./tychoc's own
#        verdict -- the only leg that reaches tests/conc, tests/diag and
#        tests/reject/pkg, where three of Phase 4's four defects were.
#   [6]  the same 1078 files scored a SECOND time, against `--resolve`: the
#        SYNTAX and NAME files must be refused, everything else accepted, and a
#        file ./tychoc accepts must resolve with no unused local and no unused
#        import. Phase 5 split NAME out of SEMANTIC (27 of the 337 fixtures,
#        49 of the 1078 files) -- printed by compiler/verdict_diff.py.
#   [2b] the reject corpus again, under `--resolve` rather than `--parse`.
#   [7]  the RESOLUTION census, the leg the two verdicts cannot carry: a name
#        that resolves to the WRONG declaration is accepted either way. Every
#        use is printed as the package-mangled name it resolved to, so
#        preferring a top-level const over a local -- which leaves legs 1..6
#        fully green -- moves a counted line.
#
# The split in compiler/reject_class.tsv is COMMITTED, not recomputed: it is
# grounded in src/tychoc.c's diagnostic sites (see scripts/classify_rejects.py),
# costs a full ./tychoc run over 337 fixtures to rebuild, and is the half of
# this evidence that is expensive and auditable rather than cheap and derived.
#
# RECORD=1 re-records the census golden. It cannot bless a lost count: legs 1,
# 1b and 2 compare against literals below.
cd "$(dirname "$0")/.." || exit 1

TYCHOC1="${TYCHOC1:-./tychoc1}"
TSV=compiler/reject_class.tsv
GOLDEN=compiler/census.expected.out
RGOLDEN=compiler/rcensus.expected.out
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
rc=0

[ -x "$TYCHOC1" ] || { echo "parse-check: no $TYCHOC1 -- run make tychoc1"; exit 1; }
[ -f "$TSV" ] || { echo "parse-check: no $TSV"; exit 1; }

# Corpus sizes, asserted first: a leg that scores 0 of 0 is green by accident,
# and a new tests/reject/ fixture must force a reclassification rather than
# being silently absent from the split.
n_acc=$(ls tests/*.ty | wc -l)
n_lib=$(find corelib -name '*.ty' | wc -l)
n_rej=$(ls tests/reject/*.ty | wc -l)
n_tsv=$(wc -l < "$TSV")
n_new=$(find tools examples server bench -name '*.ty' | wc -l)
[ "$n_acc" = 274 ] || { echo "parse-check: tests/*.ty is $n_acc, expected 274"; rc=1; }
[ "$n_lib" = 91 ]  || { echo "parse-check: corelib/**.ty is $n_lib, expected 91"; rc=1; }
[ "$n_new" = 179 ] || { echo "parse-check: tools+examples+server+bench .ty is $n_new, expected 179"; rc=1; }
[ "$n_rej" = "$n_tsv" ] || { echo "parse-check: $n_rej reject fixtures but $n_tsv classified rows -- rerun scripts/classify_rejects.py"; rc=1; }

# [1] and [1b] -- the accept corpora
leg_accept() {
    ok=0; bad=0
    for f in $2; do
        if "$TYCHOC1" "$f" --parse >/dev/null 2>"$T/e"; then ok=$((ok+1))
        else bad=$((bad+1)); echo "  PARSE-FAIL $f :: $(head -1 "$T/e")"; fi
    done
    echo "$1: files=$((ok+bad)) parse-ok=$ok fail=$bad"
    [ "$ok" = "$3" ] && [ "$bad" = 0 ] || { echo "parse-check: $1 expected $3 ok, 0 fail"; rc=1; }
}
leg_accept "leg1  tests/*.ty" "$(ls tests/*.ty)" 274
leg_accept "leg1b corelib/**.ty" "$(find corelib -name '*.ty' | sort)" 91
leg_accept "leg1c tools+examples+server+bench" "$(find tools examples server bench -name '*.ty' | sort)" 179

# [2] -- the reject corpus, split. There is no exemption list: Phase 2b closed
# the three literal-range misses this leg used to name, so every SYNTAX fixture
# must be rejected and `missed` must be 0. The split moved 47/290 -> 57/280 in
# Phase 4: eight rules were misjudged as needing a symbol table when each is
# decided by the signature, the five-name predicate set, or a parse-time const
# fold. scripts/classify_rejects.py carries the reasoning; rerun it to rebuild.
sr=0; sa=0; ma=0; mr=0
nsyn=0; nname=0; nsem=0
rr=0; rm_=0; ra=0; rw=0
while IFS='	' read -r f cls line msg; do
    case "$cls" in SYNTAX) nsyn=$((nsyn+1)) ;; NAME) nname=$((nname+1)) ;; SEMANTIC) nsem=$((nsem+1)) ;; esac
    if "$TYCHOC1" "$f" --parse >/dev/null 2>&1; then v=accept; else v=reject; fi
    case "$cls$v" in
        SYNTAXreject)   sr=$((sr+1)) ;;
        SYNTAXaccept)   sa=$((sa+1)); echo "  SYNTAX-NOT-REJECTED $f :: $msg" ;;
        *accept)        ma=$((ma+1)) ;;
        *reject)        mr=$((mr+1)); echo "  NON-SYNTAX-WRONGLY-REJECTED $f :: $msg" ;;
    esac
    if "$TYCHOC1" "$f" --resolve >/dev/null 2>&1; then w=accept; else w=reject; fi
    case "$cls$w" in
        SYNTAXreject|NAMEreject) rr=$((rr+1)) ;;
        SYNTAXaccept|NAMEaccept) rm_=$((rm_+1)); echo "  NAME-NOT-REJECTED $f :: $msg" ;;
        SEMANTICaccept)          ra=$((ra+1)) ;;
        SEMANTICreject)          rw=$((rw+1)); echo "  SEMANTIC-WRONGLY-REJECTED $f :: $msg :: $("$TYCHOC1" "$f" --resolve 2>&1 | head -1)" ;;
    esac
done < "$TSV"
echo "leg2  tests/reject/*.ty --parse: SYNTAX=$((sr+sa)) rejected=$sr missed=$sa | NAME+SEMANTIC=$((ma+mr)) accepted=$ma wrongly-rejected=$mr"
echo "leg2b tests/reject/*.ty --resolve: SYNTAX+NAME=$((rr+rm_)) rejected=$rr missed=$rm_ | SEMANTIC=$((ra+rw)) accepted=$ra wrongly-rejected=$rw"
[ "$nsyn" = 57 ] && [ "$nname" = 27 ] && [ "$nsem" = 253 ] || { echo "parse-check: the split moved -- expected SYNTAX=57 NAME=27 SEMANTIC=253"; rc=1; }
[ "$mr" = 0 ] || { echo "parse-check: a NAME or SEMANTIC fixture was rejected by --parse; a parser has no symbol table"; rc=1; }
[ "$sa" = 0 ] || { echo "parse-check: a SYNTAX fixture was accepted; the parser must refuse it"; rc=1; }
[ "$rm_" = 0 ] || { echo "parse-check: a NAME fixture resolved; the resolver must refuse it"; rc=1; }
[ "$rw" = 0 ] || { echo "parse-check: a SEMANTIC fixture was rejected by --resolve; a resolver cannot see a type error"; rc=1; }

# [3] -- the census, against a recorded golden
for f in $(ls tests/*.ty) $(find corelib -name '*.ty' | sort) $(find tools examples server bench -name '*.ty' | sort); do
    "$TYCHOC1" "$f" --parse-census 2>/dev/null || true
done | awk '{c[$1]+=$2} END{for (k in c) printf "%s=%d\n", k, c[k]}' | LC_ALL=C sort > "$T/census.out"
kinds=$(wc -l < "$T/census.out")
nodes=$(awk -F= '{s+=$2} END{print s}' "$T/census.out")
echo "leg3  census: $kinds kinds, $nodes nodes over $((n_acc+n_lib+n_new)) files"
if [ -n "$RECORD" ]; then
    cp "$T/census.out" "$GOLDEN"; echo "parse-check: recorded $GOLDEN"
elif ! cmp -s "$T/census.out" "$GOLDEN"; then
    echo "parse-check: the AST census moved -- the tree changed shape, not just the verdict"
    diff "$GOLDEN" "$T/census.out" | head -20
    rc=1
fi

# [7] -- the RESOLUTION census. Same corpus as [3], and the same argument: an
# accept/reject verdict is blind to a name that resolves to the WRONG
# declaration. Each line is `<class>:<mangled name> <count>`, so preferring a
# top-level const over a local, or the bare name over the package-local one,
# moves a line while every verdict leg stays green.
for f in $(ls tests/*.ty) $(find corelib -name '*.ty' | sort) $(find tools examples server bench -name '*.ty' | sort); do
    "$TYCHOC1" "$f" --resolve-census 2>/dev/null || true
done | awk '{c[$1]+=$2} END{for (k in c) printf "%s=%d\n", k, c[k]}' | LC_ALL=C sort > "$T/rcensus.out"
rkinds=$(wc -l < "$T/rcensus.out")
ruses=$(awk -F= '{s+=$2} END{print s}' "$T/rcensus.out")
echo "leg7  resolution census: $rkinds distinct targets, $ruses resolved uses"
if [ -n "$RECORD" ]; then
    cp "$T/rcensus.out" "$RGOLDEN"; echo "parse-check: recorded $RGOLDEN"
elif ! cmp -s "$T/rcensus.out" "$RGOLDEN"; then
    echo "parse-check: the RESOLUTION census moved -- a name resolves to a different declaration"
    diff "$RGOLDEN" "$T/rcensus.out" | head -20
    rc=1
fi

# [4] -- the declaration rules with no fixture in tests/reject/. Each probe is a
# whole program in its own directory, because sibling .ty files share a package.
# The REFUSE and ACCEPT halves are both required: refusals alone are satisfied by
# a parser that refuses everything, acceptances alone by one that refuses nothing.
mkdir -p "$T/p"
pr() { mkdir -p "$T/p/$1"; cat > "$T/p/$1/main.ty"; }

pr var_sink <<'EOF'
package main
fn f(xs: sink ...int) -> int:
    return len(xs)
fn main():
    print(str(f(1, 2)))
EOF
pr var_inout <<'EOF'
package main
fn f(xs: inout ...int) -> int:
    return len(xs)
fn main():
    print(str(f(1, 2)))
EOF
pr extern_sink <<'EOF'
package main
extern fn strlen(s: sink string) -> int
fn main():
    print(str(strlen("hi")))
EOF
pr extern_variadic <<'EOF'
package main
extern fn strlen(s: ...string) -> int
fn main():
    print(str(strlen("hi")))
EOF
pr sub_inout_param <<'EOF'
package main
struct G:
    xs: [int]
subscript at(g: inout G, i: int) -> inout int:
    yield &g.xs[i]
fn main():
    g := G([1, 2])
    g.at(0) = 5
    print(str(g.xs[0]))
EOF

pr ok_variadic <<'EOF'
package main
fn f(xs: ...int) -> int:
    return len(xs)
fn main():
    print(str(f(1, 2)))
EOF
pr ok_sink <<'EOF'
package main
fn f(s: sink string) -> int:
    return len(s)
fn main():
    print(str(f("hi")))
EOF
pr ok_inout <<'EOF'
package main
fn f(x: inout int):
    x = x + 1
fn main():
    v := 1
    f(&v)
    print(str(v))
EOF
pr ok_extern_inout <<'EOF'
package main
extern fn frexp(x: float, e: inout int) -> float
fn main():
    n := 0
    print(str(frexp(8.0, &n)))
EOF
# The accepting subscript twin is also the only place either corpus writes
# THROUGH a subscript, which is the `g.at(0) = 5` place form (src/tychoc.c:4136).
pr ok_subscript <<'EOF'
package main
struct G:
    xs: [int]
subscript at(g: G, i: int) -> inout int:
    yield &g.xs[i]
fn main():
    g := G([1, 2])
    g.at(0) = 5
    print(str(g.xs[0]))
EOF

nref=0; nacc=0; l4=0
for d in var_sink var_inout extern_sink extern_variadic sub_inout_param; do
    if "$TYCHOC1" "$T/p/$d/main.ty" --parse >/dev/null 2>&1; then
        echo "  DECL-NOT-REFUSED $d"; l4=1
    else nref=$((nref+1)); fi
done
for d in ok_variadic ok_sink ok_inout ok_extern_inout ok_subscript; do
    if "$TYCHOC1" "$T/p/$d/main.ty" --parse >/dev/null 2>&1; then nacc=$((nacc+1))
    else echo "  DECL-WRONGLY-REFUSED $d :: $("$TYCHOC1" "$T/p/$d/main.ty" --parse 2>&1 | head -1)"; l4=1; fi
done
echo "leg4  declaration rules: refused=$nref/5 accepted=$nacc/5"
[ "$l4" = 0 ] || { echo "parse-check: a declaration rule moved"; rc=1; }

# [5] -- the whole tree, both verdicts, split by the same site table. See the
# header of compiler/verdict_diff.py; it is the only leg that reaches
# tests/conc, tests/diag and tests/reject/pkg.
python3 compiler/verdict_diff.py || rc=1

[ "$rc" = 0 ] && echo "parse-check: all green" || echo "parse-check: FAILED"
exit $rc
