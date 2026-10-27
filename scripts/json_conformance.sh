#!/bin/sh
# Run core:json against the JSONTestSuite parsing corpus (nst/JSONTestSuite).
# The corpus is not vendored: set JSONTESTSUITE to a checkout, or this skips.
# y_*.json must parse, n_*.json must be refused; i_*.json is implementation-
# defined by the suite and is not scored.
set -u
cd "$(dirname "$0")/.."
D="${JSONTESTSUITE:-}"
if [ ! -d "$D/test_parsing" ]; then
    echo "json-conformance: skip (set JSONTESTSUITE to a JSONTestSuite checkout)"
    exit 0
fi
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "json-conformance: no $TYCHOC -- run make" >&2; exit 2; }
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cat > "$T/h.ty" <<'TY'
package main
import "core:io"
import "core:json"

fn main():
    match io.read_stdin_bytes():
        Ok(b):
            match json.parse_checked(to_str(b)):
                Ok(_): println("y")
                Err(_): println("n")
        Err(_):
            println("e")
TY
"$TYCHOC" "$T/h.ty" -o "$T/h" || exit 2
pass=0; fail=0
for kind in y n; do
    for f in "$D/test_parsing/${kind}_"*.json; do
        got="$("$T/h" < "$f")"
        if [ "$got" = "$kind" ]; then
            pass=$((pass + 1))
        else
            echo "FAIL $(basename "$f") -> $got"
            fail=$((fail + 1))
        fi
    done
done
echo "json-conformance: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
