set -u
cd "$(dirname "$0")/../.." || exit 2
. ./scripts/shlib.sh
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || make tychoc1 >/dev/null 2>&1 || { echo "no $TYCHOC -- run 'make' first"; exit 2; }
RECORD="${RECORD:-0}"
golden="tools/tycho-dupe/expected.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
note() { echo "FAIL $1"; fail=1; }
case "$(uname -s)" in
    *MSYS*|*MINGW*|*CYGWIN*) IS_WINDOWS=1 ;;
    *) IS_WINDOWS=0 ;;
esac
transcript="$T/transcript"
: > "$transcript"

# [0] the build is a leg: a new .ty in tools/ must compile under the compiler
# this suite defaults to, and under the bootstrap compiler as a second opinion.
$TYCHOC tools/tycho-dupe/main.ty -o "$T/dupe" > "$T/build.log" 2>&1 || {
    echo "tycho-dupe: FAILED (does not build)"; tail -8 "$T/build.log"; exit 1; }
D="$T/dupe"
REF=""
if [ -x ./tychoc ] && [ "$TYCHOC" != "./tychoc" ]; then
    ./tychoc tools/tycho-dupe/main.ty -o "$T/dupe-ref" > "$T/build-ref.log" 2>&1 && REF="$T/dupe-ref" \
        || note "[0] the bootstrap compiler did not build tycho-dupe"
fi

# The gate's own tree. Every size and every digest is a literal here, so the
# totals below are checkable by reading this file, not by trusting a RECORD=1.
mkdir -p "$T/tree/a" "$T/tree/b" "$T/tree/c" "$T/tree/nested/deep" "$T/tree/big"
printf 'alpha\n' > "$T/tree/a/one.txt"
printf 'alpha\n' > "$T/tree/a/two.txt"
printf 'alpha\n' > "$T/tree/b/three.txt"
printf 'beta\n'  > "$T/tree/b/four.txt"
printf 'beta\n'  > "$T/tree/c/five.txt"
printf 'gamma\n' > "$T/tree/c/unique.txt"
printf 'alpha\n' > "$T/tree/nested/deep/six.txt"
python3 - "$T/tree/big" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
(root / "one.bin").write_bytes(b"\0" * (2 * 1024 * 1024))
(root / "two.bin").write_bytes(b"\0" * (2 * 1024 * 1024))
(root / "three.bin").write_bytes(b"\0" * (1024 * 1024))
(root / "four.bin").write_bytes(b"\0" * (1024 * 1024))
PY
: > "$T/tree/empty1"
: > "$T/tree/empty2"

# [1] two runs identical, the first equal to the golden.
timeout 30 "$D" "$T/tree" > "$T/one.txt" 2>&1 || note "[1] first run exited non-zero"
timeout 30 "$D" "$T/tree" > "$T/two.txt" 2>&1 || note "[1] second run exited non-zero"
cmp -s "$T/one.txt" "$T/two.txt" || note "[1] two runs printed different output"
{ printf '=== default\n'; cat "$T/one.txt"; } >> "$transcript"

# [2] the literals the report has to carry: 13 files, 6 MiB scanned, four sets,
# 10 duplicate files. A golden alone would bless a wrong number after RECORD=1.
grep -q '^4 duplicate sets, 3 MiB reclaimable across 10 files$' "$T/one.txt" \
    || note "[2] the default report's set/file totals are not the expected ones"
grep -q '^scanned 13 files, 6 MiB$' "$T/one.txt" \
    || note "[2] the default report's scan total is not the expected one"
grep -q '2 MiB  2 files  (2 MiB each)' "$T/one.txt" || note "[2] the 2 MiB group is missing"
grep -q '1 MiB  2 files  (1 MiB each)' "$T/one.txt" || note "[2] the 1 MiB group is missing"
grep -q '18 B  4 files  (6 B each)' "$T/one.txt"    || note "[2] the alpha group is missing"
grep -q '5 B  2 files  (5 B each)' "$T/one.txt"     || note "[2] the beta group is missing"

# [3] machine output, and the hashes against Python's hashlib -- an independent
# implementation, not this compiler agreeing with itself.
timeout 30 "$D" --json "$T/tree" > "$T/report.json" 2> "$T/report.err" \
    || note "[3] --json exited non-zero"
python3 - "$T/tree" "$T/report.json" <<'PY' || fail=1
import hashlib, json, sys
root, path = sys.argv[1], sys.argv[2]
d = json.load(open(path))
if d["roots"] != [root]: sys.exit("roots: %r" % (d["roots"],))
if d["min_size"] != 1: sys.exit("min_size: %r" % (d["min_size"],))
if d["scanned"] != {"files": 13, "bytes": 6291496, "skipped": 0}:
    sys.exit("scanned: %r" % (d["scanned"],))
want = [(2 * 1024 * 1024, 2), (1024 * 1024, 2), (6, 4), (5, 2)]
got = [(g["size"], len(g["files"])) for g in d["groups"]]
if got != want: sys.exit("groups: got %r want %r" % (got, want))
for g in d["groups"]:
    if g["wasted"] != g["size"] * (len(g["files"]) - 1):
        sys.exit("wasted: %r" % (g,))
    for f in g["files"]:
        b = open(f, "rb").read()
        if len(b) != g["size"]: sys.exit("size mismatch: %s" % f)
        h = hashlib.sha256(b).hexdigest()
        if h != g["hash"]: sys.exit("hash mismatch: %s" % f)
PY

# [4] --min-size=0 adds the empty-file group; --top=1 limits text and JSON.
timeout 30 "$D" --min-size=0 "$T/tree" > "$T/empty.txt" 2>&1 || note "[4] --min-size=0 exited non-zero"
{ printf '=== min-size 0\n'; cat "$T/empty.txt"; } >> "$transcript"
grep -q '^5 duplicate sets, 3 MiB reclaimable across 12 files$' "$T/empty.txt" \
    || note "[4] --min-size=0 did not add the empty-file group"
grep -q '^0 B  2 files  (0 B each)' "$T/empty.txt" \
    || note "[4] the empty-file group is missing"

timeout 30 "$D" --top=1 "$T/tree" > "$T/top.txt" 2>&1 || note "[4] --top=1 exited non-zero"
{ printf '=== top 1\n'; cat "$T/top.txt"; } >> "$transcript"
grep -q '^4 duplicate sets' "$T/top.txt" || note "[4] --top=1 lost the summary"
grep -q '^2 MiB  2 files' "$T/top.txt"    || note "[4] --top=1 did not show the largest group"
if grep -q '^1 MiB  2 files' "$T/top.txt"; then note "[4] --top=1 showed more than one group"; fi

timeout 30 "$D" --json --top=1 "$T/tree" > "$T/top.json" 2>&1 || note "[4] --json --top=1 exited non-zero"
python3 - "$T/top.json" <<'PY' || fail=1
import json, sys
d = json.load(open(sys.argv[1]))
if len(d["groups"]) != 1: sys.exit("top1 groups: %d" % len(d["groups"]))
PY

# both flag spellings: the README synopsis uses the space form, the option table
# uses '=', and docs-fences runs the synopsis for real.
timeout 30 "$D" --min-size 1 --top 2 "$T/tree" > "$T/space.txt" 2>&1 || note "[4] space-separated options exited non-zero"
timeout 30 "$D" --min-size=1 --top=2 "$T/tree" > "$T/equals.txt" 2>&1 || note "[4] equals-separated options exited non-zero"
cmp -s "$T/space.txt" "$T/equals.txt" || note "[4] --top N and --top=N disagree"

# [5] -- terminates option parsing, so a root that starts with '-' is reachable.
cp -R "$T/tree" "$T/-tree"
( cd "$T" && timeout 30 "$D" --json -- -tree ) > "$T/dash.json" 2>&1 \
    || note "[5] -- did not accept a dash-leading root"
python3 - "$T/dash.json" <<'PY' || fail=1
import json, sys
d = json.load(open(sys.argv[1]))
if len(d["groups"]) != 4: sys.exit("dash-root groups: %d" % len(d["groups"]))
PY

# [6] error paths: exit 2, a message on stderr, and empty stdout.
err_case() {
    desc=$1; shift
    rc=0
    "$D" "$@" > "$T/e.out" 2> "$T/e.err" || rc=$?
    [ "$rc" -eq 2 ] || note "[6] $desc: want exit 2, got $rc"
    [ -s "$T/e.err" ] || note "[6] $desc: said nothing on stderr"
    [ ! -s "$T/e.out" ] || note "[6] $desc: wrote to stdout"
    { printf '=== err %s\n' "$desc"; cat "$T/e.err"; } >> "$transcript"
}
err_case "no args"
err_case "unknown option" --nope
err_case "bad top" --top=x "$T/tree"
err_case "bad min-size" --min-size=-1 "$T/tree"
err_case "missing root" "$T/nope"

# [7] the two compilers agree on the same tree.
if [ -n "$REF" ]; then
    timeout 30 "$REF" --json "$T/tree" > "$T/ref.json" 2>&1 || note "[7] the reference build exited non-zero"
    cmp -s "$T/report.json" "$T/ref.json" || note "[7] the two compilers emitted different JSON"
fi

# [8] symlinks: a symlink to a file is not a second copy, and a symlink cycle
# terminates. The base tree is already recorded, so this leg can skip cleanly.
if [ "$IS_WINDOWS" = 0 ] && ln -s ../a/one.txt "$T/tree/b/alias.txt" 2>/dev/null && ln -s .. "$T/tree/a/up" 2>/dev/null; then
    timeout 30 "$D" --json "$T/tree" > "$T/sym.json" 2> "$T/sym.err" \
        || note "[8] a tree with a symlink cycle exited non-zero"
    python3 - "$T/sym.json" <<'PY' || fail=1
import json, sys
d = json.load(open(sys.argv[1]))
if d["scanned"]["files"] != 13: sys.exit("symlink scan saw %r files, want 13" % (d["scanned"]["files"],))
for g in d["groups"]:
    for f in g["files"]:
        if f.endswith("/alias.txt"): sys.exit("a symlink was counted as a duplicate: %s" % f)
PY
else
    echo "  note [8] symlinks unavailable; symlink legs skipped"
fi

# [8b] a hard link names one inode twice. same_file/device+inode must collapse
# it, or the report claims reclaimable bytes that a delete cannot reclaim.
if [ "$IS_WINDOWS" = 0 ] && ln "$T/tree/a/one.txt" "$T/tree/b/hard.txt" 2>/dev/null; then
    timeout 30 "$D" --json "$T/tree" > "$T/hard.json" 2> "$T/hard.err" \
        || note "[8b] a tree with a hard link exited non-zero"
    python3 - "$T/hard.json" <<'PY' || fail=1
import json, sys
d = json.load(open(sys.argv[1]))
if d["scanned"]["files"] != 13: sys.exit("hardlink scan saw %r files, want 13" % (d["scanned"]["files"],))
for g in d["groups"]:
    for f in g["files"]:
        if f.endswith("/hard.txt"): sys.exit("a hard link was counted as a duplicate: %s" % f)
PY
    # same_file on two roots: two names of one inode are one input.
    ln "$T/tree/a/one.txt" "$T/hardroot1" 2>/dev/null
    ln "$T/hardroot1" "$T/hardroot2" 2>/dev/null
    timeout 30 "$D" --json "$T/hardroot1" "$T/hardroot2" > "$T/hardroots.json" 2> "$T/hardroots.err" \
        || note "[8b] two hard-link roots exited non-zero"
    python3 - "$T/hardroots.json" <<'PY' || fail=1
import json, sys
d = json.load(open(sys.argv[1]))
if d["scanned"]["files"] != 1: sys.exit("two hard-link roots scanned %r files, want 1" % (d["scanned"]["files"],))
PY
else
    echo "  note [8b] hard links unavailable or Windows; hard-link leg skipped"
fi

# [9] unreadable duplicate candidates are counted as skipped, not dropped. A
# root run can read through mode 000, so the leg states its skip.
printf 'probe\n' > "$T/secret-probe" 2>/dev/null || true
if [ "$(id -u 2>/dev/null || echo 0)" -ne 0 ] && chmod 000 "$T/secret-probe" 2>/dev/null && [ ! -r "$T/secret-probe" ]; then
    mkdir "$T/secret"
    printf 'secret\n' > "$T/secret/a"
    cp "$T/secret/a" "$T/secret/b"
    chmod 000 "$T/secret/a" "$T/secret/b"
    timeout 30 "$D" --json "$T/secret" > "$T/secret.json" 2> "$T/secret.err" \
        || note "[9] the unreadable-candidate scan exited non-zero"
    python3 - "$T/secret.json" <<'PY' || fail=1
import json, sys
d = json.load(open(sys.argv[1]))
if d["scanned"]["skipped"] != 2: sys.exit("skipped: %r, want 2" % (d["scanned"]["skipped"],))
if d["groups"] != []: sys.exit("unreadable files were grouped: %r" % (d["groups"],))
PY
    chmod 644 "$T/secret/a" "$T/secret/b"
    # real_checked's Denied branch: a root below a mode-000 directory cannot be
    # resolved, and the diagnostic must say permission, not not-found.
    mkdir "$T/locked"
    chmod 000 "$T/locked"
    rc=0
    "$D" "$T/locked/child" > "$T/denied.out" 2> "$T/denied.err" || rc=$?
    [ "$rc" -eq 2 ] || note "[9] a permission-denied root: want exit 2, got $rc"
    [ ! -s "$T/denied.out" ] || note "[9] a permission-denied root wrote to stdout"
    grep -q 'permission denied' "$T/denied.err" || note "[9] a permission-denied root did not name the cause"
    chmod 755 "$T/locked"
else
    echo "  note [9] no mode-000 semantics on this host; unreadable-candidate leg skipped"
fi

# [10] the golden. It is the transcript, not the only assertion: the numeric
# legs above still fail after a RECORD=1 that blessed a wrong total.
sed "s|$T|TMP|g" "$transcript" > "$T/norm.txt"
if [ "$RECORD" = 1 ]; then
    cp "$T/norm.txt" "$golden"; echo "rec  $golden"
else
    [ -f "$golden" ] || { echo "tycho-dupe: FAILED (no golden -- run RECORD=1)"; exit 1; }
    cmp -s "$T/norm.txt" "$golden" || { note "[10] transcript != golden"; diff -u "$golden" "$T/norm.txt" | head -30 || true; }
fi

[ "$fail" -eq 0 ] || { echo "tycho-dupe: FAIL"; exit 1; }
echo "tycho-dupe: green (builds under both compilers; two runs identical and equal to the golden; 13 files / 6 MiB / 4 sets and every hash against Python hashlib; --min-size=0 adds the empty group; --top=1 limits text and JSON; -- accepts a dash-leading root; 5 error paths exit 2 with stderr and empty stdout; symlink file and cycle skipped; unreadable duplicate candidates counted as 2 skipped)"
