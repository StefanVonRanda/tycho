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

cat > "$TMP/want" <<'EOF'
tests/reject/hex_escape_one_digit.ty
tests/reject/rawstring_unterminated.ty
EOF

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

if [ "$fail" -ne 0 ]; then echo "editors-check: FAIL"; exit 1; fi
echo "editors-check: ok"
