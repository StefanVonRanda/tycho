set -u
cd "$(dirname "$0")/../.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
command -v python3 >/dev/null 2>&1 || { echo "snap-check: SKIPPED (no python3 for the independent zip reader)"; exit 0; }
RECORD="${RECORD:-0}"
golden="tools/tycho-snap/snap.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
note() { echo "FAIL $1"; fail=1; }

$TYCHOC -o "$T/snap" tools/tycho-snap/main.ty > "$T/build.log" 2>&1 || {
    echo "snap-check: FAILED (tycho-snap does not build)"; tail -3 "$T/build.log"; exit 1; }

# ---- the fixture tree, built here so its shape is this file's to assert ------
R="$T/tree"
mkdir -p "$R/sub" "$R/skipme"
printf 'excluded\n'     > "$R/skipme/f.ty"
printf 'deeper\n'       > "$R/sub/yank.md"
printf 'deep\n'         > "$R/sub/bravo.ty"
printf 'not selected\n' > "$R/kilo.txt"
printf 'beta\n'         > "$R/mike.md"
printf 'alpha\n'        > "$R/zeta.ty"
cat > "$T/m.toml" <<TOML
[snapshot]
name = "fixture"
root = "$R"
ext  = [".ty", ".md"]
skip = ["skipme"]
TOML

# [1] two runs: transcripts identical to each other, archives identical too
timeout 10 "$T/snap" --manifest "$T/m.toml" --out "$T/one.zip" > "$T/one.txt" 2>&1 || note "[1] first run exited non-zero"
timeout 10 "$T/snap" --manifest "$T/m.toml" --out "$T/two.zip" > "$T/two.txt" 2>&1 || note "[1] second run exited non-zero"
# the transcript names the temp paths, which move per run -- strip them
# normalise the temp paths AND the per-run output name, which is all that may
# differ between the two runs; anything else differing is the finding
sed "s|$T|TMP|g; s|$R|ROOT|g; s|one\.zip|OUT.zip|g" "$T/one.txt" > "$T/one.norm"
sed "s|$T|TMP|g; s|$R|ROOT|g; s|two\.zip|OUT.zip|g" "$T/two.txt" > "$T/two.norm"
cmp -s "$T/one.norm" "$T/two.norm" || note "[1] two runs printed different transcripts"
cmp -s "$T/one.zip"  "$T/two.zip"  || note "[1] two runs produced different archive BYTES (walk order not sorted?)"

if [ "$RECORD" = 1 ]; then
    cp "$T/one.norm" "$golden"; echo "rec  $golden"
else
    [ -f "$golden" ] || { echo "snap-check: FAILED (no golden -- run RECORD=1)"; exit 1; }
    cmp -s "$T/one.norm" "$golden" || { note "[1] transcript differs from the golden"; diff "$golden" "$T/one.norm" | head -8; }
fi

# [2] the entry set, exact, against a literal
python3 -c "import zipfile,sys;print('\n'.join(sorted(zipfile.ZipFile(sys.argv[1]).namelist())))" "$T/one.zip" > "$T/got.txt" 2>&1
cat > "$T/want.txt" <<'WANT'
mike.md
sub/bravo.ty
sub/yank.md
zeta.ty
WANT
cmp -s "$T/got.txt" "$T/want.txt" || { note "[2] entry set is not the expected one"; diff "$T/want.txt" "$T/got.txt" | head -8; }

# [2b] the member ORDER, not just the set. Two runs of the same broken build
# agree with each other and a golden re-recorded from it agrees too, so neither
# [1] nor [2] can see a walk that stopped sorting -- only this can. The fixture
# is created in reverse order below so readdir order and sorted order differ.
if [ "$(python3 -c "import os,sys;g=os.listdir(sys.argv[1]);print(g==sorted(g))" "$R")" = "True" ]; then
    echo "NOTE [2b] readdir order on this host equals sorted order for this fixture --"
    echo "     the leg below cannot distinguish a sorted walk from an unsorted one here."
fi
python3 -c "import zipfile,sys;print('\n'.join(zipfile.ZipFile(sys.argv[1]).namelist()))" "$T/one.zip" > "$T/order.txt" 2>&1
cmp -s "$T/order.txt" "$T/want.txt" || { note "[2b] members are not in sorted order"; diff "$T/want.txt" "$T/order.txt" | head -8; }

# [3] an independent reader accepts it, and one member matches the file on disk
python3 - "$T/one.zip" "$R" > "$T/indep.txt" 2>&1 <<'PY'
import hashlib, sys, zipfile
z = zipfile.ZipFile(sys.argv[1]); root = sys.argv[2]
print("testzip", z.testzip())
m = "sub/bravo.ty"
print("member", hashlib.sha256(z.read(m)).hexdigest())
print("ondisk", hashlib.sha256(open(root + "/" + m, "rb").read()).hexdigest())
PY
grep -q "^testzip None$" "$T/indep.txt" || note "[3] python zipfile refused the archive (testzip did not return None)"
[ "$(awk '/^member/{print $2}' "$T/indep.txt")" = "$(awk '/^ondisk/{print $2}' "$T/indep.txt")" ] \
    || note "[3] a member's bytes out of the archive differ from the file on disk"

# [4] an empty selection is still a valid archive
cat > "$T/empty.toml" <<TOML
[snapshot]
name = "empty"
root = "$R"
ext  = [".nosuchext"]
TOML
timeout 10 "$T/snap" --manifest "$T/empty.toml" --out "$T/empty.zip" --quiet > "$T/empty.txt" 2>&1 || note "[4] empty snapshot exited non-zero"
sz=$(wc -c < "$T/empty.zip")
[ "$sz" -eq 22 ] || note "[4] empty archive is $sz bytes, expected the 22-byte EOCD"
[ "$(python3 -c "import zipfile,sys;print(len(zipfile.ZipFile(sys.argv[1]).namelist()))" "$T/empty.zip" 2>&1)" = "0" ] \
    || note "[4] python zipfile does not read the empty archive as 0 entries"

# [5] failure paths: exit non-zero, and say which thing
timeout 10 "$T/snap" --manifest "$T/nosuch.toml" --out "$T/x.zip" > "$T/miss.txt" 2>&1
[ $? -ne 0 ] || note "[5] a missing manifest exited 0"
grep -q "nosuch.toml" "$T/miss.txt" || note "[5] the missing-manifest message does not name the file"
timeout 10 "$T/snap" --manifest "$T/m.toml" --bogus 1 --out "$T/y.zip" > "$T/unk.txt" 2>&1
rc=$?
[ "$rc" -eq 2 ] || note "[5] an unknown option exited $rc, expected 2"
grep -q -- "--bogus" "$T/unk.txt" || note "[5] the unknown-option message does not name the option"

[ "$fail" = 0 ] || { echo "snap-check: FAILED"; exit 1; }
echo "tycho-snap: green (transcript and archive BYTES identical over 2 runs and the transcript equal to the golden; the 4-member entry set exact against a literal, with sub/ descended, .txt filtered out and skipme/ never entered; python3 zipfile testzip() None and sub/d.ty's sha256 out of the archive equal to the file on disk; the empty selection a 22-byte EOCD read as 0 entries; a missing manifest exit 1 naming the file and an unknown option exit 2 naming it)"
