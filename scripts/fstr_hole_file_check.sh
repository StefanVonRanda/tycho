#!/bin/sh
# A lex error INSIDE an f-string hole must name the file and the f-string's
# own line.
#
# A hole's text is re-tokenized as a FRAGMENT, and a fragment carries no name
# and numbers its own lines from 0, so `f"{1 @ 2}"` used to die as
#
#     tychoc1: line 1: unexpected character '@'
#
# -- no path, and line 1 of the fragment rather than the line the f-string is
# on. Three sites re-tokenize hole text: parse/parse.ty@_hole, and emit's
# @_fstr and @_fv. All three go through lex.tokenize_hole now.
#
# This gate is BEHAVIOURAL on purpose. The version it replaces grepped
# emit.ty for `lex.tokenize(` and counted; that could not see the parse.ty
# site at all -- which is the one that actually raised the diagnostic -- and
# could not tell whether a call it did see produced a located error.

cd "$(dirname "$0")/.." || exit 1
TYCHOC=${TYCHOC:-./tychoc1}
[ -x "$TYCHOC" ] || { echo "SKIP  fstr-hole-file-check  ($TYCHOC not built)"; exit 0; }

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT
rc=0
fail() { echo "fstr-hole-file-check: $1"; rc=1; }

# [1] a lex error in a hole names the file and the f-string's line.
#     The `@` is on line 5; nothing else in the file can raise this.
cat > "$tmp/a.ty" <<'TY'
fn main():
    x := 1
    println("pad")
    println("pad")
    println(f"v={x @ 1}")
TY
out=$("$TYCHOC" "$tmp/a.ty" -o "$tmp/a" 2>&1)
echo "$out" | grep -q "unexpected character '@'" \
    || fail "[1] the lex error itself vanished -- got: $out"
echo "$out" | grep -q "a\.ty:5: error:" \
    || fail "[1] hole lex error does not name file:line -- got: $out"
# The exact shape the fix removed. A fragment's own numbering starts at 1.
echo "$out" | grep -q '^tychoc1: line 1:' \
    && fail "[1] still reporting the FRAGMENT's line with no path -- got: $out"

# [2] the control: the same error OUTSIDE a hole was always located, so [1]
#     passing means nothing unless this does too. If this one regresses the
#     problem is not holes.
cat > "$tmp/b.ty" <<'TY'
fn main():
    x := 1
    println("pad")
    println("pad")
    println(x @ 1)
TY
out2=$("$TYCHOC" "$tmp/b.ty" -o "$tmp/b" 2>&1)
echo "$out2" | grep -q "b\.ty:5: error:" \
    || fail "[2] a lex error OUTSIDE a hole is unlocated -- got: $out2"

# [3] a hole that is merely WRONG, not unlexable, still resolves against the
#     enclosing file: the semantic path was never broken and must stay green,
#     or [1] could be "passed" by making every hole fail early.
cat > "$tmp/c.ty" <<'TY'
fn main():
    x := 1
    println(f"v={x.nosuchfield}")
TY
out3=$("$TYCHOC" "$tmp/c.ty" -o "$tmp/c" 2>&1)
echo "$out3" | grep -q "c\.ty:3: error:" \
    || fail "[3] a semantic error in a hole lost its location -- got: $out3"

# [4] a well-formed hole must still COMPILE and RUN. Three legs that all
#     assert a refusal would pass on a compiler that refused every f-string.
cat > "$tmp/d.ty" <<'TY'
fn main():
    x := 41
    println(f"v={x + 1}")
TY
if "$TYCHOC" "$tmp/d.ty" -o "$tmp/d" >/dev/null 2>&1; then
    got=$("$tmp/d" 2>&1)
    [ "$got" = "v=42" ] || fail "[4] a good f-string printed '$got', expected 'v=42'"
else
    fail "[4] a well-formed f-string stopped compiling"
fi

[ "$rc" = 0 ] && echo "fstr-hole-file-check: all green (4 legs)" || echo "fstr-hole-file-check: FAILED"
exit $rc
