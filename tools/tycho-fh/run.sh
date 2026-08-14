#!/bin/sh
# Gate for tycho-fh, the handle-based file counter in tools/tycho-fh/.
#
# Re-record the golden with:  RECORD=1 sh tools/tycho-fh/run.sh
#
# WHY THIS LANE EXISTS. `handle Name:` was declared in exactly 10 files before
# this program and ALL 10 were under tests/ -- nine rejects and tests/ffi. No
# program anywhere used one. Writing the first consumer found a double free
# (FRICTION #43).
#
# WHY ITS SUBJECT IS "EXACTLY ONCE". A handle's contract is that its destructor
# runs once per successful open, at every scope exit. A transcript cannot show
# that: a leaked FILE* prints the same lines as a closed one, and a double free
# is undefined behaviour that often prints nothing. So the C side counts
# (tools/tycho-fh/fh.c) and this lane reads the counters back -- `live` is 0 when
# balanced, POSITIVE on a leak and NEGATIVE on a double free, so the two failure
# directions are told apart rather than merely detected.
#
# WHAT IT ASSERTS
#   [1] THE RUN, TWICE, and equal to the golden.
#   [2] THE COUNTS AGAINST LITERALS, over a file written here.
#   [3] BALANCED AT EXIT: live == 0 and opens == closes, after 64 scope exits.
#   [4] NO FD LEAK UNDER LOAD: --stress opens 20000 times; a destructor that did
#       not run exhausts the process long before that, and the re-read checksum
#       (20000 x the single-file total) catches an open that started failing
#       silently.
#   [5] THE AFFINE REFUSALS, each a probe compiled here and required to FAIL:
#       a decl copy `g := f`, a bare handle struct field, a handle returned from
#       a Tycho fn, a handle in an array, a handle in an Option, and close() on
#       a call result. The first two COMPILED until 2026-08-14 and the first one
#       double-freed at run time, which is why they are pinned here.
#   [6] A BORROW IS STILL A BORROW: passing the handle twice in one expression
#       leaves live == 1, so the fix for [5] did not turn passing into consuming.
#   [7] AN UNKNOWN OPTION IS REFUSED BY NAME (cli.parse_checked, #29).
#
# Every run is bounded by timeout(1).
set -u
cd "$(dirname "$0")/../.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
RECORD="${RECORD:-0}"
golden="tools/tycho-fh/fh.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
note() { echo "FAIL $1"; fail=1; }

cc -O2 -c tools/tycho-fh/fh.c -o "$T/fh.o" > "$T/cc.log" 2>&1 || {
    echo "fh-check: FAILED (the C shim does not compile)"; tail -3 "$T/cc.log"; exit 1; }
ar rcs "$T/libfhdemo.a" "$T/fh.o" || { echo "fh-check: FAILED (ar)"; exit 1; }

build() {   # build <src.ty> <out-prefix> <logfile>
    $TYCHOC --emit-c -o "$2" "$1" > "$3" 2>&1 || return 1
    cc -O2 -o "$2.bin" "$2.c" $($TYCHOC --print-shims 2>/dev/null) -L"$T" -lfhdemo -lm -lpthread >> "$3" 2>&1
}

build tools/tycho-fh/main.ty "$T/prog" "$T/build.log" || {
    echo "fh-check: FAILED (tycho-fh does not build)"; tail -3 "$T/build.log"; exit 1; }

printf 'one two\nthree\n' > "$T/t.txt"       # 2 lines, 3 words, 14 bytes

# [1] two runs, identical, first equal to the golden
timeout 20 "$T/prog.bin" --file "$T/t.txt" > "$T/one.txt" 2>&1 || note "[1] first run exited non-zero"
timeout 20 "$T/prog.bin" --file "$T/t.txt" > "$T/two.txt" 2>&1 || note "[1] second run exited non-zero"
cmp -s "$T/one.txt" "$T/two.txt" || note "[1] two runs printed different output"

if [ "$RECORD" = 1 ]; then
    sed "s|$T|TMP|g" "$T/one.txt" > "$golden"; echo "rec  $golden"
else
    [ -f "$golden" ] || { echo "fh-check: FAILED (no golden -- run RECORD=1)"; exit 1; }
    sed "s|$T|TMP|g" "$T/one.txt" > "$T/norm.txt"
    cmp -s "$T/norm.txt" "$golden" || { note "[1] output differs from the golden"; diff "$golden" "$T/norm.txt" | head -8; }
fi

# [2]+[3] the counts and the balance, against literals. 64 reps x (2+3+14) = 1216.
cat > "$T/want.txt" <<WANT
TMP/t.txt 2 3 14
total 2 3 14
first 111
live 0
balance 0
reread 1216
WANT
sed "s|$T|TMP|g" "$T/one.txt" > "$T/norm.txt"
cmp -s "$T/norm.txt" "$T/want.txt" || { note "[2] the counts/balance are not the expected ones"; diff "$T/want.txt" "$T/norm.txt" | head -6; }

# [4] 20000 opens: no fd leak, and every reopen really read (20000 x 19)
timeout 60 "$T/prog.bin" --file "$T/t.txt" --stress > "$T/stress.txt" 2>&1 || note "[4] the --stress run exited non-zero"
grep -q '^live 0$'    "$T/stress.txt" || { note "[4] live is not 0 after 20000 opens -- positive is a leak, negative a double free"; grep '^live' "$T/stress.txt"; }
grep -q '^balance 0$' "$T/stress.txt" || { note "[4] opens and closes do not balance over 20000 scopes"; grep '^balance' "$T/stress.txt"; }
grep -q '^reread 380000$' "$T/stress.txt" || { note "[4] the re-read checksum moved -- an open began failing part-way"; grep '^reread' "$T/stress.txt"; }

# [5] the affine refusals. Each probe is its own directory: tychoc compiles every
# .ty beside the entry file, so two probes in one directory collide on `main`.
PRE='package main
handle File:
    free: fh_close
extern "fhdemo" fn fh_open(path: string, mode: string) -> File
extern "fhdemo" fn fh_close(f: File) -> int
extern "fhdemo" fn fh_getc(f: File) -> int
extern "fhdemo" fn fh_live() -> int'
probe() {   # probe <name> <body> <text the refusal must contain>
    _n=$1; _b=$2; _w=$3
    _d="$T/p_$(echo "$_n" | tr -cd 'a-z')"
    mkdir -p "$_d"
    printf '%s\n%s\n' "$PRE" "$_b" > "$_d/main.ty"
    if $TYCHOC --emit-c -o "$_d/p" "$_d/main.ty" > "$_d/err" 2>&1; then
        note "[5] $_n COMPILED -- a handle may be aliased, so its destructor runs twice on one pointer"
    else
        grep -q "$_w" "$_d/err" || { note "[5] $_n was refused, but not for the affine reason"; head -1 "$_d/err"; }
    fi
}
probe "declcopy" 'fn main():
    f := fh_open("/etc/hostname", "r")
    g := f
    println(str(fh_live()))'                                    'handle'
probe "structfield" 'struct S:
    f: File
fn main():
    println("x")'                                               'handle'
probe "returned" 'fn mk() -> File:
    return fh_open("/etc/hostname", "r")
fn main():
    println("x")'                                               'cannot return a handle'
probe "array" 'fn main():
    f := fh_open("/etc/hostname", "r")
    xs := [f]
    println(str(len(xs)))'                                      'container/aggregate'
probe "option" 'fn main():
    f := fh_open("/etc/hostname", "r")
    o := Some(f)
    println("x")'                                               'container/aggregate'
probe "closecall" 'fn main():
    close(fh_open("/etc/hostname", "r"))
    println("x")'                                               'handle variable'

# [6] a borrow is still a borrow -- the [5] fix must not make passing consume
mkdir -p "$T/borrow"
printf '%s\n%s\n' "$PRE" 'fn main():
    f := fh_open("/etc/hostname", "r")
    println(str(fh_getc(f)) + " " + str(fh_getc(f)) + " live=" + str(fh_live()))' > "$T/borrow/main.ty"
if build "$T/borrow/main.ty" "$T/borrow/b" "$T/borrow.log"; then
    out=$(timeout 10 "$T/borrow/b.bin" 2>&1)
    case "$out" in
        *"live=1"*) : ;;
        *) note "[6] passing a handle twice no longer leaves one live owner: $out" ;;
    esac
else
    note "[6] a plain double borrow stopped compiling -- passing a handle must stay a BORROW"
    head -2 "$T/borrow.log"
fi

# [7] an unknown option is refused by name
timeout 10 "$T/prog.bin" --file "$T/t.txt" --fyle x > "$T/unk.txt" 2>&1
[ $? -ne 0 ] || note "[7] an unknown option exited 0"
grep -q -- "--fyle" "$T/unk.txt" || note "[7] the unknown-option message does not name the option"

[ "$fail" = 0 ] || { echo "fh-check: FAILED"; exit 1; }
echo "tycho-fh: green (run identical over 2 runs and equal to the golden; counts and balance against literals; live=0 and opens==closes after 64 scope exits and again after 20000, with a re-read checksum that catches an open that began failing; all SIX affine shapes -- a decl copy, a bare handle struct field, a handle returned, in an array, in an Option, and close() on a call result -- refused; a double borrow still leaves exactly one live owner; an unknown option refused by name)"
