set -eu

# Fuzz the codecs added during the corelib parity push. Every call below takes
# attacker-controlled bytes: HXA parse, BNG decode, shoco decode, PPMd7/PPMd8,
# PKZIP Shrink/Reduce/Implode, Deflate64 and BCJ2. The harness is one Tycho
# program; ASan+UBSan catch a memory error, a bounds trap and undefined
# behaviour, and the Python mutator is deterministic so a finding reproduces.

cd "$(dirname "$0")/.."
N=${FUZZ_NEWCOMERS_N:-80}
[ "$N" -gt 0 ] || { echo "fuzz-newcomers: FUZZ_NEWCOMERS_N must be > 0" >&2; exit 2; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p build/fuzz-newcomers

command -v python3 >/dev/null 2>&1 || { echo "fuzz-newcomers: SKIPPED (no python3)"; exit 0; }
command -v pkg-config >/dev/null 2>&1 || { echo "fuzz-newcomers: SKIPPED (no pkg-config)"; exit 0; }
pkg-config --exists zlib libpng 2>/dev/null || {
    echo "fuzz-newcomers: SKIPPED (need zlib and libpng)"; exit 0; }
[ -x ./tychoc ] || make tychoc >/dev/null

CC=${CC:-cc}
# --- the control: a real memory error must be caught -------------------------
cat > "$T/ctl.c" <<'EOF'
#include <stdlib.h>
int main(void){ char *p = malloc(4); p[7] = 'x'; free(p); return 0; }
EOF
if ! "$CC" -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 -o "$T/ctl" "$T/ctl.c" 2>/dev/null; then
    echo "fuzz-newcomers: SKIPPED (no sanitizer-capable cc)"; exit 0
fi
if ASAN_OPTIONS=detect_leaks=0 "$T/ctl" >/dev/null 2>&1; then
    echo "fuzz-newcomers: FAILED -- the CONTROL heap overflow was not caught"
    exit 1
fi

# --- the harness -------------------------------------------------------------
cat > "$T/h.ty" <<'EOF'
package main

import "core:compress"
import "core:hxa"
import "core:image"
import "core:io"

fn _u32(b: bytes, o: int) -> int:
    if o < 0 or o + 4 > len(b):
        return 0
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)

fn _want(b: bytes) -> int:
    if len(b) < 4:
        return 1
    n := _u32(b, 0) % 262144
    if n == 0:
        return 1
    return n

fn _hxa(b: bytes) -> int:
    if len(b) > 8192:
        return 0
    match hxa.parse(b):
        Ok(f): return len(f.nodes)
        Err(_): return 0

fn _shoco(b: bytes) -> int:
    match compress.shoco_decompress(b):
        Ok(o): return len(o)
        Err(_): return 0

fn _pp7(b: bytes) -> int:
    if len(b) < 5:
        return 0
    p := compress.PpmdProps(compress.H, 6, 1048576, 0)
    match compress.ppmd_decompress_7z(b[4:], p, _want(b)):
        Ok(o): return len(o)
        Err(_): return 0

fn _ppz(b: bytes) -> int:
    if len(b) < 5:
        return 0
    p := compress.PpmdProps(compress.I, 8, 1048576, 0)
    match compress.ppmd_decompress_zip(b[4:], p, _want(b)):
        Ok(o): return len(o)
        Err(_): return 0

fn _pk(b: bytes) -> int:
    if len(b) < 6:
        return 0
    flags := b[4]
    d := b[5:]
    w := _want(b)
    n := 0
    match compress.unshrink(d, w):
        Ok(o): n += len(o)
        Err(_): n += 0
    match compress.expand(d, 1 + (flags % 4), w):
        Ok(o): n += len(o)
        Err(_): n += 0
    match compress.explode(d, (flags & 2) != 0, (flags & 4) != 0, w):
        Ok(o): n += len(o)
        Err(_): n += 0
    return n

fn _d64(b: bytes) -> int:
    if len(b) < 5:
        return 0
    match compress.deflate64_decompress(b[4:], _want(b)):
        Ok(o): return len(o)
        Err(_): return 0

fn _bcj2(b: bytes) -> int:
    if len(b) < 13:
        return 0
    rest := len(b) - 12
    ml := _u32(b, 0) % (rest + 1)
    cl := _u32(b, 4) % (rest - ml + 1)
    jl := _u32(b, 8) % (rest - ml - cl + 1)
    off := 12
    main := b[off:off + ml]
    call := b[off + ml:off + ml + cl]
    jump := b[off + ml + cl:off + ml + cl + jl]
    rc := b[off + ml + cl + jl:len(b)]
    match compress.bcj2_decode(main, call, jump, rc):
        Ok(o): return len(o)
        Err(_): return 0

fn _bng(b: bytes) -> int:
    if len(b) < 16:
        return 0
    w := _u32(b, 8)
    h := _u32(b, 12)
    if w <= 0 or h <= 0 or w > 2048 or h > 2048:
        return 0
    match image.decode_bng(b):
        Ok(o): return len(o.pixels)
        Err(_): return 0

fn main():
    match io.read_bytes(args()[1]):
        Ok(b):
            n := _hxa(b) + _shoco(b) + _pp7(b) + _ppz(b) + _pk(b) + _d64(b) + _bcj2(b) + _bng(b)
            println(str(n))
        Err(_):
            println("unreadable")
EOF

./tychoc "$T/h.ty" --emit-c -o "$T/h" > "$T/emit.log" 2>&1 || {
    echo "fuzz-newcomers: FAILED (the harness does not compile)"; tail -3 "$T/emit.log"; exit 1; }
SHIMS=$(./tychoc "$T/h.ty" --print-shims 2>/dev/null | tr '\n' ' ')
# $SHIMS unquoted on purpose: /bin/sh word-splits this path list.
# shellcheck disable=SC2086
"$CC" -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 \
   -o "$T/fz" "$T/h.c" $SHIMS \
   $(pkg-config --cflags --libs zlib libpng) -lm -lpthread 2>"$T/cc.log" || {
    echo "fuzz-newcomers: SKIPPED (cannot link the sanitized harness)"; head -3 "$T/cc.log"; exit 0; }

python3 - "$T" "$N" <<'PY'
import os, pathlib, random, struct, subprocess, sys

T, N = sys.argv[1], int(sys.argv[2])
root = pathlib.Path("corelib/test")
def rb(p): return (root / p).read_bytes()

bcj2_main = bytes.fromhex("e890e9900f849090554889e5e8c3e890e9")
bcj2_call = bytes.fromhex("000000060000002d00000023")
bcj2_jump = bytes.fromhex("0000000a0000001400000029")
bcj2_rc   = bytes.fromhex("00fbdffc00")

seeds = [
    b"",
    rb("hxa/sample.hxa"),
    rb("image/opaque_raw.bng"),
    rb("image/opaque.bng"),
    rb("image/testcard.bng"),
    bytes.fromhex("ca296f778e6c64549420717569636b2062df966e20666f78206a756d7073206f7680209065206c617a7920ba67"),
    struct.pack("<I", 102400) + rb("compress_legacy/ppmd7.raw"),
    struct.pack("<I", 912) + rb("compress_legacy/ppmd8.raw"),
    struct.pack("<I", 76) + b"\x00" + rb("compress_legacy/shrink.raw"),
    struct.pack("<I", 818) + b"\x00" + rb("compress_legacy/implode4.raw"),
    struct.pack("<I", 11560) + b"\x06" + rb("compress_legacy/implode8.raw"),
    struct.pack("<I", 76) + b"\x03" + rb("compress_legacy/reduce.raw"),
    struct.pack("<I", 144060) + rb("compress_legacy/deflate64.raw"),
    struct.pack("<III", len(bcj2_main), len(bcj2_call), len(bcj2_jump))
        + bcj2_main + bcj2_call + bcj2_jump + bcj2_rc,
]

random.seed(int(os.environ.get("FUZZ_SEED", "20261112")))
blob = pathlib.Path(T + "/in.bin")
env = dict(os.environ); env["ASAN_OPTIONS"] = "detect_leaks=0"
bad = runs = 0; kinds = {}
for base in seeds:
    for _ in range(N):
        b = bytearray(base if base else os.urandom(random.randint(0, 64)))
        for _ in range(random.randint(1, 8)):
            if not b:
                b = bytearray(os.urandom(8))
            op = random.choice("fdi")
            i = random.randrange(len(b))
            if op == "f":
                b[i] ^= 1 << random.randrange(8)
            elif op == "d":
                del b[i]
            else:
                b.insert(i, random.randrange(256))
        if len(b) > 65536:
            del b[65536:]
        blob.write_bytes(bytes(b)); runs += 1
        try:
            r = subprocess.run([T + "/fz", str(blob)], capture_output=True,
                               text=True, timeout=30, env=env)
        except subprocess.TimeoutExpired:
            bad += 1; kinds["TIMEOUT"] = kinds.get("TIMEOUT", 0) + 1
            pathlib.Path(f"build/fuzz-newcomers/finding_{bad}.bin").write_bytes(bytes(b)); continue
        if r.returncode != 0:
            bad += 1
            k = "ASan/UBSan" if ("ERROR:" in r.stderr or "runtime error" in r.stderr) else f"exit {r.returncode}"
            kinds[k] = kinds.get(k, 0) + 1
            pathlib.Path(f"build/fuzz-newcomers/finding_{bad}.bin").write_bytes(bytes(b))
            if bad <= 2:
                print("  FINDING:", k)
                print("   ", "\n    ".join(r.stderr.strip().split("\n")[:4]))
print(f"  {runs} mutated inputs, {bad} failures")
pathlib.Path(T + "/runs").write_text(str(runs))
for k, v in sorted(kinds.items()): print(f"    {v:4d}  {k}")
sys.exit(1 if bad else 0)
PY
rc=$?
[ "$rc" -eq 0 ] || { echo "fuzz-newcomers: FAIL"; exit 1; }
runs=$(cat "$T/runs" 2>/dev/null || echo "?")
echo "fuzz-newcomers: green (control overflow caught first, then $runs mutated inputs through hxa.parse, image.decode_bng, shoco_decompress, ppmd_decompress_7z/zip, unshrink/expand/explode, deflate64_decompress and bcj2_decode under ASan+UBSan with no memory error, no UB and no hang)"
