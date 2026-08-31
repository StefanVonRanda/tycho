set -eu
cd "$(dirname "$0")/.."
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
TYCHOC="${TYCHOC:-./tychoc1}"
fail=0
bad() { echo "image-ceiling: FAIL -- $*" >&2; fail=1; }

command -v python3 >/dev/null 2>&1 || { echo "image-ceiling: SKIPPED (no python3 to build the fixtures)"; exit 0; }
pkg-config --exists libpng 2>/dev/null || { echo "image-ceiling: SKIPPED (libpng absent -- core:image is skipped too)"; exit 0; }
[ -x "$TYCHOC" ] || make -s tychoc >/dev/null

python3 - "$T/real.png" "$T/bomb.png" <<'PY'
import zlib, struct, sys
def chunk(t, d):
    c = t + d
    return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
def png(path, w, h, raw):
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
# A real 200x200 RGBA image: 160000 bytes decoded, well under the ceiling.
png(sys.argv[1], 200, 200,
    b"".join(b"\x00" + bytes([(x*7) % 256, (x*3) % 256, x % 256, 255]) * 200 for x in range(200)))
# The bomb: the header alone asks for 30000*30000*4 = 3.6 GB. The IDAT is 100 bytes.
png(sys.argv[2], 30000, 30000, b"\x00" * 100)
PY

cat > "$T/m.ty" <<'TY'
package main
import "core:io"
import "core:image"
fn main():
    match io.read_bytes(args()[1]):
        Ok(b):
            match image.decode(b):
                Ok(im): println("ok " + str(im.width) + "x" + str(im.height))
                Err(image.TooBig): println("toobig")
                Err(e): println("other")
        Err(e): println("unreadable")
TY

"$TYCHOC" "$T/m.ty" --emit-c -o "$T/g" >"$T/emit.log" 2>&1 || {
    echo "image-ceiling: FAIL -- could not emit C" >&2; tail -3 "$T/emit.log" >&2; exit 1; }
# $SH unquoted on purpose: it is a LIST of shim paths, and this script runs under
# /bin/sh which word-splits it. Under zsh it would arrive as one argument and the
# link fails naming a single impossible path -- that cost a debug round here too.
SH=$("$TYCHOC" "$T/m.ty" --print-shims 2>/dev/null | tr '\n' ' ')
PNGF=$(pkg-config --cflags --libs libpng)
cc -O2 -o "$T/dflt" "$T/g.c" $SH $PNGF -lm -lpthread 2>"$T/c1.log" || {
    echo "image-ceiling: FAIL -- default build" >&2; tail -3 "$T/c1.log" >&2; exit 1; }
cc -O2 -DIMG_MAX_OUT=1000 -o "$T/tiny" "$T/g.c" $SH $PNGF -lm -lpthread 2>"$T/c2.log" || {
    echo "image-ceiling: FAIL -- forced-ceiling build" >&2; tail -3 "$T/c2.log" >&2; exit 1; }

b_dflt=$("$T/dflt" "$T/bomb.png" 2>&1 || true)
r_dflt=$("$T/dflt" "$T/real.png" 2>&1 || true)
r_tiny=$("$T/tiny" "$T/real.png" 2>&1 || true)

[ "$b_dflt" = "toobig" ] || bad "[1] a 69-byte PNG declaring 3.6 GB was not refused at the default ceiling (got '$b_dflt')"
case "$r_dflt" in ok\ 200x200) ;; *) bad "[2] a real 200x200 PNG was not accepted at the default ceiling (got '$r_dflt')";; esac
[ "$r_tiny" = "toobig" ] || bad "[3] the same real PNG was not refused with the ceiling forced to 1000 bytes (got '$r_tiny')"
[ "$b_dflt" != "$r_dflt" ] || bad "[4] the bomb and the real image gave the SAME answer -- the ceiling decides nothing"

[ "$fail" -eq 0 ] || exit 1
echo "image-ceiling: ok (3.6 GB header refused from 69 bytes; a real image still decodes; the ceiling is what decides)"
