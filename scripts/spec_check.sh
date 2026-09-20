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
# The SUB-CASE names, which the container check above cannot see. A row often
# cites `corelib/test/io` (`byte_index`, `byte_slice`, ...) -- the directory is
# validated, the names inside it were not, and those names ARE the evidence for
# the clause. FRICTION 119 found five citations pointing at nothing because they
# were not path-shaped; this closes the same hole one level down, where a
# renamed test case would leave the spec citing a name no file contains.
python3 - "$econf" "$root" <<'ESUB'
import re, sys, os
econf, root = sys.argv[1], sys.argv[2]
bad = 0; checked = 0
# `container` (`a`, `b`) -- container may be a dir, a .ty, or a source file
pat = re.compile(r'`([A-Za-z0-9_/.\-]+)`\s*\(((?:\s*`[A-Za-z0-9_]+`\s*,?)+)\)')
for line in open(econf, encoding='utf-8'):
    if not line.lstrip().startswith('|'):
        continue
    for m in pat.finditer(line):
        cont, names = m.group(1), re.findall(r'`([A-Za-z0-9_]+)`', m.group(2))
        cands = [os.path.join(root, cont), os.path.join(root, cont + '.ty'),
                 os.path.join(root, cont, 'main.ty')]
        blob = ''
        for c in cands:
            if os.path.isfile(c):
                blob += open(c, encoding='utf-8', errors='replace').read()
            elif os.path.isdir(c):
                for dp, _, fs in os.walk(c):
                    for f in fs:
                        blob += open(os.path.join(dp, f), encoding='utf-8', errors='replace').read()
        if not blob:
            continue                     # container itself is the other check's job
        for n in names:
            checked += 1
            if not re.search(r'\b%s\b' % re.escape(n), blob):
                print("spec-check: FAIL -- Appendix E cites `%s` inside `%s`, which does not contain it" % (n, cont), file=sys.stderr)
                bad += 1
print("spec-check: %d Appendix E sub-case citation(s) resolve inside their container (%s)"
      % (checked, "ok" if not bad else "FAILED"))
sys.exit(1 if bad else 0)
ESUB
[ $? -eq 0 ] || fail=1

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
