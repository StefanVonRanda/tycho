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

# ---------------------------------------------------------------------------
# [7] EVERY EARLY EXIT still frees. [4] proves the destructor runs when a scope
#     ends NORMALLY, 20000 times. The paths where a scope-exit free is actually
#     forgotten are the other ones -- `break`, `continue`, a nested block, and an
#     `or_return` that leaves the function from the middle. The docs claim all of
#     them; a leak on any would be invisible in a transcript and would show up as
#     an fd exhaustion much later, in something else.
#
#     The counts are LITERALS derived from the control flow, not from a run: the
#     loop opens on i=0,1,2,3 (continue at 1 still opened; break at 3 opened
#     first) = 4, the nested block opens on j=1,2 = 2, and the or_return path
#     opens 1. Seven opens, seven closes, live 0.
# ---------------------------------------------------------------------------
mkdir -p "$T/early"
cat > "$T/early/main.ty" <<'TY'
handle File:
    free: fh_close

extern "fhdemo" fn fh_open(path: string, mode: string) -> File
extern "fhdemo" fn fh_close(f: File) -> int
extern "fhdemo" fn fh_live() -> int
extern "fhdemo" fn fh_opens() -> int
extern "fhdemo" fn fh_closes() -> int

fn boom() -> Result(int, string):
    return Err("nope")

fn use() -> Result(int, string):
    _f := fh_open("tools/tycho-fh/fh.c", "r")
    v := boom() or_return
    return Ok(v)

fn loops():
    for i := 0; i < 5; i += 1:
        _f := fh_open("tools/tycho-fh/fh.c", "r")
        if i == 1:
            continue
        if i == 3:
            break
    for j := 0; j < 3; j += 1:
        if j > 0:
            _g := fh_open("tools/tycho-fh/fh.c", "r")
            pass

fn main():
    loops()
    match use():
        Ok(v): pass
        Err(e): pass
    println("live " + str(fh_live()))
    println("opens " + str(fh_opens()))
    println("closes " + str(fh_closes()))
TY
if $TYCHOC "$T/early/main.ty" --emit-c -o "$T/early/g" > "$T/early.log" 2>&1 && cc -O2 -o "$T/early/bin" "$T/early/g.c" $($TYCHOC "$T/early/main.ty" --print-shims 2>/dev/null)       -L"$T" -lfhdemo -lm -lpthread >> "$T/early.log" 2>&1; then
    "$T/early/bin" > "$T/early.txt" 2>&1 || note "[7] the early-exit probe did not run"
    grep -qx 'live 0'    "$T/early.txt" || { note "[7] live is not 0 -- an early exit skipped the destructor"; cat "$T/early.txt"; }
    grep -qx 'opens 7'   "$T/early.txt" || { note "[7] want 7 opens (4 loop + 2 nested + 1 or_return)"; cat "$T/early.txt"; }
    grep -qx 'closes 7'  "$T/early.txt" || { note "[7] want 7 closes -- one early exit did not free"; cat "$T/early.txt"; }
else
    note "[7] the early-exit probe does not build"; tail -3 "$T/early.log"
fi

[ "$fail" = 0 ] || { echo "fh-check: FAILED"; exit 1; }
echo "tycho-fh: green (run identical over 2 runs and equal to the golden; counts and balance against literals; live=0 and opens==closes after 64 scope exits and again after 20000, with a re-read checksum that catches an open that began failing; all SIX affine shapes -- a decl copy, a bare handle struct field, a handle returned, in an array, in an Option, and close() on a call result -- refused; a double borrow still leaves exactly one live owner; an unknown option refused by name)"
