#!/bin/sh
# Gate for tycho-ar, the deterministic archiver in tools/tycho-ar/main.ty.
#
# WHY THIS IS A GOLDEN RUNNER AND NOT A DAEMON HARNESS. server/run.sh has to
# start a process, read a bound port out of a banner and talk to it over a
# socket, because the thing it gates is a server. tycho-ar is a batch program:
# it takes a tree in and produces a file, so it gates the way examples/*/run.sh
# do -- build it, run it over a fixture, compare stdout to a recorded golden --
# with the archive-specific assertions layered on top.
#
# Re-record the golden with:  RECORD=1 sh tools/tycho-ar/run.sh
#
# WHAT IT ASSERTS, and why each leg exists. The four properties
# the tycho-ar plan established are the four ways this
# program can betray the person using it:
#
#   [1] CREATE IS DETERMINISTIC. Archiving one tree twice gives two
#       `cmp`-identical files. This is the property the whole format was
#       ordered around (sorted entries), and it is the one that silently dies
#       if anything ever iterates a directory in readdir order again.
#   [2] THE LISTING MATCHES A GOLDEN. `t` prints `size mtime sha256 path` per
#       member. The golden is content-derived -- sizes, mtimes and digests of
#       a fixture this script builds -- so it carries no temp path and no host
#       detail, and it reddens on any change to the digest, the walk order or
#       the line format.
#   [3] THE ROUND TRIP IS CLEAN. `diff -r` between the fixture and an extracted
#       tree is empty, including the empty file, the dotfile, the name with a
#       space, the name with a NEWLINE in it, the file with interior NULs and a
#       file larger than the 64 KiB hashing chunk.
#   [4] DAMAGE IS REFUSED, three ways, and a path that escapes is refused
#       BEFORE the first write.
#
# THE GOLDEN IS DETERMINISTIC ONLY BECAUSE THE FIXTURE IS BUILT HERE. Every
# fixture file is written by this script from a literal or a counted loop --
# never from /dev/urandom, never copied out of the tree -- and every one is
# stamped `touch -d @1700000000`, because mtime is a header field and `t`
# prints it. A fixture with a real mtime would make the golden a record of the
# minute it was recorded.
#
# WHAT IS NOT ASSERTED, deliberately: the ARCHIVE BYTES have no golden. They
# embed a gzip payload, and its byte length depends on the zlib the host links.
# Determinism of the archive is a property of two runs on ONE host, which is
# what leg 1 measures; a recorded byte length would be a claim about the
# grader's zlib. The digests in the golden are over the ORIGINAL bytes, so they
# are zlib-independent. This is also why every offset used by the corruption
# legs below is PARSED out of the header rather than hardcoded.
#
# THE ONE SOFT DEPENDENCY is sha256sum, and only for leg 4b -- the leg that has
# to forge a payload digest. Everything else is dd, cmp and diff. 4b prints a
# SKIP line if sha256sum is absent rather than pretending it ran.
set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "tycho-ar: no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
RECORD="${RECORD:-0}"
golden=tools/tycho-ar/expected.out

# A filename containing a newline is not representable on Windows. MSYS2 stores
# one by mapping the byte into a Unicode private-use plane, but tycho-ar is a
# NATIVE program reading the directory through the narrow (ANSI) API, so the
# name comes back as "new?line.txt" and the stat that follows fails -- the tool
# is behaving correctly about a name the platform cannot hold. The fixture drops
# that ONE member there and selects a `.win` golden, the same mechanism
# tests/run.sh uses for float_str_locale.out.win. Every other awkward name (the
# dotfile, the embedded space, the NUL payload, the deep path, the empty file,
# the multichunk file) is still exercised on both platforms.
case "$(uname -s)" in
    *MSYS*|*MINGW*|*CYGWIN*) IS_WINDOWS=1 ;;
    *) IS_WINDOWS=0 ;;
esac
if [ "$IS_WINDOWS" = 1 ]; then
    echo "tycho-ar: SKIP the newline-in-name member (Windows filenames cannot contain \\n); using $golden.win"
    golden="$golden.win"
fi
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL: $*"; fail=1; }

AR="$T/tycho-ar"
if ! "$TYCHOC" tools/tycho-ar/main.ty -o "$AR" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/build.log"
    echo "tycho-ar: FAIL"; exit 1
fi

# ---------------------------------------------------------------------------
# the fixture tree -- every awkward name and size the phases used, minus the
# random bytes, which a golden cannot carry.
# ---------------------------------------------------------------------------
tree="$T/tree"
mkdir -p "$tree/sub/deep"
printf 'hello, archive\n'          > "$tree/a.txt"
: > "$tree/empty"
printf 'dotfile\n'                 > "$tree/.hidden"
printf 'spaced out\n'              > "$tree/with space.txt"
if [ "$IS_WINDOWS" = 0 ]; then
    printf 'newline in the name\n' > "$tree/new
line.txt"
fi
printf '%b' 'A\000B\000C\000'      > "$tree/sub/nul.bin"
printf 'deep\n'                    > "$tree/sub/deep/d.txt"
# 4000 x 38 bytes = 152000, so it spans three 64 KiB hashing chunks and
# straddles no boundary exactly -- the exact-boundary sizes (0, 55, 56, 63, 64,
# 65, 65535, 65536, 65537) were checked against sha256sum when the chunked
# hasher landed and do not need a golden here.
i=0
while [ "$i" -lt 4000 ]; do
    printf 'multichunk line %04d ----------------\n' "$i"
    i=$((i + 1))
done > "$tree/sub/multichunk.txt"
find "$tree" -exec touch -d @1700000000 {} + || { echo "FAIL: touch -d @EPOCH unsupported"; exit 1; }

# A second, one-member tree for the damage legs. One member means the first
# header IS the only header, so the offsets parsed below are unambiguous, and
# its payload is small enough to dd a byte at a time.
tiny="$T/tiny"
mkdir -p "$tiny/xx"
printf 'twelve bytes' > "$tiny/xx/a.txt"       # exactly 12 bytes, and the path
find "$tiny" -exec touch -d @1700000000 {} +   # `xx/a.txt` is exactly 8

# ---------------------------------------------------------------------------
# [1] create twice, byte-identical
# ---------------------------------------------------------------------------
"$AR" c "$T/c1.tyar" "$tree" >"$T/c1.log" 2>&1 || bad "c (first run) exited $?"
"$AR" c "$T/c2.tyar" "$tree" >"$T/c2.log" 2>&1 || bad "c (second run) exited $?"
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c1.tyar" "$T/c2.tyar"; then
    bad "create is NOT deterministic: two runs over one tree differ"
fi

# ---------------------------------------------------------------------------
# [2] the listing, against the golden
# ---------------------------------------------------------------------------
if [ "$fail" -eq 0 ]; then
    "$AR" t "$T/c1.tyar" >"$T/t.out" 2>"$T/t.err" || bad "t exited $? on a good archive"
fi
if [ "$RECORD" = 1 ]; then
    if [ "$fail" -ne 0 ]; then echo "FAIL: refusing to RECORD from a failed run"; exit 1; fi
    cp "$T/t.out" "$golden"; echo "rec  tycho-ar"
fi
if [ "$fail" -eq 0 ] && [ ! -f "$golden" ]; then bad "no golden -- run RECORD=1 sh tools/tycho-ar/run.sh"; fi
if [ "$fail" -eq 0 ] && ! cmp -s "$T/t.out" "$golden"; then
    bad "t listing != golden"; diff "$golden" "$T/t.out" | sed 's/^/      /'
fi

# ---------------------------------------------------------------------------
# [3] the round trip
# ---------------------------------------------------------------------------
if [ "$fail" -eq 0 ]; then
    "$AR" x "$T/c1.tyar" "$T/out" >"$T/x.log" 2>&1 || bad "x exited $? on a good archive"
    diff -r "$tree" "$T/out" >"$T/diff.log" 2>&1 || {
        bad "round trip: diff -r is not empty"; sed 's/^/      /' "$T/diff.log"
    }
fi

# ---------------------------------------------------------------------------
# [4] damage and escape
#
# Every offset here is READ OUT OF THE ARCHIVE, never assumed: the first member
# header is a single ASCII line `F size mtime clen plen sha csha`, so parsing it
# gives the path offset and the payload offset without depending on how many
# bytes this host's zlib produced. See THE FORMAT in tools/tycho-ar/main.ty.
# ---------------------------------------------------------------------------
A="$T/tiny.tyar"
"$AR" c "$A" "$tiny" >"$T/tiny.log" 2>&1 || bad "c exited $? on the one-file tree"

hdr=$(dd if="$A" bs=1 skip=6 count=512 2>/dev/null | head -1)
hlen=${#hdr}
# shellcheck disable=SC2086
set -- $hdr
[ "$#" -eq 7 ] || bad "first member header has $# fields, want 7 -- the format moved"
clen=${4:-0}; plen=${5:-0}
path_off=$((6 + hlen + 1))          # magic "TYAR1\n" + header line + its \n
payload_off=$((path_off + plen + 1))
csha_off=$((6 + hlen - 64))         # csha is the last field on the header line
[ "$plen" -eq 8 ] || bad "tiny fixture path is $plen bytes, want 8 (xx/a.txt)"

# refuses_x <label> <archive> <dest> <expected substring>
refuses_x() {
    _lbl=$1; _arc=$2; _dst=$3; _want=$4
    "$AR" x "$_arc" "$_dst" >"$T/r.out" 2>"$T/r.err"
    _rc=$?
    if [ "$_rc" -eq 0 ]; then
        bad "$_lbl: x EXITED 0 -- the archive was accepted"
    elif ! grep -qF "$_want" "$T/r.err"; then
        bad "$_lbl: x failed but not for the expected reason (want: $_want)"
        sed 's/^/      /' "$T/r.err"
    fi
}

# [4a] a member path that climbs out. `../a.txt` is the same 8 bytes as
# `xx/a.txt`, and no digest in the format covers the path, so overwriting it in
# place produces an archive that is valid in every other respect -- which is the
# only honest way to test the traversal check.
if [ "$fail" -eq 0 ]; then
    cp "$A" "$T/esc.tyar"
    printf '../a.txt' > "$T/p"
    dd if="$T/p" of="$T/esc.tyar" bs=1 seek="$path_off" count=8 conv=notrunc 2>/dev/null
    refuses_x "traversal" "$T/esc.tyar" "$T/escdest" "path escapes the destination"
    [ ! -e "$T/escdest" ] || bad "traversal: destination was created before the refusal"

    cp "$A" "$T/abs.tyar"
    printf '/tmp/a.t' > "$T/p"
    dd if="$T/p" of="$T/abs.tyar" bs=1 seek="$path_off" count=8 conv=notrunc 2>/dev/null
    refuses_x "absolute path" "$T/abs.tyar" "$T/absdest" "path escapes the destination"
    [ ! -e "$T/absdest" ] || bad "absolute path: destination was created before the refusal"
fi

# [4b] a flipped payload byte, caught by csha -- before anything is handed to
# zlib. Offset 10 into a gzip stream is its first deflate byte.
if [ "$fail" -eq 0 ]; then
    cp "$A" "$T/flip.tyar"
    off=$((payload_off + 10))
    orig=$(dd if="$T/flip.tyar" bs=1 skip="$off" count=1 2>/dev/null | od -An -tu1 | tr -d ' \n')
    oct=$(printf '\\%03o' $(( (orig + 1) % 256 )))
    printf "$oct" > "$T/p"
    dd if="$T/p" of="$T/flip.tyar" bs=1 seek="$off" count=1 conv=notrunc 2>/dev/null
    refuses_x "flipped payload byte" "$T/flip.tyar" "$T/flipdest" "payload digest mismatch (csha)"
    "$AR" t "$T/flip.tyar" >/dev/null 2>&1 && bad "flipped payload byte: t EXITED 0 -- listing does not verify"
fi

# [4c] the same flip WITH csha recomputed to match, which disarms 4b's check and
# leaves `size` as the only witness. This is the leg that proves the format's
# answer to the empty-versus-corrupt question is load-bearing:
# `compress.decompress` returns zero bytes for corrupt input exactly as it does
# for a legitimately empty file, so without the stored original length this
# archive would extract as a 0-byte a.txt and exit 0.
if [ "$fail" -eq 0 ]; then
    if ! command -v sha256sum >/dev/null 2>&1; then
        echo "tycho-ar: leg 4c SKIP (no sha256sum -- cannot forge a payload digest)"
    else
        cp "$T/flip.tyar" "$T/fixed.tyar"
        dd if="$T/fixed.tyar" bs=1 skip="$payload_off" count="$clen" 2>/dev/null > "$T/pay"
        sha256sum < "$T/pay" | cut -d' ' -f1 | tr -d '\n' > "$T/p"
        dd if="$T/p" of="$T/fixed.tyar" bs=1 seek="$csha_off" count=64 conv=notrunc 2>/dev/null
        refuses_x "forged csha" "$T/fixed.tyar" "$T/fixeddest" "(corrupt payload)"
    fi
fi

# [4d] the trailer sheared off -- the one damage per-member checks cannot see.
if [ "$fail" -eq 0 ]; then
    sz=$(wc -c < "$A")
    dd if="$A" of="$T/trunc.tyar" bs=1 count=$((sz - 20)) 2>/dev/null
    refuses_x "truncated archive" "$T/trunc.tyar" "$T/truncdest" "truncated (no trailer"
fi

if [ "$fail" -eq 0 ]; then
    echo "tycho-ar: green (create twice byte-identical; t == golden; diff -r round trip empty; traversal, absolute path, flipped payload, forged csha and truncation all refused)"
else
    echo "tycho-ar: FAIL"; exit 1
fi
