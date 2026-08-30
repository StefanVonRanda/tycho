set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC="${TYCHOC:-./tychoc}"
[ -x "$TYCHOC" ] || { echo "tycho-ar: no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
RECORD="${RECORD:-0}"
golden=tools/tycho-ar/expected.out

case "$(uname -s)" in
    *MSYS*|*MINGW*|*CYGWIN*) IS_WINDOWS=1; FIND=/usr/bin/find ;;
    *) IS_WINDOWS=0; FIND=find ;;
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
i=0
while [ "$i" -lt 4000 ]; do
    printf 'multichunk line %04d ----------------\n' "$i"
    i=$((i + 1))
done > "$tree/sub/multichunk.txt"
"$FIND" "$tree" -exec env TZ=UTC0 touch -t 202311142213.20 {} + || { echo "FAIL: portable mtime stamp"; exit 1; }

# A second, one-member tree for the damage legs. One member means the first
# header IS the only header, so the offsets parsed below are unambiguous, and
# its payload is small enough to dd a byte at a time.
tiny="$T/tiny"
mkdir -p "$tiny/xx"
printf 'twelve bytes' > "$tiny/xx/a.txt"       # exactly 12 bytes, and the path
"$FIND" "$tiny" -exec env TZ=UTC0 touch -t 202311142213.20 {} +   # `xx/a.txt` is exactly 8

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

if [ "$fail" -eq 0 ]; then
    : > "$T/ref_before"; : > "$T/ref_at"
    env TZ=UTC0 touch -t 202311142213.19 "$T/ref_before"
    env TZ=UTC0 touch -t 202311142213.20 "$T/ref_at"
    "$FIND" "$T/out" -type f \( ! -newer "$T/ref_before" -o -newer "$T/ref_at" \) \
        > "$T/mtime.bad" 2>"$T/mtime.err"
    if [ -s "$T/mtime.bad" ]; then
        bad "round trip: extracted files do not carry the archived mtime (1700000000)"
        sed 's/^/      /' "$T/mtime.bad"
    fi
    # And the control on the control: the same test over the SOURCE tree, whose
    # files were stamped by `touch` above, must find nothing. If it does, the
    # comparison itself is broken and the empty result above proves nothing.
    "$FIND" "$tree" -type f \( ! -newer "$T/ref_before" -o -newer "$T/ref_at" \) \
        > "$T/mtime.src" 2>&1
    if [ -s "$T/mtime.src" ]; then
        bad "mtime comparison is broken: it rejects the FIXTURE's own timestamps"
        sed 's/^/      /' "$T/mtime.src"
    fi
fi

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

forge_hdr() {
    _lbl=$1; _field=$2; _value=$3
    _new=$(echo "$hdr" | awk -v f="$_field" -v v="$_value" '{ $f = v; print }')
    dd if="$A" bs=1 count=6 2>/dev/null > "$T/$_lbl.tyar"
    printf '%s\n' "$_new" >> "$T/$_lbl.tyar"
    dd if="$A" bs=1 skip="$path_off" 2>/dev/null >> "$T/$_lbl.tyar"
    refuses_x "$_lbl" "$T/$_lbl.tyar" "$T/${_lbl}dest" "header has a non-numeric length field"
    [ ! -e "$T/${_lbl}dest" ] || bad "$_lbl: destination was created before the refusal"
    "$AR" t "$T/$_lbl.tyar" >/dev/null 2>&1 && bad "$_lbl: t EXITED 0 -- listing accepted the forged length"
}
if [ "$fail" -eq 0 ]; then
    forge_hdr "neg_size"  2 "-5"                       # field 2 is `size`
    forge_hdr "huge_clen" 4 "1000000000000000001"      # field 4 is `clen`, one over the cap
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
        refuses_x "forged csha" "$T/fixed.tyar" "$T/fixeddest" "payload is corrupt"
    fi
fi

# [4d] the trailer sheared off -- the one damage per-member checks cannot see.
if [ "$fail" -eq 0 ]; then
    sz=$(wc -c < "$A")
    dd if="$A" of="$T/trunc.tyar" bs=1 count=$((sz - 20)) 2>/dev/null
    refuses_x "truncated archive" "$T/trunc.tyar" "$T/truncdest" "truncated (no trailer"
fi

# ---------------------------------------------------------------------------
# [5] A GENUINE set_mtime FAILURE: warn on stderr, keep going, exit nonzero.
#
# The lever is real, not a stub. `/dev/null` is mode 666 and owned by root, so a
# member target that is a symlink to it ACCEPTS the write and REFUSES the
# utimensat -- POSIX grants explicit times only to the owner (EPERM). Probed
# first and SKIPPED if this host can stamp /dev/null anyway (running as root, a
# platform without it): a leg that cannot fail would prove nothing. `m_aaa`
# sorts before `m_zzz`, so finding `m_zzz` on disk afterwards is the proof that
# extraction CONTINUED past the failure rather than dying at it.
if [ "$fail" -eq 0 ]; then
    mt="$T/mtree"; mkdir -p "$mt"
    printf 'first\n' > "$mt/m_aaa.txt"
    printf 'last\n'  > "$mt/m_zzz.txt"
    "$AR" c "$T/m.tyar" "$mt" >/dev/null 2>&1 || bad "mtime warn: create failed"
    md="$T/mdest"; mkdir -p "$md"
    ln -s /dev/null "$md/m_aaa.txt" 2>/dev/null
    # The probe must ask for an EXPLICIT time, as `set_mtime` does. A bare
    # `touch` is UTIME_NOW, which write permission alone is enough for, and it
    # answers a different question -- it passed here and skipped the leg.
    if [ ! -L "$md/m_aaa.txt" ] || env TZ=UTC0 touch -t 202311142213.20 "$md/m_aaa.txt" 2>/dev/null; then
        echo "tycho-ar: SKIP the set_mtime-warning leg (this host can stamp /dev/null)"
        rm -f "$md/m_aaa.txt"
    else
        "$AR" x "$T/m.tyar" "$md" >"$T/m.out" 2>"$T/m.err"
        rc=$?
        [ "$rc" -ne 0 ] || bad "mtime warn: x exited 0 despite a failed set_mtime"
        grep -q 'cannot set mtime' "$T/m.err" || bad "mtime warn: no warning on stderr"
        if grep -q 'cannot set mtime' "$T/m.out"; then
            bad "mtime warn: the warning went to stdout"
        fi
        grep -q '2 files extracted' "$T/m.out" || bad "mtime warn: x did not report both members"
        [ "$(cat "$md/m_zzz.txt" 2>/dev/null)" = "last" ] ||
            bad "mtime warn: extraction stopped at the failing member"
    fi
fi

if command -v python3 >/dev/null 2>&1 && [ -n "${TYCHOC:-./tychoc}" ]; then
    bd="$T/bomb"; mkdir -p "$bd"
    python3 -c "
import gzip,pathlib,sys
pathlib.Path(sys.argv[1]).write_bytes(gzip.compress(b'x'*300000, 9))
" "$bd/small.gz" 2>/dev/null || bad "bomb: could not build the fixture"
    cat > "$bd/m.ty" <<'TY'
package main
import "core:io"
import "core:compress"
fn main():
    match io.read_bytes(args()[1]):
        Ok(b):
            match compress.decompress(b):
                Ok(o): println("out " + str(len(o)))
                Err(e): println("refused")
        Err(e): println("unreadable")
TY
    if "$TYCHOC" "$bd/m.ty" --emit-c -o "$bd/g" >"$bd/emit.log" 2>&1; then
        SH=$("$TYCHOC" "$bd/m.ty" --print-shims 2>/dev/null | tr '
' ' ')
        # $SH unquoted on purpose: it is a LIST of shim paths. This lane runs under
        # /bin/sh, which word-splits it; zsh would not, and that cost a debug round.
        if cc -O2 -o "$bd/dflt" "$bd/g.c" $SH -lz -lm -lpthread 2>/dev/null         && cc -O2 -DZD_MAX_OUT=65536 -o "$bd/tiny" "$bd/g.c" $SH -lz -lm -lpthread 2>/dev/null; then
            got_d=$("$bd/dflt" "$bd/small.gz" 2>&1)
            got_t=$("$bd/tiny" "$bd/small.gz" 2>&1)
            [ "$got_d" = "out 300000" ] || bad "bomb: under the 1 GiB ceiling a 300 KB expansion should pass, got '$got_d'"
            [ "$got_t" = "refused" ]    || bad "bomb: with the ceiling forced to 64 KiB the SAME input should be refused, got '$got_t' -- the ceiling does not fire"
        else
            echo "      skip bomb leg (cc could not link the probe)"
        fi
    else
        bad "bomb: the probe does not compile"
    fi
fi

if [ "$fail" -eq 0 ]; then
    echo "tycho-ar: green (create twice byte-identical; t == golden; diff -r round trip empty; extracted mtimes == archived mtimes; a failed set_mtime warns on stderr and exits nonzero without stopping; traversal, absolute path, flipped payload, forged csha and truncation all refused)"
else
    echo "tycho-ar: FAIL"; exit 1
fi
