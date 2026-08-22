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
#   [3]  the AST node-kind census, compared to a recorded golden. An accept/
#        reject verdict is blind to a parse that SUCCEEDS with the wrong tree:
#        dropping the ast.Named wrapper from a `name: value` call argument left
#        legs 1 and 2 fully green on all 611 files and only moved the census.
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
[ "$n_acc" = 274 ] || { echo "parse-check: tests/*.ty is $n_acc, expected 274"; rc=1; }
[ "$n_lib" = 91 ]  || { echo "parse-check: corelib/**.ty is $n_lib, expected 91"; rc=1; }
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

# [2] -- the reject corpus, split. There is no exemption list any more: Phase 2b
# closed the three literal-range misses this leg used to name
# (float_lit_overflow_neg, int_hex_overflow, int_literal_overflow), so every
# SYNTAX fixture must now be rejected and `missed` must be 0.
sr=0; sa=0; ma=0; mr=0
: > "$T/missed"; : > "$T/wrong"
while IFS='	' read -r f cls line msg; do
    if "$TYCHOC1" "$f" --parse >/dev/null 2>&1; then v=accept; else v=reject; fi
    case "$cls$v" in
        SYNTAXreject)   sr=$((sr+1)) ;;
        SYNTAXaccept)   sa=$((sa+1)); echo "$f" >> "$T/missed"; echo "  SYNTAX-NOT-REJECTED $f :: $msg" ;;
        SEMANTICaccept) ma=$((ma+1)) ;;
        SEMANTICreject) mr=$((mr+1)); echo "$f" >> "$T/wrong"; echo "  SEMANTIC-WRONGLY-REJECTED $f :: $msg" ;;
    esac
done < "$TSV"
echo "leg2  tests/reject/*.ty: SYNTAX=$((sr+sa)) rejected=$sr missed=$sa | SEMANTIC=$((ma+mr)) accepted=$ma wrongly-rejected=$mr"
[ "$((sr+sa))" = 47 ] && [ "$((ma+mr))" = 290 ] || { echo "parse-check: the split moved -- expected SYNTAX=47 SEMANTIC=290"; rc=1; }
[ "$mr" = 0 ] || { echo "parse-check: a SEMANTIC fixture was rejected; a parser cannot see a type error"; rc=1; }
[ "$sa" = 0 ] || { echo "parse-check: a SYNTAX fixture was accepted; the parser must refuse it"; rc=1; }

# [3] -- the census, against a recorded golden
for f in $(ls tests/*.ty) $(find corelib -name '*.ty' | sort); do
    "$TYCHOC1" "$f" --parse-census 2>/dev/null || true
done | awk '{c[$1]+=$2} END{for (k in c) printf "%s=%d\n", k, c[k]}' | LC_ALL=C sort > "$T/census.out"
kinds=$(wc -l < "$T/census.out")
nodes=$(awk -F= '{s+=$2} END{print s}' "$T/census.out")
echo "leg3  census: $kinds kinds, $nodes nodes over $((n_acc+n_lib)) files"
if [ -n "$RECORD" ]; then
    cp "$T/census.out" "$GOLDEN"; echo "parse-check: recorded $GOLDEN"
elif ! cmp -s "$T/census.out" "$GOLDEN"; then
    echo "parse-check: the AST census moved -- the tree changed shape, not just the verdict"
    diff "$GOLDEN" "$T/census.out" | head -20
    rc=1
fi

[ "$rc" = 0 ] && echo "parse-check: all green" || echo "parse-check: FAILED"
exit $rc
