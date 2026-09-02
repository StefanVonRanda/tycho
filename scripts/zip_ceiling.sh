set -eu
cd "$(dirname "$0")/.."
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
TYCHOC="${TYCHOC:-./tychoc1}"
fail=0
bad() { echo "zip-ceiling: FAIL -- $*" >&2; fail=1; }

# core:zip is the DOCUMENTED archive reader and had no bomb leg: tools/tycho-ar
# has one, but it goes through compress.decompress (gzip), and zip entries are
# RAW deflate -- a different function, and the one that was missing the ceiling.
# zip.ty compares against the declared usize only AFTER the inflate has run.
command -v python3 >/dev/null 2>&1 || { echo "zip-ceiling: SKIPPED (no python3 to build the fixtures)"; exit 0; }
[ -x "$TYCHOC" ] || make -s tychoc >/dev/null

python3 - "$T/real.zip" "$T/bomb.zip" <<'PY'
import sys, zipfile
# A normal archive: the positive control. A refusal that also breaks this proves
# nothing, which is why it is scored separately below.
with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("hello.txt", b"hello zip\n")
# The bomb: 8 MB of zeros deflates to a few KB and blows any small ceiling.
with zipfile.ZipFile(sys.argv[2], "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    z.writestr("bomb.bin", b"\0" * (8 * 1024 * 1024))
PY

cat > "$T/m.ty" <<'TY'
package main
import "core:io"
import "core:zip"
fn main():
    match io.read_bytes(args()[1]):
        Ok(b):
            d := zip.extract(b, args()[2])
            if len(d) == 0:
                println("refused")
            else:
                println("out " + str(len(d)))
        Err(e): println("unreadable")
TY

"$TYCHOC" "$T/m.ty" --emit-c -o "$T/g" >"$T/emit.log" 2>&1 || {
    echo "zip-ceiling: FAIL -- could not emit C" >&2; tail -3 "$T/emit.log" >&2; exit 1; }
# $SH unquoted on purpose: it is a LIST of shim paths and this runs under /bin/sh.
SH=$("$TYCHOC" "$T/m.ty" --print-shims 2>/dev/null | tr '\n' ' ')
cc -O2 -o "$T/dflt" "$T/g.c" $SH -lz -lm -lpthread 2>"$T/c1.log" || {
    echo "zip-ceiling: FAIL -- default build" >&2; tail -3 "$T/c1.log" >&2; exit 1; }
cc -O2 -DZD_MAX_OUT=65536 -o "$T/tiny" "$T/g.c" $SH -lz -lm -lpthread 2>"$T/c2.log" || {
    echo "zip-ceiling: FAIL -- forced-ceiling build" >&2; tail -3 "$T/c2.log" >&2; exit 1; }

r_dflt=$("$T/dflt" "$T/real.zip" hello.txt 2>&1 || true)
b_tiny=$("$T/tiny" "$T/bomb.zip" bomb.bin 2>&1 || true)
r_tiny=$("$T/tiny" "$T/real.zip" hello.txt 2>&1 || true)
b_dflt=$("$T/dflt" "$T/bomb.zip" bomb.bin 2>&1 || true)

[ "$r_dflt" = "out 10" ]  || bad "[1] a normal zip entry did not extract at the default ceiling (got '$r_dflt')"
[ "$b_tiny" = "refused" ] || bad "[2] an 8 MB expansion was not refused with the ceiling forced to 64 KiB (got '$b_tiny') -- the raw-inflate ceiling does not fire"
[ "$r_tiny" = "out 10" ]  || bad "[3] the ceiling refused a 10-byte entry too -- it is refusing everything, so [2] proves nothing (got '$r_tiny')"
[ "$b_dflt" = "out 8388608" ] || bad "[4] the same bomb is not accepted under the 1 GiB default (got '$b_dflt') -- then [2] is not the ceiling deciding"
[ "$b_tiny" != "$r_tiny" ] || bad "[5] the bomb and the normal entry gave the SAME answer at 64 KiB -- the ceiling decides nothing"

[ "$fail" -eq 0 ] || exit 1
echo "zip-ceiling: ok (8 MB raw-deflate entry refused at a forced ceiling; a normal entry still extracts at both ceilings; the ceiling is what decides)"
