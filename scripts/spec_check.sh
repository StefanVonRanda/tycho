set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
appendix="$root/docs/spec/appendix-a-grammar.md"
begin='<!-- BEGIN GENERATED'
end='<!-- END GENERATED -->'

fail=0

# --fast: checks 1 and 2 only -- both are text comparisons over docs/spec/ and
# cost ~0.05s, against ~20s for spec_examples.sh, which builds and runs every
# runnable example in the spec. That split is what makes the drift check cheap
# enough for .githooks/pre-push, where the whole gate is not: `packed` reached
# origin with Appendix A and 02-grammar.md disagreeing, and nothing between the
# two commits could notice.
fast=0
[ "${1:-}" = "--fast" ] && fast=1

# --- Check 1: Appendix A collected grammar matches the chapters -------------
# Slice the region strictly between the marker lines (markers excluded).
committed=$(awk -v b="$begin" -v e="$end" '
    index($0,b){inr=1; next}
    index($0,e){inr=0}
    inr{print}
' "$appendix")

generated=$(sh "$root/scripts/gen_grammar.sh")

if [ "$committed" = "$generated" ]; then
    echo "spec-check: Appendix A grammar matches §3/§4 (ok)"
else
    echo "spec-check: FAIL — Appendix A grammar has drifted from §3/§4." >&2
    echo "  Regenerate with: sh scripts/gen_grammar.sh  (paste into the GENERATED region)" >&2
    echo "  --- diff (committed appendix  vs  generated) ---" >&2
    tmpc=$(mktemp); tmpg=$(mktemp)
    printf '%s\n' "$committed" > "$tmpc"
    printf '%s\n' "$generated" > "$tmpg"
    diff "$tmpc" "$tmpg" >&2 || true
    rm -f "$tmpc" "$tmpg"
    fail=1
fi

# --- Check 2: every fixture cited in Appendix E exists ----------------------
# The conformance matrix is worthless if it points at fixtures that were
# renamed or removed. Extract each `code`-quoted fixture path and assert it
# resolves to a real file/dir (bare reject//abort/ are under tests/).
econf="$root/docs/spec/appendix-e-conformance.md"
missing=$(
    grep -oE '`(tests/[A-Za-z0-9_/.]+|reject/[A-Za-z0-9_]+|abort/[A-Za-z0-9_]+|corelib/test/[A-Za-z0-9_]+|examples/[A-Za-z0-9_/.]+)`' "$econf" \
    | tr -d '`' | sort -u | while read -r p; do
        if [ "${p#reject/}" != "$p" ] || [ "${p#abort/}" != "$p" ]; then
            rel="tests/$p"
        else
            rel="$p"
        fi
        if [ ! -e "$root/$rel" ] && [ ! -e "$root/$rel.ty" ] && [ ! -d "$root/$rel" ]; then
            echo "$p"
        fi
    done
)
if [ -z "$missing" ]; then
    echo "spec-check: all Appendix E fixture citations resolve (ok)"
else
    echo "spec-check: FAIL — Appendix E cites fixtures that do not exist:" >&2
    echo "$missing" | sed 's/^/    /' >&2
    fail=1
fi

if [ "$fast" = 1 ]; then
    echo "spec-check: --fast, NOT run: spec_examples.sh (the runnable examples in docs/spec/). Run \`sh scripts/spec_check.sh\` for those."
elif sh "$root/scripts/spec_examples.sh"; then
    :
else
    fail=1
fi

exit $fail
