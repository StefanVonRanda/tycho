#!/bin/sh
# The Phase 3 parse sweep, as a runnable lane. `make parse-check`.
#
# Four legs. The first three are counts; the fourth is the one that carries the
# wrong-tree class, and it is the reason this lane is not two `wc -l`s:
#
#   [1]  every tests/*.ty that ./tychoc accepts must parse
#   [0]  the classifier table's own citations: every src/tychoc.c line it names
#        must still hold the diagnostic it was classified by.
#   [1b] every corelib/**.ty must parse
#   [2]  tests/reject/, SPLIT by the committed classifier table:
#        a SYNTAX rejection must be rejected, a SEMANTIC one must be ACCEPTED.
#        "everything in tests/reject/ was rejected" is the failure this leg
#        exists to catch -- most of them are type errors a parser cannot see.
#   [1c] every tools/ examples/ server/ bench/ .ty must parse
#   [3]  the AST node-kind census, compared to a recorded golden. An accept/
#        reject verdict is blind to a parse that SUCCEEDS with the wrong tree:
#        dropping the ast.Named wrapper from a `name: value` call argument left
#        legs 1 and 2 fully green on every file in the corpus and only
#        moved the census.
#   [4]  the declaration rules that NO fixture in tests/reject/ covers, each
#        written here as a whole program. Every refusal is paired with an
#        accepting twin one token away -- a leg that only ever refuses is
#        satisfied by a parser that refuses everything.
#   [5]  every .ty in the tree, scored against ./tychoc's own
#        verdict -- the only leg that reaches tests/conc, tests/diag and
#        tests/reject/pkg, where three of Phase 4's four defects were.
#   [6]  the same files scored a SECOND time, against `--resolve`: the
#        SYNTAX and NAME files must be refused, everything else accepted, and a
#        file ./tychoc accepts must resolve with no unused local and no unused
#        import. Phase 5 split NAME out of SEMANTIC; Phase 5c added the three
#        rules it had left behind -- the NAME count is printed by
#        compiler/verdict_diff.py.
#   [2b] the reject corpus again, under `--resolve` rather than `--parse`.
#   [7]  the RESOLUTION census, the leg the two verdicts cannot carry: a name
#        that resolves to the WRONG declaration is accepted either way. Every
#        use is printed as the package-mangled name it resolved to, so
#        preferring a top-level const over a local -- which leaves legs 1..6
#        fully green -- moves a counted line.
#   [4b] the two package-member message FORMATS, which no verdict leg can see:
#        `pkg.Name` (no call) and `pkg.name(...)` are separate formats in
#        src/tychoc.c and tychoc1 answered both with the second one.
#   [8]  every NAME file's `file:line` compared against ./tychoc's for the same
#        input. Phase 5b put a line on every AST node, and a line that is
#        PRESENT BUT WRONG passes every leg above: the file is refused either
#        way. Printed by compiler/verdict_diff.py alongside legs 5 and 6.
#   [2c] the reject corpus a THIRD time, under `--typecheck`: the SYNTAX, NAME
#        and TYPE files must be refused and the SEMANTIC ones accepted. Phase 6a
#        split TYPE out of SEMANTIC by the same rule -- the die_at SITE.
#   [9]  the TYPE census: the type this pass INFERRED at every declaration,
#        parameter, return, loop variable, call, field, index and operator.
#        Making `str(x)` answer `?` instead of `string` left every verdict leg
#        on every file in the corpus green and moved 9,742 counted sites here.
#   [10] the whole tree under `--typecheck`, and [11] every TYPE file's
#        `file:line` against ./tychoc's -- leg8's argument, for the new class.
#   [12] how many accepted programs use a generic/newtype/handle/bounded, all of
#        which this pass answers `?`: the size of what Phase 6a defers.
#
# The split in compiler/reject_class.tsv is COMMITTED, not recomputed: it is
# grounded in src/tychoc.c's diagnostic sites (see scripts/classify_rejects.py),
# costs a full ./tychoc run over every tests/reject/*.ty to rebuild, and is
# the half of
# this evidence that is expensive and auditable rather than cheap and derived.
#
# RECORD=1 re-records the census golden. It cannot bless a lost count: legs 1,
# 1b and 2 compare against literals below.
cd "$(dirname "$0")/.." || exit 1

TYCHOC1="${TYCHOC1:-./tychoc1}"
TSV=compiler/reject_class.tsv
GOLDEN=compiler/census.expected.out
RGOLDEN=compiler/rcensus.expected.out
TGOLDEN=compiler/tcensus.expected.out
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
[ "$n_acc" = 281 ] || { echo "parse-check: tests/*.ty is $n_acc, expected 280"; rc=1; }   # +1 each: tests/packed_struct.ty (V2), tests/vector_type.ty (V3), tests/fixarr_iter.ty (V3a)
[ "$n_lib" = 91 ]  || { echo "parse-check: corelib/**.ty is $n_lib, expected 91"; rc=1; }
[ "$n_new" = 178 ] || { echo "parse-check: tools+examples+server+bench .ty is $n_new, expected 178"; rc=1; }   # -1: tools/tycho-rsa/ removed in 0888bf28, which left this literal at 179 and the lane red
[ "$n_rej" = "$n_tsv" ] || { echo "parse-check: $n_rej reject fixtures but $n_tsv classified rows -- rerun scripts/classify_rejects.py"; rc=1; }

# The same literals, read back out of this file and compared to the tree by
# scripts/check_corpus_counts.py -- sub-second, so a commit that adds or deletes
# a .ty can run it alone. It is here as well so the lane and its predictor cannot
# drift apart: if it ever passes where the block above fails, one of them is
# reading the wrong corpus. `make corpus-check`.
python3 scripts/check_corpus_counts.py || rc=1
python3 scripts/check_corpus_counts.py --selfcheck >/dev/null || { echo "parse-check: corpus-counts selfcheck failed -- run it directly"; rc=1; }

# [0] -- the classifier table's own evidence. Column 3 is a src/tychoc.c line and
# nothing gated it, so it rotted silently: 310 of 337 rows cited a line that had
# moved (2026-09-03) while every class was still right. Cheap -- it re-reads the
# diagnostic sites, it does not run ./tychoc.
# Column 2, the CLASS, is checked alongside it since 2026-09-04: the SYNTAX
# boundary was a typed line and its drift reclassified three fixtures in silence.
# The selfcheck is the proof both legs can still fail; it runs no ./tychoc either.
python3 scripts/check_reject_sites.py || rc=1
python3 scripts/check_reject_sites.py --selfcheck || rc=1

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
leg_accept "leg1  tests/*.ty" "$(ls tests/*.ty)" 281
leg_accept "leg1b corelib/**.ty" "$(find corelib -name '*.ty' | sort)" 91
leg_accept "leg1c tools+examples+server+bench" "$(find tools examples server bench -name '*.ty' | sort)" 178

# [2] -- the reject corpus, split. There is no exemption list: Phase 2b closed
# the three literal-range misses this leg used to name, so every SYNTAX fixture
# must be rejected and `missed` must be 0. The split moved 47/290 -> 57/280 when
# eight rules were re-read off their sites (2026-08): each had been misjudged as
# needing a symbol table when it is decided by the signature, the five-name
# predicate set, or a parse-time const fold. scripts/classify_rejects.py carries the reasoning; rerun it to rebuild.
sr=0; sa=0; ma=0; mr=0
nsyn=0; nname=0; nsem=0; ntype=0
rr=0; rm_=0; ra=0; rw=0
tr=0; tm=0; ta=0; tw=0
# Phase 6b made `--typecheck` the WHOLE semantic check: SEMANTIC is no longer an
# accept bucket, so every reject fixture must be refused except the one named
# here. It is compared as a SET, so a new miss reddens the lane and so does a
# fixed one.
#   generic_recur_grow  needs the template BODY instantiated, not just its
#                       signature
# infer_use_before_ground and void_grounds_pending_push LEFT this list when the
# B-3 grounding analysis was ported into compiler/types/tcheck.ty@_pend_ground:
# both are refused by their OWN rule now, byte for byte with ./tychoc.
# infer_bare_empty LEFT it earlier, when `declared and not used` became fatal --
# by THAT rule, not by grounding; it is the :8909 audit that refuses it today.
# len_scalar LEFT it too, when the f-string interpolation holes became real
# expressions: the `len` rule was always here, the hole was simply never walked.
KNOWN_TYPE_MISS="tests/reject/generic_recur_grow.ty"
# (the whole-tree list in compiler/verdict_diff.py carries five more, all under
# tests/diag and tests/reject/pkg, which this leg does not reach)
seen_miss=""
while IFS='	' read -r f cls line msg; do
    case "$cls" in SYNTAX) nsyn=$((nsyn+1)) ;; NAME) nname=$((nname+1)) ;; TYPE) ntype=$((ntype+1)) ;; SEMANTIC) nsem=$((nsem+1)) ;; esac
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
        *accept)                 ra=$((ra+1)) ;;
        *reject)                 rw=$((rw+1)); echo "  NON-NAME-WRONGLY-REJECTED $f :: $msg :: $("$TYCHOC1" "$f" --resolve 2>&1 | head -1)" ;;
    esac
    if "$TYCHOC1" "$f" --typecheck >/dev/null 2>&1; then x=accept; else x=reject; fi
    case "$x" in
        reject) tr=$((tr+1)) ;;
        accept) tm=$((tm+1)); seen_miss="$seen_miss $f"; echo "  NOT-REJECTED $f :: $msg" ;;
    esac
done < "$TSV"
echo "leg2  tests/reject/*.ty --parse: SYNTAX=$((sr+sa)) rejected=$sr missed=$sa | NAME+SEMANTIC=$((ma+mr)) accepted=$ma wrongly-rejected=$mr"
echo "leg2b tests/reject/*.ty --resolve: SYNTAX+NAME=$((rr+rm_)) rejected=$rr missed=$rm_ | TYPE+SEMANTIC=$((ra+rw)) accepted=$ra wrongly-rejected=$rw"
echo "leg2c tests/reject/*.ty --typecheck: all=$((tr+tm)) rejected=$tr missed=$tm (KNOWN $(echo $KNOWN_TYPE_MISS | wc -w))"
# SYNTAX 93 -> 95: the two `packed struct` fixtures (V2, 2026-09-04), both
# raised inside parse_struct and so both the parser's to refuse.
# SYNTAX 95 -> 98 and SEMANTIC 264 -> 267: `vector[N]T` (V3) -- three parser
# rules (both power-of-two refusals and the absent count) and three that need a
# symbol table (the non-const count, the element set, the vector/array mix).
# SEMANTIC 256 -> 264: the bytes bridge (V2b) -- seven new rules for
# from_bytes$(T)/size_of$(T) plus to_bytes over a packed struct, all raised
# after the parse region and so none of them the parser's.
# SEMANTIC 267 -> 271: V3a -- slice and push refused on a [N]T and on a vector[N]T.
# NAME 33 -> 34: tests/reject/fstring_hole_name.ty -- the NAME rule reached
# only from inside an f-string interpolation hole.
# NAME 34 -> 35: tests/reject/destructure_dup_name.ty -- a duplicate BINDING in a
# destructuring list, which tychoc1 refuses in its resolver.
# SYNTAX 98 -> 101: the three malformed f-string bodies, which tychoc1 refused
# only in emit until 2026-09-05 and now refuses in compiler/parse/parse.ty@_fholes.
# SYNTAX 101 -> 103: a struct/enum type parameter that is not a `$Name`, ported
# into compiler/parse/parse.ty@_is_typaram_ty -- tychoc1 accepted both outright.
[ "$nsyn" = 104 ] && [ "$nname" = 35 ] && [ "$ntype" = 164 ] && [ "$nsem" = 271 ] || { echo "parse-check: the split moved -- expected SYNTAX=104 NAME=35 TYPE=164 SEMANTIC=271"; rc=1; }
[ "$mr" = 0 ] || { echo "parse-check: a NAME or SEMANTIC fixture was rejected by --parse; a parser has no symbol table"; rc=1; }
[ "$sa" = 0 ] || { echo "parse-check: a SYNTAX fixture was accepted; the parser must refuse it"; rc=1; }
[ "$rm_" = 0 ] || { echo "parse-check: a NAME fixture resolved; the resolver must refuse it"; rc=1; }
[ "$rw" = 0 ] || { echo "parse-check: a TYPE or SEMANTIC fixture was rejected by --resolve; a resolver cannot see a type error"; rc=1; }
# The misses are compared as a SET, not as a count: a new one reddens the lane
# and so does a fixed one, which is what stops this exemption from widening.
got=$(echo $seen_miss | tr ' ' '\n' | LC_ALL=C sort | tr '\n' ' ')
want=$(echo $KNOWN_TYPE_MISS | tr ' ' '\n' | LC_ALL=C sort | tr '\n' ' ')
[ "$got" = "$want" ] || { echo "parse-check: the TYPE misses moved"; echo "    now:  $got"; echo "    was:  $want"; rc=1; }
[ "$tr" = 573 ] || { echo "parse-check: --typecheck refused $tr of 574, expected 573"; rc=1; }

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

# [9] -- the TYPE census: every declaration, parameter, return, loop variable,
# call result, field access, index and operator, printed as the type this pass
# INFERRED for it. The verdict legs cannot see a wrong inference on a program
# that is accepted either way -- reading `f * 2` at an unknown `f` as `int` left
# every one of legs 1..8 and 2c green while the type was simply wrong.
for f in $(ls tests/*.ty) $(find corelib -name '*.ty' | sort) $(find tools examples server bench -name '*.ty' | sort); do
    "$TYCHOC1" "$f" --type-census 2>/dev/null || true
done | awk '{c[$1]+=$2} END{for (k in c) printf "%s=%d\n", k, c[k]}' | LC_ALL=C sort > "$T/tcensus.out"
tkinds=$(wc -l < "$T/tcensus.out")
tnodes=$(awk -F= '{s+=$2} END{print s}' "$T/tcensus.out")
tunk=$(awk -F= '/^[a-z]:\?=/{s+=$2} END{print s+0}' "$T/tcensus.out")
echo "leg9  type census: $tkinds distinct types, $tnodes inference sites, $tunk deferred to Phase 6b"
if [ -n "$RECORD" ]; then
    cp "$T/tcensus.out" "$TGOLDEN"; echo "parse-check: recorded $TGOLDEN"
elif ! cmp -s "$T/tcensus.out" "$TGOLDEN"; then
    echo "parse-check: the TYPE census moved -- an expression infers a different type"
    diff "$TGOLDEN" "$T/tcensus.out" | head -20
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
# THROUGH a subscript, which is the `g.at(0) = 5` place form (src/tychoc.c:4202).
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

# [4b] -- the two package-member formats, which NO verdict leg can see: both
# spellings are a refusal, so leg2b/5/6/8 are green either way and the wording
# is decoration until Phase 9 pins message text. src/tychoc.c:6403 answers
# `pkg.Name` written with NO call; src/tychoc.c:6741 answers `pkg.name(...)`.
# Both measured against ./tychoc 2026-08-23. The accepting twin is required for
# the usual reason: two refusals alone are satisfied by refusing everything.
pr r3_field <<'EOF'
package main
import "core:strings"
fn main():
    v := strings.NoSuchThingHere
    print(str(v))
EOF
pr r3_call <<'EOF'
package main
import "core:strings"
fn main():
    print(strings.no_such_thing_here("x"))
EOF
pr r3_ok <<'EOF'
package main
import "core:strings"
fn main():
    print(strings.trim(" x "))
EOF
l4b=0; n4b=0
mf=$("$TYCHOC1" "$T/p/r3_field/main.ty" --resolve 2>&1 | head -1)
mc=$("$TYCHOC1" "$T/p/r3_call/main.ty" --resolve 2>&1 | head -1)
case "$mf" in *"has no variant, const or function 'NoSuchThingHere'"*) n4b=$((n4b+1)) ;;
    *) echo "  R3-FIELD-WRONG :: $mf"; l4b=1 ;; esac
case "$mc" in *"has no symbol 'no_such_thing_here'"*) n4b=$((n4b+1)) ;;
    *) echo "  R3-CALL-WRONG :: $mc"; l4b=1 ;; esac
if "$TYCHOC1" "$T/p/r3_ok/main.ty" --resolve >/dev/null 2>&1; then n4b=$((n4b+1))
else echo "  R3-OK-REFUSED :: $("$TYCHOC1" "$T/p/r3_ok/main.ty" --resolve 2>&1 | head -1)"; l4b=1; fi
echo "leg4b package-member wording: passed=$n4b/3 (field format, call format, accepting twin)"
[ "$l4b" = 0 ] || { echo "parse-check: the two package-member formats collapsed into one"; rc=1; }

# [13] -- THE AFFINE RULES, ONE PROBE EACH, EVERY REFUSAL PAIRED WITH AN
# ACCEPTING TWIN. This leg is written this way because a checker that refuses
# EVERYTHING scores identically to a correct one on tests/reject/: leg2c cannot
# tell the two apart, and neither can leg10. Each pair below differs by ONE
# token, and the twin is the half that fails when a rule is too wide --
# tools/tycho-fh/run.sh's borrow leg makes the same argument about a handle
# passed as an argument, which must stay a BORROW and not become a consume.
mkdir -p "$T/a"
ap() { mkdir -p "$T/a/$1"; cat > "$T/a/$1/main.ty"; }

ap chan_copy <<'EOF'
package main
fn main():
    c := channel(int, 2)
    e := c
    send(e, 41)
EOF
ap ok_chan_copy <<'EOF'
package main
fn main():
    c := channel(int, 2)
    e := channel(int, 2)
    send(e, 41)
    send(c, 41)
EOF
ap chan_arr <<'EOF'
package main
fn f(v: [Channel(int)]) -> int:
    return len(v)
fn main():
    print(str(1))
EOF
ap ok_chan_param <<'EOF'
package main
fn f(v: Channel(int)) -> int:
    return 1
fn main():
    c := channel(int, 2)
    print(str(f(c)))
EOF
ap chan_field <<'EOF'
package main
struct S:
    c: Channel(int)
fn main():
    print("x")
EOF
ap ok_chan_field <<'EOF'
package main
struct S:
    c: int
fn main():
    print("x")
EOF
ap chan_inout <<'EOF'
package main
fn f(c: inout Channel(int)) -> int:
    return 1
fn main():
    print("x")
EOF
ap ok_chan_plain <<'EOF'
package main
fn f(c: Channel(int)) -> int:
    return 1
fn main():
    print("x")
EOF
ap chan_capture <<'EOF'
package main
fn main():
    c := channel(int, 2)
    g := fn(x: int) -> int: x + len(str(c))
    print(str(g(1)))
EOF
ap ok_no_capture <<'EOF'
package main
fn main():
    c := channel(int, 2)
    g := fn(x: int) -> int: x + 1
    send(c, 1)
    print(str(g(1)))
EOF
ap task_copy <<'EOF'
package main
fn w(n: int) -> int:
    return n
fn main():
    t := spawn w(1)
    u := t
    print(str(wait(u)))
EOF
ap ok_task <<'EOF'
package main
fn w(n: int) -> int:
    return n
fn main():
    t := spawn w(1)
    print(str(wait(t)))
EOF
ap task_arr <<'EOF'
package main
fn w(n: int) -> int:
    return n
fn main():
    ts := [spawn w(1)]
    print(str(len(ts)))
EOF
ap handle_copy <<'EOF'
package main
handle H:
    free: hclose
extern "x" fn hopen(id: int) -> H
extern "x" fn hclose(h: H) -> int
fn main():
    f := hopen(1)
    g := f
    close(g)
EOF
# The BORROW twin, and the reason this pair exists: passing a handle to a
# function is a borrow, not a consume, and a rule wide enough to catch `g := f`
# must not catch `use(f)`. tools/tycho-fh/run.sh guards the same boundary.
ap ok_handle_borrow <<'EOF'
package main
handle H:
    free: hclose
extern "x" fn hopen(id: int) -> H
extern "x" fn hclose(h: H) -> int
fn use1(h: H) -> int:
    return 1
fn main():
    f := hopen(1)
    print(str(use1(f) + use1(f)))
    close(f)
EOF
ap handle_field <<'EOF'
package main
handle H:
    free: hclose
extern "x" fn hopen(id: int) -> H
extern "x" fn hclose(h: H) -> int
struct Bag:
    h: H
fn main():
    print("x")
EOF
ap handle_nonvar_close <<'EOF'
package main
handle H:
    free: hclose
extern "x" fn hopen(id: int) -> H
extern "x" fn hclose(h: H) -> int
fn main():
    close(hopen(1))
EOF
ap chan_generic_field <<'EOF'
package main
struct Box($T):
    v: $T
fn main():
    c := channel(int, 2)
    b := Box(c)
    send(b.v, 1)
EOF
# ... and the SAME generic at a type that is not affine, which is what proves
# the refusal is about the INSTANCE and not about `Box` being generic at all.
ap ok_generic_field <<'EOF'
package main
struct Box($T):
    v: $T
fn main():
    b := Box(7)
    print(str(b.v))
EOF

l13=0; n13r=0; n13a=0
for d in chan_copy chan_arr chan_field chan_inout chan_capture task_copy task_arr \
         handle_copy handle_field handle_nonvar_close chan_generic_field; do
    if "$TYCHOC1" "$T/a/$d/main.ty" --typecheck >/dev/null 2>&1; then
        echo "  AFFINE-NOT-REFUSED $d"; l13=1
    else n13r=$((n13r+1)); fi
done
for d in ok_chan_copy ok_chan_param ok_chan_field ok_chan_plain ok_no_capture ok_task ok_handle_borrow ok_generic_field; do
    if "$TYCHOC1" "$T/a/$d/main.ty" --typecheck >/dev/null 2>&1; then n13a=$((n13a+1))
    else echo "  AFFINE-WRONGLY-REFUSED $d :: $("$TYCHOC1" "$T/a/$d/main.ty" --typecheck 2>&1 | head -1)"; l13=1; fi
done
echo "leg13 affine shapes, one probe each: refused=$n13r/11 accepted=$n13a/8 (each refusal has an accepting twin)"
[ "$l13" = 0 ] || { echo "parse-check: an affine rule moved"; rc=1; }

# [14] -- NEWTYPE DISTINCTNESS, the same argument in the form tools/tycho-ledger
# makes it: a newtype is ERASED in lowering (spec 5.4), so `money.Cents` is
# represented exactly as an `int` and NO transcript, golden or type census can
# see whether the three domain types are distinct. The only instrument is a
# probe that must FAIL to compile -- and each refusal must name the UNMANGLED
# type, never `coin__Cents`, which is what ledger-check greps for.
mkdir -p "$T/n/coin"
cat > "$T/n/coin/coin.ty" <<'EOF'
package coin
type Cents = int
type Rate = float
type Account = string
fn cents(n: int) -> Cents:
    return Cents(n)
EOF
np() { mkdir -p "$T/n/$1"; cp -r "$T/n/coin" "$T/n/$1/coin"; cat > "$T/n/$1/main.ty"; }

np mix_newtypes <<'EOF'
package main
import "coin"
fn main():
    println(str(to_int(coin.cents(1) + coin.Rate(2.0))))
EOF
np under_enum <<'EOF'
package main
import "coin"
enum E:
    X
type Bad = E
fn main():
    println("x")
EOF
np under_newtype <<'EOF'
package main
import "coin"
type Bad = coin.Cents
fn main():
    println("x")
EOF
np under_tuple <<'EOF'
package main
import "coin"
type Bad = (int, int)
fn main():
    println("x")
EOF
np ok_same_newtype <<'EOF'
package main
import "coin"
fn main():
    println(str(to_int(coin.cents(1) + coin.cents(2))))
EOF
np ok_under_struct <<'EOF'
package main
struct P:
    x: int
type Good = P
fn main():
    g := Good(P(1))
    println(str(to_under(g).x))
EOF

l14=0; n14r=0; n14a=0
for d in mix_newtypes under_enum under_newtype under_tuple; do
    m14=$("$TYCHOC1" "$T/n/$d/main.ty" --typecheck 2>&1 | head -1)
    if "$TYCHOC1" "$T/n/$d/main.ty" --typecheck >/dev/null 2>&1; then
        echo "  NEWTYPE-NOT-REFUSED $d"; l14=1
    else
        case "$m14" in *__*) echo "  NEWTYPE-MANGLED-IN-MESSAGE $d :: $m14"; l14=1 ;;
            *) n14r=$((n14r+1)) ;; esac
    fi
done
for d in ok_same_newtype ok_under_struct; do
    if "$TYCHOC1" "$T/n/$d/main.ty" --typecheck >/dev/null 2>&1; then n14a=$((n14a+1))
    else echo "  NEWTYPE-WRONGLY-REFUSED $d :: $("$TYCHOC1" "$T/n/$d/main.ty" --typecheck 2>&1 | head -1)"; l14=1; fi
done
echo "leg14 newtype distinctness: refused=$n14r/4 accepted=$n14a/2 (no message names a mangled type)"
[ "$l14" = 0 ] || { echo "parse-check: a newtype rule moved"; rc=1; }

# [15] -- THE CONCURRENCY AND MATCH-ARM FAMILIES, BY MESSAGE. leg [5] below
# reaches tests/conc/reject and tests/reject/pkg, but it scores a VERDICT: a
# fixture refused for the WRONG reason is a refusal either way, and every one of
# these rules is a statement SHAPE with a sibling shape next to it, so refusing
# by a neighbour's rule is the likely regression rather than accepting. So each
# fixture's first diagnostic MESSAGE is compared against ./tychoc's, and the
# legal programs beside them must still typecheck -- the accepting twin, without
# which "refuses everything" scores full marks on a corpus of rejections.
l15=0; n15r=0; n15a=0; n15d=0
# The first diagnostic MESSAGE, location and driver prefix stripped, so the
# two are comparable. merge_pkg's two package-header refusals carry NO
# `error:` and name the FILE -- an unlocated `tychoc: <msg>` is a message
# like any other, and matching only the located form scored those two as an
# empty string, i.e. as not refused at all (observed while writing this).
m1() { sed -n -e 's/^.*: error: //p' -e 's/^tychoc1\{0,1\}: \([^ ].*\)$/\1/p' \
           | grep -v ': error: ' | head -1; }
for f in tests/conc/reject/*.ty tests/reject/pkg/*/main.ty; do
    a=$("./tychoc" "$f" --emit-c -o "$T/_l15.c" 2>&1 | m1)
    b=$("$TYCHOC1" "$f" --typecheck 2>&1 | m1)
    if [ -z "$b" ]; then echo "  CONC-NOT-REFUSED $f"; l15=1; continue; fi
    n15r=$((n15r+1))
    if [ "$a" != "$b" ]; then
        n15d=$((n15d+1)); l15=1
        echo "  CONC-RULE-MOVED $f"; echo "    tychoc : $a"; echo "    tychoc1: $b"
    fi
done
for f in tests/conc/*.ty tests/pkg/*/main.ty; do
    [ -f "$f" ] || continue
    if "$TYCHOC1" "$f" --typecheck >/dev/null 2>&1; then n15a=$((n15a+1))
    else echo "  CONC-WRONGLY-REFUSED $f :: $("$TYCHOC1" "$f" --typecheck 2>&1 | head -1)"; l15=1; fi
done
echo "leg15 conc + pkg rules by MESSAGE: refused=$n15r/50 disagree=$n15d accepted=$n15a/39"
[ "$n15r" = 50 ] && [ "$n15a" = 39 ] || { echo "parse-check: leg15 corpus moved -- update the literals"; l15=1; }
[ "$l15" = 0 ] || { echo "parse-check: a concurrency or match-arm rule moved"; rc=1; }

# [5] -- the whole tree, both verdicts, split by the same site table. See the
# header of compiler/verdict_diff.py; it is the only leg that reaches
# tests/conc, tests/diag and tests/reject/pkg.
python3 compiler/verdict_diff.py || rc=1

[ "$rc" = 0 ] && echo "parse-check: all green" || echo "parse-check: FAILED"
exit $rc
