set -u
cd "$(dirname "$0")/.." || exit 2

TS="${TYCHO_TREE_SITTER:-npx --yes tree-sitter-cli@0.25}"
GDIR=editors/zed/grammars/tycho

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
fail=0

echo ">>> editors: JSON syntax (vscode)"
for j in editors/vscode/syntaxes/tycho.tmLanguage.json \
         editors/vscode/language-configuration.json; do
    if python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$j" 2>"$TMP/json.err"; then
        echo "    ok  $j"
    else
        echo "    INVALID JSON: $j"; sed 's/^/      /' "$TMP/json.err"; fail=1
    fi
done

# The corpus file list, computed ONCE and reused by both the README lane below
# and the CORPUS lane at the bottom. Deliberately the same list for both, so the
# number the README is checked against is the number the parser actually ran on.
find "$PWD" -name '*.ty' -not -path '*/.git/*' -not -path '*/node_modules/*' \
     -not -path "$PWD/fuzz/findings/*" | sort > "$TMP/files"
nfiles=$(wc -l < "$TMP/files" | tr -d ' ')

echo ">>> editors: zed README corpus claim"
claim='every tracked `.ty` file'
if grep -qF "$claim" editors/zed/README.md; then
    echo "    ok  claim present; tree has $nfiles .ty files (reported, not asserted)"
else
    echo "    CLAIM MISSING from editors/zed/README.md -- expected the phrase"
    echo "    \"$claim\". Reword it and this lane must be updated in step."
    fail=1
fi

# ---------------------------------------------------------------- tree-sitter
if ! $TS --version >"$TMP/ver" 2>/dev/null; then
    echo ">>> editors: zed grammar SKIPPED (tree-sitter CLI unavailable: '$TS --version' failed -- offline, or no npx. The JSON lane above still ran.)"
    if [ "$fail" -ne 0 ]; then echo "editors-check: FAIL"; exit 1; fi
    echo "editors-check: ok (grammar lanes skipped)"
    exit 0
fi
echo ">>> editors: zed grammar regenerated with $TS ($(tr -d '\n' < "$TMP/ver"))"

cp "$GDIR/grammar.js" "$GDIR/tree-sitter.json" "$TMP/" || exit 2
if ! (cd "$TMP" && $TS generate --abi 15) >"$TMP/gen.log" 2>&1; then
    echo "    GENERATE FAILED -- grammar.js does not compile"
    sed 's/^/      /' "$TMP/gen.log"
    echo "editors-check: FAIL"; exit 1
fi

# The committed src/ is a build artifact of grammar.js. If they disagree, the
# shipped parser does not implement the grammar in the tree.
if diff -r "$TMP/src" "$GDIR/src" >"$TMP/diff.out" 2>&1; then
    echo "    src/ matches grammar.js byte for byte (parser.c, grammar.json, node-types.json, tree_sitter/)"
else
    echo "    GENERATED src/ IS STALE: $GDIR/src does not match grammar.js."
    echo "    Regenerate: (cd $GDIR && $TS generate --abi 15)"
    sed 's/^/      /' "$TMP/diff.out" | head -20
    fail=1
fi

# The known-bad set is DERIVED from editors/zed/syntax-coverage.tsv, which
# carries a reason for every SYNTAX-class reject fixture -- both the ones this
# grammar refuses and the ones it accepts. One list, one source of truth.
COV=editors/zed/syntax-coverage.tsv
grep -v '^#' "$COV" | awk -F'\t' '$2=="REJECTED"{print $1}' | sort > "$TMP/want"

(cd "$TMP" && $TS parse -q $(cat "$TMP/files")) 2>/dev/null \
    | awk -F'\t' '/ERROR|MISSING/ {print $1}' \
    | sed -e 's/[[:space:]]*$//' -e "s|^$PWD/||" | sort -u > "$TMP/got"
echo ">>> editors: zed grammar over the corpus ($nfiles .ty files)"
if diff "$TMP/want" "$TMP/got" >"$TMP/corpus.diff" 2>&1; then
    echo "    $nfiles files parsed; the only failure is the enumerated known-bad set ($(tr '\n' ' ' < "$TMP/want"))"
else
    echo "    CORPUS PARSE MISMATCH ('<' expected to fail but parsed, '>' newly failing):"
    sed 's/^/      /' "$TMP/corpus.diff"
    fail=1
fi

# ---------------------------------------------- SYNTAX-reject classification
# The corpus lane above proves nothing about tychoc's reject fixtures beyond
# "they still parse the way they did". This leg pins WHY: every SYNTAX-class
# fixture in compiler/reject_class.tsv must appear in the coverage table with a
# verdict that matches a real parse and a non-empty reason. A new SYNTAX fixture
# cannot join the accepted majority unclassified.
awk -F'\t' '$2=="SYNTAX"{print $1}' compiler/reject_class.tsv | sort > "$TMP/syn"
nsyn=$(wc -l < "$TMP/syn" | tr -d ' ')
grep -v '^#' "$COV" | awk -F'\t' '{print $1}' | sort > "$TMP/cov.paths"
echo ">>> editors: SYNTAX-reject classification ($nsyn fixtures)"
if ! diff "$TMP/syn" "$TMP/cov.paths" > "$TMP/cov.diff" 2>&1; then
    echo "    COVERAGE TABLE OUT OF STEP with compiler/reject_class.tsv"
    echo "    ('<' classified but no longer SYNTAX, '>' SYNTAX but unclassified):"
    sed 's/^/      /' "$TMP/cov.diff"
    fail=1
fi
nbad=0
while IFS="$(printf '\t')" read -r path verdict reason; do
    case "$path" in \#*|"") continue;; esac
    if [ -z "$reason" ]; then
        echo "    NO REASON GIVEN: $path"; nbad=$((nbad+1)); continue
    fi
    if grep -qxF "$path" "$TMP/got"; then measured=REJECTED; else measured=ACCEPTED; fi
    if [ "$measured" != "$verdict" ]; then
        echo "    VERDICT MOVED: $path recorded $verdict, grammar now $measured"
        nbad=$((nbad+1))
    fi
done < "$COV"
if [ "$nbad" -ne 0 ]; then
    fail=1
else
    nrej=$(wc -l < "$TMP/want" | tr -d ' ')
    echo "    $nsyn classified, each with a reason: $nrej refused by the grammar, $((nsyn - nrej)) accepted (grammar.js has no external scanner and does no name resolution)"
fi

# ------------------------------------------- keyword coverage, BOTH integrations
# Nothing above can see a keyword the grammars do not KNOW: an unknown word lexes
# as an identifier and the file parses, so `packed` shipped in V2 with both
# grammars reading it as a name and all 1339 corpus files stayed green. This leg
# measures a verdict for every word in surface.lock's lexer set -- the tree-sitter
# node kind it lexes to, and the first tmLanguage pattern that spans it -- and
# requires the table to match. Measured, not read off the grammar source.
KCOV=editors/keyword-coverage.tsv
python3 -c 'import json;print("\n".join(sorted(json.load(open("surface.lock"))["keywords"])))' \
    > "$TMP/kw" || exit 2
nkw=$(wc -l < "$TMP/kw" | tr -d ' ')
# LC_ALL=C: python's sorted() is byte order and this box's locale collation is
# not -- `get` / `getenv` swap places under it, which is a false red.
grep -v '^#' "$KCOV" | awk -F'\t' '{print $1}' | LC_ALL=C sort > "$TMP/kcov.words"
echo ">>> editors: keyword coverage ($nkw lexer words x 2 integrations)"
if ! diff "$TMP/kw" "$TMP/kcov.words" > "$TMP/kcov.diff" 2>&1; then
    echo "    COVERAGE TABLE OUT OF STEP with surface.lock"
    echo "    ('<' a lexer word nothing classified, '>' classified but no longer a lexer word):"
    sed 's/^/      /' "$TMP/kcov.diff"
    fail=1
fi

cp "$TMP/kw" "$TMP/kw.ty"
(cd "$TMP" && $TS parse kw.ty) 2>/dev/null \
    | sed -n 's/^ *(\([a-z_]*\) \[.*/\1/p' | tail -n +2 > "$TMP/kzed"
python3 - "$TMP/kw" > "$TMP/kvsc" <<'PY' || exit 2
import json,re,sys
tm = json.load(open("editors/vscode/syntaxes/tycho.tmLanguage.json"))
for w in open(sys.argv[1]).read().split():
    hit = "none"
    for p in tm["patterns"]:
        if "match" not in p or "name" not in p: continue
        m = re.search(p["match"], w)
        if m and m.start() == 0 and m.end() == len(w):
            hit = p["name"]; break
    print(hit)
PY
if [ "$(wc -l < "$TMP/kzed")" -ne "$nkw" ] || [ "$(wc -l < "$TMP/kvsc")" -ne "$nkw" ]; then
    echo "    MEASUREMENT FAILED: got $(wc -l < "$TMP/kzed") zed and $(wc -l < "$TMP/kvsc") vscode verdicts for $nkw words"
    fail=1
else
    paste "$TMP/kw" "$TMP/kzed" "$TMP/kvsc" > "$TMP/kmeasured"
    grep -v '^#' "$KCOV" | awk -F'\t' '{print $1"\t"$2"\t"$3}' | LC_ALL=C sort > "$TMP/krecorded"
    if diff "$TMP/kmeasured" "$TMP/krecorded" > "$TMP/kverdict.diff" 2>&1; then
        nhl=$(awk -F'\t' '$2!="identifier"' "$TMP/kmeasured" | wc -l | tr -d ' ')
        echo "    $nkw words measured, all matching the table: $nhl highlighted, $((nkw - nhl)) lexing as ordinary identifiers"
    else
        echo "    VERDICT MOVED ('<' measured now, '>' recorded in $KCOV):"
        sed 's/^/      /' "$TMP/kverdict.diff"
        fail=1
    fi
fi
if grep -v '^#' "$KCOV" | awk -F'\t' 'NF<4 || $4==""{print "    NO REASON GIVEN: "$1}' | grep .; then
    fail=1
fi

if [ "$fail" -ne 0 ]; then echo "editors-check: FAIL"; exit 1; fi
echo "editors-check: ok"
