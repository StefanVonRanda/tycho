#!/bin/sh
# Check that emit.ty's f-string hole re-tokenization carries the file.
#
# Two sites in compiler/emit/emit.ty re-tokenize f-string hole text with
# lex.tokenize() (no file), so if parse.expr() triggers die() through those
# tokens, _where() falls back to the bare `tychoc1: line N:` form:
#
#   compiler/emit/emit.ty:2568 in _fstr  -- C emission re-tokenizes hole text
#   compiler/emit/emit.ty:2728 in _fv    -- free-variable collection re-tokenizes
#
# This gate fails while the gap exists and passes when the sites are fixed
# to use a file-carrying tokenize variant (e.g. lex.tokenize_file() or
# stamping the file on the last token as parse.ty@_hole does).

cd "$(dirname "$0")/.." || exit 1
rc=0

# Count bare lex.tokenize() calls in emit.ty. Every call must either use
# tokenize_file (which carries the file) or stamp the file on the K_EOF
# token afterward. A bare lex.tokenize() in an f-string hole path means
# the diagnostic would lack the file.
n_bare=$(grep -c 'lex.tokenize(' compiler/emit/emit.ty | tr -d '[:space:]')
n_file=$(grep -c 'lex.tokenize_file(' compiler/emit/emit.ty | tr -d '[:space:]')
n_total=$((n_bare - n_file))

if [ "$n_total" -gt 0 ]; then
    echo "fstr-hole-file-check: $n_total bare lex.tokenize() call(s) in emit.ty lack a file"
    grep -n 'lex.tokenize(' compiler/emit/emit.ty | grep -v 'lex.tokenize_file(' | sed 's/^/  /'
    echo "  These should carry the file so _where() prints file:line: error: ..."
    rc=1
fi

[ "$rc" = 0 ] && echo "fstr-hole-file-check: all green" || echo "fstr-hole-file-check: FAILED"
exit $rc
