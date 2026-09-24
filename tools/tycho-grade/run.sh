#!/bin/sh
# make grade-check -- tools/tycho-grade, the tree's worked user of vector[4]f32,
# swizzling and the .r/.g/.b/.a lane names (FRICTION 128, layout 4).
#
# The fixtures and the oracle are python3, not the program: python writes the
# input TGAs with struct.pack and computes every expected output byte itself.
set -u
cd "$(dirname "$0")/../.." || exit 2
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "no $TYCHOC -- run 'make' first"; exit 2; }
command -v python3 >/dev/null 2>&1 || { echo "grade-check: skipped (no python3 for the oracle)"; exit 0; }
RECORD="${RECORD:-0}"
golden="tools/tycho-grade/grade.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
note() { echo "FAIL $1"; fail=1; }
GAIN="1.5,1,0.5,1"
LIFT="-40,10,16,0"

# [0] the build, under whichever compiler the lane substitutes, and under the
# reference compiler too: tychoc could not build this program at first (an
# in-loop recycle of `gain = parse4(...)` emitted `.data` on an inline vector).
$TYCHOC tools/tycho-grade/main.ty -o "$T/grade" > "$T/build.log" 2>&1 || {
    echo "grade-check: FAILED (tycho-grade does not build with $TYCHOC)"; tail -5 "$T/build.log"; exit 1; }
if grep -q 'warning' "$T/build.log"; then note "[0] the build warns"; grep warning "$T/build.log" | head -3; fi
if [ "$TYCHOC" != ./tychoc ] && [ -x ./tychoc ]; then
    ./tychoc tools/tycho-grade/main.ty -o "$T/grade0" > "$T/build0.log" 2>&1 || {
        note "[0] tycho-grade does not build with ./tychoc"; tail -5 "$T/build0.log"; }
fi

# Fixtures and the oracle. a.tga: 4x3, 32bpp, BOTTOM-left origin (rows stored
# last-first). c.tga: the same image at 24bpp, TOP-left origin. The first two
# pixels are chosen by hand for leg [2]; the rest come from a formula.
python3 - "$T" "$GAIN" "$LIFT" <<'PY' || { echo "grade-check: FAILED (fixture script)"; exit 1; }
import struct, sys, math
t, gain, lift = sys.argv[1], [float(x) for x in sys.argv[2].split(',')], [float(x) for x in sys.argv[3].split(',')]
W, H = 4, 3
px = [(200, 100, 50, 255), (10, 250, 255, 128)]
for i in range(2, W * H):
    px.append(((37*i+11) % 256, (59*i+100) % 256, (83*i+7) % 256, (255 - 20*i) % 256))
def hdr(bpp, desc): return struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, W, H, bpp, desc)
rows = [px[y*W:(y+1)*W] for y in range(H)]
a = hdr(32, 8) + b''.join(bytes((b, g, r, al)) for row in reversed(rows) for (r, g, b, al) in row)
c = hdr(24, 32) + b''.join(bytes((b, g, r)) for row in rows for (r, g, b, al) in row)
open(t + '/a.tga', 'wb').write(a)
open(t + '/c.tga', 'wb').write(c)
open(t + '/trunc.tga', 'wb').write(a[:40])
def grade(p):
    out = []
    for ch in range(4):
        v = p[ch] * gain[ch] + lift[ch]
        out.append(0 if v < 0 else 255 if v > 255 else math.floor(v + 0.5))
    return out
def expect(src):
    body = b''
    for (r, g, b, al) in (grade(p) for p in src):
        body += bytes((b, g, r, al))
    return hdr(32, 40) + body
open(t + '/want_a.tga', 'wb').write(expect(px))
open(t + '/want_c.tga', 'wb').write(expect([(r, g, b, 255) for (r, g, b, al) in px]))
PY

# [1] two runs, identical in what they print and what they write, and the
# printout equal to the golden
"$T/grade" --gain "$GAIN" --lift "$LIFT" "$T/a.tga" "$T/o1.tga" > "$T/one.txt" 2>&1 || note "[1] first run exited non-zero"
"$T/grade" --gain "$GAIN" --lift "$LIFT" "$T/a.tga" "$T/o2.tga" > "$T/two.txt" 2>&1 || note "[1] second run exited non-zero"
cmp -s "$T/one.txt" "$T/two.txt" || note "[1] two runs printed different output"
cmp -s "$T/o1.tga" "$T/o2.tga" || note "[1] two runs wrote different files"
if [ "$RECORD" = 1 ]; then
    cp "$T/one.txt" "$golden"; echo "rec  $golden"
else
    cmp -s "$T/one.txt" "$golden" || { note "[1] output differs from the golden"; diff "$golden" "$T/one.txt" | head -10; }
fi

# [2] two pixels worked out by hand, not by the oracle. The output is 32bpp,
# top-left origin, so pixel 0 is at byte 18 and pixel 1 at byte 22, each B,G,R,A.
#   (200,100,50,255): r 200*1.5-40 = 260 -> 255 (clipped)  g 100+10 = 110
#                     b 50*0.5+16 = 41                     a 255
#   (10,250,255,128): r 10*1.5-40 = -25 -> 0 (clipped)     g 250+10 = 260 -> 255
#                     b 255*0.5+16 = 143.5 -> 144 (half up) a 128
got=$(od -An -tu1 -j18 -N8 "$T/o1.tga" | tr -s ' ' | sed 's/^ //; s/ $//')
[ "$got" = "41 110 255 255 144 255 0 128" ] || note "[2] pixels 0-1 are '$got', want '41 110 255 255 144 255 0 128'"
grep -q '^clipped r=' "$T/one.txt" || note "[2] no clip report"

# [3] every byte, header included, against the python oracle -- for the 32bpp
# bottom-up input and for the 24bpp top-down one, which must grade to the same
# file apart from alpha
cmp -s "$T/o1.tga" "$T/want_a.tga" || note "[3] 32bpp bottom-left: output differs from the oracle"
"$T/grade" --gain "$GAIN" --lift "$LIFT" "$T/c.tga" "$T/oc.tga" > /dev/null 2>&1 || note "[3] 24bpp run exited non-zero"
cmp -s "$T/oc.tga" "$T/want_c.tga" || note "[3] 24bpp top-left: output differs from the oracle"

# [4] through core:raster: grade to BMP (R,G,B,A in memory, no swizzle on the
# way out), then read that BMP back with an identity grade and write TGA (no
# swizzle in, one out). Both swizzle directions have to agree for this to match.
"$T/grade" --gain "$GAIN" --lift "$LIFT" "$T/a.tga" "$T/o.bmp" > /dev/null 2>&1 || note "[4] grade to .bmp exited non-zero"
"$T/grade" "$T/o.bmp" "$T/back.tga" > /dev/null 2>&1 || note "[4] .bmp -> .tga exited non-zero"
cmp -s "$T/back.tga" "$T/want_a.tga" || note "[4] tga -> bmp -> tga does not reproduce the graded file"
"$T/grade" --gain "$GAIN" --lift "$LIFT" "$T/a.tga" "$T/o.qoi" > /dev/null 2>&1 || note "[4] grade to .qoi exited non-zero"
"$T/grade" "$T/o.qoi" "$T/backq.tga" > /dev/null 2>&1 || note "[4] .qoi -> .tga exited non-zero"
cmp -s "$T/backq.tga" "$T/want_a.tga" || note "[4] tga -> qoi -> tga does not reproduce the graded file"

# [5] refusals: a truncated TGA and a malformed gain each exit 1 by name, and
# write nothing
"$T/grade" "$T/trunc.tga" "$T/x.tga" > "$T/r1.txt" 2>&1 && note "[5] a truncated TGA was accepted"
grep -q 'truncated' "$T/r1.txt" || note "[5] the truncation refusal does not say so"
"$T/grade" --gain 1,2,x,1 "$T/a.tga" "$T/x.tga" > "$T/r2.txt" 2>&1 && note "[5] a non-numeric gain was accepted"
grep -q "'x' is not a number" "$T/r2.txt" || note "[5] the gain refusal does not name the bad value"
[ -e "$T/x.tga" ] && note "[5] a refused run wrote its output"

[ "$fail" = 0 ] && { echo "grade-check: ok (build x2, deterministic, 2 hand pixels, oracle x2, bmp/qoi round trip, 2 refusals)"; exit 0; }
echo "grade-check: FAILED"; exit 1
