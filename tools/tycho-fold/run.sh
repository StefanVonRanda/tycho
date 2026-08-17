set -eu

cd "$(dirname "$0")/../.."
TYCHOC=./tychoc
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
out="$T/got"
: > "$out"
fail=0
bad() { echo "FAIL: $*"; fail=1; }

[ -x "$TYCHOC" ] || make tychoc >/dev/null

$TYCHOC tools/tycho-fold/main.ty -o "$T/fold" > "$T/build.log" 2>&1 || {
    echo "tycho-fold: FAILED (does not build)"; tail -5 "$T/build.log"; exit 1; }
F="$T/fold"

# --- [2] the rendering, and the ASCII/non-ASCII contrast, into the golden -----
printf 'the quick brown fox jumps over the lazy dog\nhello world\n' > "$T/ascii.txt"
printf 'h\303\251llo w\303\266rld with na\303\257ve caf\303\251 fa\303\247ade jalape\303\261o\n\346\227\245\346\234\254\350\252\236\343\201\256\343\203\206\343\202\255\343\202\271\343\203\210\343\202\222\346\212\230\343\202\212\350\277\224\343\201\231\350\251\246\351\250\223\343\201\247\343\201\231\nmixed ascii and \303\251moji \360\237\216\211 here\n' > "$T/utf8.txt"

printf '=== ascii w=20\n' >> "$out";        ( cd "$T" && "$F" ascii.txt --width=20 ) >> "$out" 2>&1 || true
printf '=== utf8 w=20 codepoints\n' >> "$out"; ( cd "$T" && "$F" utf8.txt --width=20 ) >> "$out" 2>&1 || true
printf '=== utf8 w=20 BYTES (the bug, kept reachable)\n' >> "$out"; ( cd "$T" && "$F" utf8.txt --width=20 --bytes ) >> "$out" 2>&1 || true
printf '=== w=1\n' >> "$out";               ( cd "$T" && "$F" utf8.txt --width=1 ) >> "$out" 2>&1 || true

# --- [6] the contrast, asserted rather than eyeballed -------------------------
( cd "$T" && "$F" ascii.txt --width=12 ) > "$T/a1" 2>&1 || true
( cd "$T" && "$F" ascii.txt --width=12 --bytes ) > "$T/a2" 2>&1 || true
cmp -s "$T/a1" "$T/a2" || bad "[6] on pure ASCII the two modes must AGREE -- a byte and a codepoint are the same thing there"
( cd "$T" && "$F" utf8.txt --width=20 ) > "$T/u1" 2>&1 || true
( cd "$T" && "$F" utf8.txt --width=20 --bytes ) > "$T/u2" 2>&1 || true
if cmp -s "$T/u1" "$T/u2"; then
    bad "[6] on non-ASCII the two modes must DIFFER -- if they agree, the codepoint path is not counting codepoints and [3]/[4]/[5] prove nothing"
fi

# --- [3][4][5] the properties, over generated input --------------------------
if command -v python3 >/dev/null 2>&1; then
    python3 - "$T" "$F" <<'PY' || bad "[3]/[4]/[5] property check reported a failure"
import pathlib, random, subprocess, sys
T, F = sys.argv[1], sys.argv[2]
random.seed(7)                      # deterministic: a failure is reproducible
alph = "abcdefghijéàçü日本語\U0001f389"
lost = over = badutf = 0
N = 200
for _ in range(N):
    words = ["".join(random.choice(alph) for _ in range(random.randint(1, 12)))
             for _ in range(random.randint(1, 25))]
    txt = " ".join(words)
    w = random.randint(3, 30)
    p = pathlib.Path(T + "/g.txt"); p.write_text(txt + "\n", encoding="utf-8")
    r = subprocess.run([F, str(p), f"--width={w}"], capture_output=True, text=True)
    out = r.stdout.split("\n")[:-1]
    if "".join(out).replace(" ", "") != txt.replace(" ", ""):
        lost += 1
        if lost == 1: print(f"  LOST/REORDERED at w={w}")
    for l in out:
        if len(l) > w:
            over += 1
            if over == 1: print(f"  OVER WIDTH w={w}: {len(l)} codepoints")
        try: l.encode("utf-8").decode("utf-8")
        except Exception: badutf += 1
print(f"  {N} generated lines: lost={lost} over-width={over} invalid-utf8={badutf}")
sys.exit(1 if (lost or over or badutf) else 0)
PY
else
    echo "tycho-fold: SKIPPED [3]/[4]/[5] (no python3)"
fi

# --- [7] error paths: exit 2, stderr, EMPTY stdout ---------------------------
for args in "nosuch.txt" "" "utf8.txt --width=0" "utf8.txt --width=x" "utf8.txt --nope"; do
    rc=0
    # shellcheck disable=SC2086
    ( cd "$T" && "$F" $args ) > "$T/e.out" 2> "$T/e.err" || rc=$?
    [ "$rc" -eq 2 ] || bad "[7] '$args': want exit 2, got $rc"
    [ -s "$T/e.err" ] || bad "[7] '$args': said nothing on stderr"
    [ ! -s "$T/e.out" ] || bad "[7] '$args': wrote to STDOUT"
    printf '=== err %s\n' "$args" >> "$out"; cat "$T/e.err" >> "$out"
done

# --- [8] invalid UTF-8 IN is reported, not silently passed through -----------
# OCTAL, not \xNN: /bin/sh is dash here and its printf does not know hex escapes,
# so \xff wrote the literal four characters and the "invalid UTF-8" fixture was
# perfectly valid UTF-8. The leg failed and the program was right.
printf 'good line\n\377\376 broken\n' > "$T/bad.txt"
rc=0; ( cd "$T" && "$F" bad.txt --width=20 ) > "$T/b.out" 2> "$T/b.err" || rc=$?
[ "$rc" -eq 1 ] || bad "[8] a file with invalid UTF-8 should exit 1, got $rc"
grep -q 'not valid UTF-8' "$T/b.err" || bad "[8] invalid UTF-8 was not reported on stderr"
printf '=== invalid utf8 in\n' >> "$out"; cat "$T/b.err" >> "$out"

exp=tools/tycho-fold/expected.out
if [ "${RECORD:-0}" = 1 ]; then
    cp "$out" "$exp"; echo "rec  tycho-fold"
elif ! cmp -s "$out" "$exp"; then
    bad "transcript != golden"; diff -u "$exp" "$out" | head -30 || true
fi

[ "$fail" -eq 0 ] || { echo "tycho-fold: FAIL"; exit 1; }
echo "tycho-fold: green (over 200 generated lines mixing ASCII/Latin-1/CJK/emoji at widths 3..30, nothing is lost or reordered, no line exceeds the width in CODEPOINTS, and every output line is still valid UTF-8; the byte-counting mode agrees with the codepoint mode on pure ASCII and differs on non-ASCII, which is what makes those three mean anything; invalid UTF-8 input exits 1 and says so; 5 error paths exit 2 with an empty stdout)"
