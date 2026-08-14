#!/bin/sh
# Fuzz the corelib entry points that parse UNTRUSTED BYTES, under ASan+UBSan.
#
# The tree's fuzzer (fuzz/run.py) targets the COMPILER: it generates Tycho source
# and checks tychoc. Nothing fuzzed the other direction -- the hand-written C
# shims that a running program feeds attacker-controlled data to. That gap was
# named by docs/internals/ffi-review-2026-08-14.md and this closes it.
#
# Subjects: compress.decompress (zlib), regex.compile/is_match (POSIX regex), and
# json.parse / csv.parse -- the corelib paths that take arbitrary bytes from
# outside. The first two hand them to a C library; the last two are Tycho parsers
# over text, reached through read_text so INVALID UTF-8 gets in as well. Seeds are valid gzip/zlib streams and near-misses; each is mutated by
# bit flips, deletions and insertions, so the interesting inputs are the ones that
# LOOK valid for a while and then are not.
#
# TWO THINGS THIS SCRIPT GETS RIGHT, both of which cost a debugging round:
#
#   detect_leaks=0 -- a Tycho program exits with live arenas BY DESIGN, so
#   LeakSanitizer reports every single run and the harness scored 700/700
#   "crashes" that were nothing. Leaks are not the class being fuzzed here;
#   memory ERRORS are.
#
#   A CONTROL runs first. A fuzzer that reports zero findings is indistinguishable
#   from a fuzzer that is not running, so a deliberate heap overflow is compiled
#   and must be caught before any real input is tried.
#
#   N=<count> sh scripts/fuzz_shims.sh   inputs per seed (default 150)
set -eu

cd "$(dirname "$0")/.."
N=${N:-150}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

command -v python3 >/dev/null 2>&1 || { echo "fuzz-shims: SKIPPED (no python3)"; exit 0; }
[ -x ./tychoc ] || make tychoc >/dev/null

# --- the control: a real memory error must be caught -------------------------
cat > "$T/ctl.c" <<'EOF'
#include <stdlib.h>
int main(void){ char *p = malloc(4); p[7] = 'x'; free(p); return 0; }
EOF
if ! cc -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 -o "$T/ctl" "$T/ctl.c" 2>/dev/null; then
    echo "fuzz-shims: SKIPPED (no sanitizer-capable cc)"; exit 0
fi
if ASAN_OPTIONS=detect_leaks=0 "$T/ctl" >/dev/null 2>&1; then
    echo "fuzz-shims: FAILED -- the CONTROL heap overflow was not caught, so a clean"
    echo "            run below would prove nothing. Sanitizers are not active."
    exit 1
fi

# --- the harness -------------------------------------------------------------
cat > "$T/h.ty" <<'EOF'
package main

import "core:io"
import "core:compress"
import "core:regex"
import "core:json"
import "core:csv"
import "core:zip"
import "core:markdown"

fn main():
    match io.read_bytes(args()[1]):
        Ok(b):
            n := 0
            match compress.decompress(b):
                Ok(o): n = len(o)
                Err(e): n = 0 - 1
            # A regex PATTERN built from the blob, then matched against itself.
            p := ""
            i := 0
            for i < len(b) and i < 200:
                p = p + str(b[i] % 10)
                i = i + 1
            re := regex.compile(p)
            s := str(n)
            if regex.ok(re):
                s = s + " m=" + str(regex.is_match(re, p))
            # The blob AS TEXT through the two text parsers. Both take untrusted
            # input in every real program that uses them and neither had ever been
            # fuzzed. read_text rather than the bytes, so invalid UTF-8 reaches the
            # parser too -- that is the interesting half.
            # zip.list parses an ARCHIVE STRUCTURE out of the blob: le16/le32 read
            # at offsets the file itself supplies, and find_eocd scans backwards
            # for a signature. That is offset-confusion territory and the reason
            # it is fuzzed as bytes rather than text.
            s = s + " z=" + str(len(zip.list(b)))
            match io.read_text(args()[1]):
                Ok(t):
                    # json.parse returns a Json, not a Result -- a failure comes
                    # back as a value. Classifying it is not the point here; the
                    # point is that the parser RUNS over hostile bytes without a
                    # memory error, so one arm is enough to keep the call live.
                    match json.parse(t):
                        json.JNull: s = s + " j=null"
                        _: s = s + " j=val"
                    s = s + " c=" + str(len(csv.parse(t)))
                    s = s + " md=" + str(len(markdown.render(t)))
                Err(e): s = s + " t=err"
            println(s)
        Err(e): println("unreadable")
EOF
./tychoc "$T/h.ty" --emit-c -o "$T/h" > "$T/emit.log" 2>&1 || {
    echo "fuzz-shims: FAILED (the harness does not compile)"; tail -3 "$T/emit.log"; exit 1; }
SHIMS=$(./tychoc "$T/h.ty" --print-shims 2>/dev/null | tr '\n' ' ')
# $SHIMS unquoted on purpose: a LIST of paths, word-split by /bin/sh. zsh would
# NOT split it and the link fails with "cannot find <all of them as one name>".
# shellcheck disable=SC2086
cc -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 \
   -o "$T/fz" "$T/h.c" $SHIMS -lz -lm -lpthread 2>"$T/cc.log" || {
    echo "fuzz-shims: SKIPPED (cannot link the sanitized harness)"; head -3 "$T/cc.log"; exit 0; }

python3 - "$T" "$N" <<'PY'
import gzip, io, os, pathlib, random, subprocess, sys, zipfile

def _real_zip():
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr("a.txt", "hi")
        z.writestr("d/b.bin", b"\x00\x01\x02" * 40)
    return buf.getvalue()
T, N = sys.argv[1], int(sys.argv[2])
random.seed(20260815)                       # deterministic: a finding is reproducible
blob = pathlib.Path(T + "/in.bin")
env = dict(os.environ); env["ASAN_OPTIONS"] = "detect_leaks=0"
# Binary seeds for the zlib path, TEXT seeds for the json/csv path. Without the
# text ones those two parsers only ever see gzip garbage, which they reject at the
# first byte -- deep parser state is never reached and the coverage is a fiction.
seeds = [b"", b"\x1f\x8b", gzip.compress(b"hello"), gzip.compress(b"x" * 100000),
         b"\x1f\x8b\x08\x00" + b"\xff" * 40, b"\x78\x9c" + b"\x00" * 20,
         b'{"a":[1,2,{"b":"c"}],"d":null,"e":1.5e3,"f":true}',
         b'{"deep":' + b"[" * 40 + b"1" + b"]" * 40 + b"}",
         b'{"s":"\\u00e9\\ud83c\\udf89 \\" \\\\ \\n"}',
         b"a,b,c\n1,\"quoted, comma\",3\n\"multi\nline\",x,y\n",
         b",,,\n\"\"\"\",,\n",
         # A REAL zip, built by zipfile. A hand-written EOCD record was tried
         # first and PROVED SHALLOW: zip.list returned 0 entries for it, exactly
         # as it does for pure junk, so every mutant died before the offset
         # arithmetic that is the whole reason to fuzz this. Measured, not assumed
         # -- the same trap the JSON seeds hit one commit earlier.
         _real_zip(),
         b"# h1\n\n*em* **strong** `code`\n\n- a\n- b\n\n[l](http://x) ![i](y)\n\n> q\n\n```\nfence\n```\n"]
bad = runs = 0; kinds = {}
for base in seeds:
    for _ in range(N):
        b = bytearray(base if base else os.urandom(random.randint(0, 64)))
        for _ in range(random.randint(1, 8)):
            if not b: b = bytearray(os.urandom(8))
            op = random.choice("fdi"); i = random.randrange(len(b))
            if op == "f":   b[i] ^= 1 << random.randrange(8)
            elif op == "d": del b[i]
            else:           b.insert(i, random.randrange(256))
        blob.write_bytes(bytes(b)); runs += 1
        try:
            r = subprocess.run([T + "/fz", str(blob)], capture_output=True,
                               text=True, timeout=60, env=env)
        except subprocess.TimeoutExpired:
            bad += 1; kinds["TIMEOUT"] = kinds.get("TIMEOUT", 0) + 1
            pathlib.Path(T + f"/finding_{bad}.bin").write_bytes(bytes(b)); continue
        if r.returncode != 0:
            bad += 1
            k = "ASan/UBSan" if ("ERROR:" in r.stderr or "runtime error" in r.stderr) else f"exit {r.returncode}"
            kinds[k] = kinds.get(k, 0) + 1
            pathlib.Path(T + f"/finding_{bad}.bin").write_bytes(bytes(b))
            if bad <= 2:
                print("  FINDING:", k)
                print("   ", "\n    ".join(r.stderr.strip().split("\n")[:4]))
print(f"  {runs} mutated inputs, {bad} failures")
pathlib.Path(T + "/runs").write_text(str(runs))   # the verdict quotes THIS, not N*seeds
for k, v in sorted(kinds.items()): print(f"    {v:4d}  {k}")
sys.exit(1 if bad else 0)
PY
rc=$?
[ "$rc" -eq 0 ] || { echo "fuzz-shims: FAIL"; exit 1; }
# Read the count back rather than recomputing N * <number of seeds>: that product
# was already wrong once, when seeds were added and the multiplier was not.
runs=$(cat "$T/runs" 2>/dev/null || echo "?")
echo "fuzz-shims: green (control overflow caught first, then $runs mutated inputs through compress.decompress, regex.compile/is_match, json.parse, csv.parse, zip.list and markdown.render under ASan+UBSan with no memory error, no UB and no hang)"
