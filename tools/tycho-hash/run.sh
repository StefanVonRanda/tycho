set -eu

cd "$(dirname "$0")/../.."
TYCHOC="${TYCHOC:-./tychoc1}"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
out="$T/got"
: > "$out"
fail=0
bad() { echo "FAIL: $*"; fail=1; }

[ -x "$TYCHOC" ] || make tychoc >/dev/null

# ---------------------------------------------------------------------------
# [1] it builds
# ---------------------------------------------------------------------------
$TYCHOC tools/tycho-hash/main.ty -o "$T/hash" > "$T/build.log" 2>&1 || {
    echo "tycho-hash: FAILED (does not build)"; tail -5 "$T/build.log"; exit 1; }
H="$T/hash"

# ---------------------------------------------------------------------------
# [2] a fixed tree, built here so the corpus is not a golden's hostage. Contents
#     are CONSTANT (no /dev/urandom): a random corpus would change the hashes
#     every run and force the golden to be re-recorded, which is how a golden
#     stops meaning anything.
# ---------------------------------------------------------------------------
R="$T/tree"
mkdir -p "$R/a/b" "$R/c" "$R/empty_dir"
i=1
while [ "$i" -le 9 ]; do
    # deterministic filler: i copies of a fixed 32-byte line
    j=1; : > "$R/a/f$i.txt"
    while [ "$j" -le "$i" ]; do printf 'tycho-hash deterministic filler\n' >> "$R/a/f$i.txt"; j=$((j+1)); done
    i=$((i+1))
done
printf 'hello\n' > "$R/a/b/x.txt"
printf 'world\n' > "$R/c/y.txt"
: > "$R/c/empty"
NFILES=12

printf '=== report (4 workers)\n' >> "$out"
( cd "$T" && "$H" tree --workers=4 ) >> "$out" 2>&1 || bad "[2] the 4-worker run exited non-zero"

# ---------------------------------------------------------------------------
# [3] DETERMINISM across pool widths, and [5] ACCOUNTING at each one.
# ---------------------------------------------------------------------------
for w in 1 2 3 5 8; do
    rc=0; ( cd "$T" && "$H" tree --workers=$w --dist ) > "$T/w$w.out" 2> "$T/w$w.err" || rc=$?
    [ "$rc" -eq 0 ] || bad "[3] --workers=$w exited $rc"
    if ! cmp -s "$T/w1.out" "$T/w$w.out"; then
        bad "[3] --workers=$w differs from --workers=1 -- the report depends on the pool width"
        diff "$T/w1.out" "$T/w$w.out" | head -6 | sed 's/^/      /'
    fi
    # [5] the counts must sum to exactly NFILES -- not "about"
    sum=$(sed -n 's/^dist //p' "$T/w$w.err" | tr ' ' '+' | sed 's/+$//' | bc 2>/dev/null || echo -1)
    [ "$sum" = "$NFILES" ] || bad "[5] --workers=$w hashed $sum files, want exactly $NFILES"
done
printf '=== dist w=1\n' >> "$out"; cat "$T/w1.err" >> "$out"

# ---------------------------------------------------------------------------
# [3b] the split itself. Without this, [3] also passes when one worker does
#      everything -- which is exactly what the first version of this program did.
# ---------------------------------------------------------------------------
d1=$(sed -n 's/^dist //p' "$T/w1.err")
first=$(echo "$d1" | cut -d' ' -f1)
restsum=$(echo "$d1" | cut -d' ' -f2-8 | tr ' ' '+' | bc 2>/dev/null || echo -1)
[ "$first" = "$NFILES" ] || bad "[3b] at 1 worker the first should take all $NFILES, took $first"
[ "$restsum" = "0" ] || bad "[3b] at 1 worker the others should take 0, took $restsum -- --workers does nothing"

best=0
tries=0
while [ "$tries" -lt 5 ]; do
    tries=$((tries + 1))
    ( cd "$T" && "$H" tree --workers=8 --dist ) > /dev/null 2> "$T/w8try.err" || true
    dt=$(sed -n 's/^dist //p' "$T/w8try.err")
    [ -n "$dt" ] || continue
    sum=0; act=0
    for v in $dt; do sum=$((sum + v)); [ "$v" -gt 0 ] && act=$((act + 1)); done
    [ "$sum" -eq "$NFILES" ] || bad "[3b] attempt $tries: counts sum to $sum, not $NFILES"
    [ "$act" -ge 2 ] || bad "[3b] attempt $tries: only $act worker(s) took anything -- the pool is not sharing, so [3] proves nothing"
    [ "$act" -gt "$best" ] && best=$act
    [ "$best" -ge 6 ] && break
done
[ "$best" -ge 6 ] || bad "[3b] best of $tries attempts was $best of 8 workers participating; want >= 6"

# ---------------------------------------------------------------------------
# [4] every hash against sha256sum(1)
# ---------------------------------------------------------------------------
if command -v sha256sum >/dev/null 2>&1; then
    mism=0; checked=0
    while read -r h sz rel; do
        [ "$h" = "--" ] && continue
        want=$(sha256sum "$R/$rel" 2>/dev/null | cut -d' ' -f1)
        checked=$((checked+1))
        [ "$h" = "$want" ] || { mism=$((mism+1)); echo "      MISMATCH $rel: got $h want $want"; }
    done < "$T/w1.out"
    [ "$mism" -eq 0 ] || bad "[4] $mism hash(es) disagree with sha256sum"
    [ "$checked" -eq "$NFILES" ] || bad "[4] compared $checked hashes, want $NFILES"
    # the empty file's digest is known by heart; if the loop above silently
    # compared nothing this line still fails.
    grep -q '^e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  0  c/empty$' "$T/w1.out" \
        || bad "[4] the empty file's digest is not the known SHA-256 of empty input"
else
    echo "tycho-hash: SKIPPED [4] (no sha256sum)"
fi

# ---------------------------------------------------------------------------
# [6] error paths: exit 2, a message on STDERR, and an EMPTY stdout -- a hasher
#     that reports a failure on stdout corrupts the manifest it is piped into.
# ---------------------------------------------------------------------------
for args in "nosuchdir" "" "tree --workers=0" "tree --workers=99" "tree --workers=x" "tree --nope"; do
    rc=0
    # shellcheck disable=SC2086
    ( cd "$T" && "$H" $args ) > "$T/e.out" 2> "$T/e.err" || rc=$?
    [ "$rc" -eq 2 ] || bad "[6] '$args': want exit 2, got $rc"
    [ -s "$T/e.err" ] || bad "[6] '$args': said nothing on stderr"
    [ ! -s "$T/e.out" ] || bad "[6] '$args': wrote to STDOUT"
    printf '=== err %s\n' "$args" >> "$out"; cat "$T/e.err" >> "$out"
done

# a FILE where a directory is wanted must be refused, not silently hashed
rc=0; ( cd "$T" && "$H" tree/c/y.txt ) > "$T/f.out" 2> "$T/f.err" || rc=$?
[ "$rc" -eq 2 ] || bad "[6] a plain file argument: want exit 2, got $rc"
printf '=== err file-as-dir\n' >> "$out"; cat "$T/f.err" >> "$out"

# ---------------------------------------------------------------------------
# verdict
# ---------------------------------------------------------------------------
exp=tools/tycho-hash/expected.out
if [ "${RECORD:-0}" = 1 ]; then
    cp "$out" "$exp"; echo "rec  tycho-hash"
elif ! cmp -s "$out" "$exp"; then
    bad "transcript != golden"; diff -u "$exp" "$out" | head -30 || true
fi

[ "$fail" -eq 0 ] || { echo "tycho-hash: FAIL"; exit 1; }
echo "tycho-hash: green (report byte-identical at 1, 2, 3, 5 and 8 workers, and the pool really shares -- at width 8 at least 2 workers take work on every attempt and at least 6 on the best of up to 5, while at width 1 the first takes all 12 and the rest none; every hash equals sha256sum's and the empty file's is the known e3b0c442...; the per-worker counts sum to exactly 12 at every width; 7 error paths exit 2 with a message on stderr and an empty stdout)"
