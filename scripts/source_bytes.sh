#!/bin/sh
# What a source file's BYTES mean to the two lexers, which no fixture can carry:
# a CRLF file and a NUL byte cannot be committed as `tests/*.ty` without git's
# eol normalisation or a binary blob in the corpus, so the inputs are written
# here at run time. Both compilers are scored, and they must AGREE.
#
#   [1] a CRLF file compiles and prints what its LF twin prints -- before this,
#       a blank "\r\n" line was not blank, every empty line inside a block
#       emitted DEDENTs, and the file was refused at the next statement.
#   [2] the LF twin is the control: [1] alone passes on a lexer that ignores
#       indentation entirely.
#   [3] a NUL byte is REFUSED by both. tychoc's lexer stops at the first NUL,
#       so it used to compile a truncated program with exit 0 -- the rest of the
#       file silently discarded, which is the dangerous half of this pair.
#   [4] the NUL-free twin is the control for [3]: it must be ACCEPTED.
cd "$(dirname "$0")/.." || exit 1
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
rc=0
TYCHOC="${TYCHOC:-./tychoc}"
TYCHOC1="${TYCHOC1:-./tychoc1}"
[ -x "$TYCHOC" ]  || { echo "source-bytes: no $TYCHOC -- run make tychoc"; exit 1; }
[ -x "$TYCHOC1" ] || { echo "source-bytes: no $TYCHOC1 -- run make tychoc1"; exit 1; }

# Each probe gets its OWN directory: tychoc compiles every sibling .ty.
mkdir -p "$T/crlf" "$T/lf" "$T/nul" "$T/txt"
printf 'package main\r\n\r\nfn main():\r\n    x := 1\r\n\r\n    print(str(x + 41))\r\n' > "$T/crlf/m.ty"
printf 'package main\n\nfn main():\n    x := 1\n\n    print(str(x + 41))\n'             > "$T/lf/m.ty"
printf 'package main\n\nfn main():\n    print("a")\n\000    print("b")\n'               > "$T/nul/m.ty"
printf 'package main\n\nfn main():\n    print("a")\n    print("b")\n'                   > "$T/txt/m.ty"

for d in crlf lf; do
    if ! "$TYCHOC" "$T/$d/m.ty" -o "$T/$d/m" >/dev/null 2>"$T/e"; then
        echo "  FAIL [$d] tychoc refused a well-formed file :: $(head -1 "$T/e")"; rc=1; continue
    fi
    got="$("$T/$d/m")"
    [ "$got" = 42 ] || { echo "  FAIL [$d] printed '$got', expected 42"; rc=1; }
    "$TYCHOC1" "$T/$d/m.ty" --parse >/dev/null 2>"$T/e1" \
        || { echo "  FAIL [$d] tychoc1 refused it :: $(head -1 "$T/e1")"; rc=1; }
done
echo "leg1/2 crlf + lf twin: both compile, both print 42, both parse under tychoc1"

for c in tychoc tychoc1; do
    case $c in tychoc) bin=$TYCHOC; args="--emit-c -o $T/nul/out";; *) bin=$TYCHOC1; args=--parse;; esac
    # shellcheck disable=SC2086
    if "$bin" "$T/nul/m.ty" $args >/dev/null 2>&1; then
        echo "  FAIL [nul] $c ACCEPTED a source file containing a NUL byte"; rc=1
    fi
    case $c in tychoc) args="--emit-c -o $T/txt/out";; *) args=--parse;; esac
    # shellcheck disable=SC2086
    "$bin" "$T/txt/m.ty" $args >/dev/null 2>&1 \
        || { echo "  FAIL [nul-control] $c refused the same file without the NUL"; rc=1; }
done
echo "leg3/4 nul byte: refused by both, and the NUL-free twin accepted by both"

[ $rc = 0 ] && echo "source-bytes: all green" || echo "source-bytes: FAILED"
exit $rc
