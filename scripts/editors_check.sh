#!/bin/sh
# Regression guard for the two editor grammars under editors/.
# Run by `make editors-check` and as a step in `make ci`.
#
# Until 2026-07-29 NOTHING in any gate parsed either grammar, so both could rot
# silently: scripts/tools_check.sh:25@editors excludes ./editors/* from the formatter
# sweep by name, and no other step in scripts/ci.sh mentions the directory. The
# concrete damage that found this: editors/zed/README.md carried a "parses all
# 462 committed .ty files" claim for hundreds of files past the truth (813), and
# editors/zed/grammars/tycho/src/parser.c is a GENERATED artifact that nothing
# checked against its grammar.js -- an edit to the .js alone would ship a parser
# that silently does not implement it.
#
#   JSON       editors/vscode/syntaxes/tycho.tmLanguage.json and
#              editors/vscode/language-configuration.json must be parseable JSON.
#              A typo in either silently disables highlighting in VS Code.
#   README     the "N committed `.ty` files" claim in editors/zed/README.md must
#              equal the corpus size this script computes. Added 2026-07-30: the
#              462 -> 813 repair below was itself hand-typed, so it drifted again
#              (to 829) within a day. A number a human must remember to update is
#              not a verified claim, it is a decaying one.
#   GENERATED  `tree-sitter generate --abi 15` into a temp dir must reproduce the
#              committed editors/zed/grammars/tycho/src/ byte for byte.
#   CORPUS     the generated parser must parse every tracked .ty file, with
#              exactly the enumerated known-bad set below still failing.
#
# The two grammar lanes need the tree-sitter CLI, which is fetched with npx. When
# it is unavailable (offline, no npx, nothing cached) they SKIP rather than fail
# -- same call as Makefile:270@SKIPPED's "ASan lane SKIPPED for ilp32": a gate that
# hard-fails without network access would be worse than no gate. The JSON lane
# needs only python3, which scripts/ci.sh already depends on, so it always runs.
# Override the CLI with e.g. TYCHO_TREE_SITTER='tree-sitter' if you have 0.25
# installed locally; the version matters, a different one regenerates a
# different parser.c.
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

# ------------------------------------------------------------- README corpus N
# editors/zed/README.md states how many .ty files the grammar was verified over.
# It was hand-typed and read "462" while the truth was 813 -- a claim nothing
# checked, in the one file a reader consults to decide whether to trust the
# grammar. Runs BEFORE the tree-sitter availability check on purpose: it needs
# only `find`, so it must not be skipped offline along with the grammar lanes.
echo ">>> editors: zed README corpus count"
rn=$(sed -n 's/.*[^0-9]\([0-9][0-9]*\) committed `\.ty` files.*/\1/p' \
     editors/zed/README.md | head -1)
if [ -z "$rn" ]; then
    echo "    NO COUNT FOUND in editors/zed/README.md -- expected a phrase of the"
    echo "    form 'N committed \`.ty\` files'. The corpus claim is now gated; do not"
    echo "    delete or reword it. The tree currently has $nfiles."
    fail=1
elif [ "$rn" != "$nfiles" ]; then
    echo "    STALE: editors/zed/README.md claims $rn committed .ty files, tree has $nfiles."
    echo "    Fix the README to say $nfiles."
    fail=1
else
    echo "    ok  README says $rn committed .ty files, and so does the tree"
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

# The corpus must parse clean EXCEPT for an enumerated known-bad set.
#
# This is deliberately NOT "one ERROR per tests/reject/ fixture". There are 239
# fixtures in tests/reject/, but they are overwhelmingly SEMANTIC rejects (type
# mismatches, affine violations, arity errors) whose syntax is perfectly
# well-formed and which a highlighting grammar must parse. Exactly one reject
# fixture is a LEXICAL one, and it is the only file in the tree the grammar is
# allowed to fail on. Both directions are asserted: a NEW error is a regression,
# and a known-bad file that starts parsing means the grammar grew a hole (it
# would be accepting an unterminated raw string).
cat > "$TMP/want" <<'EOF'
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
