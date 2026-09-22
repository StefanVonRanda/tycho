set -u
cd "$(dirname "$0")/../.." || exit 2
. ./scripts/shlib.sh          # `timeout` is not in the macOS base system
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "no $TYCHOC -- run 'make' first"; exit 2; }
RECORD="${RECORD:-0}"
golden="tools/tycho-du/du.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
note() { echo "FAIL $1"; fail=1; }

# [0] THE BUILD IS A LEG. tycho-du is the only program in the tree that combines
# a `handle` with --shim, which is the pair docs/reference/ffi.md recommends;
# tools/tycho-fh builds its shim into a static library by hand instead, so until
# this existed the recommended spelling was demonstrated nowhere (FRICTION 125,
# the probe's finding 12). If --shim ever stops linking a handle program, this
# is what says so.
$TYCHOC tools/tycho-du/main.ty -o "$T/du" --shim tools/tycho-du/du.c > "$T/build.log" 2>&1 || {
    echo "du-check: FAILED (tycho-du does not build with --shim)"; tail -5 "$T/build.log"; exit 1; }

# The gate's own tree, not the host's: every size is a literal here, so the
# totals below are checkable by reading this file. All four sizes are DISTINCT
# and so are the three per-extension weights, which is what makes the report's
# ordering deterministic without depending on readdir order.
mk() { mkdir -p "$(dirname "$1")"; :> "$1"; i=0; while [ "$i" -lt "$2" ]; do printf 'x' >> "$1"; i=$((i+1)); done; }
mk "$T/tree/b.md"      200
mk "$T/tree/a.txt"     100
mk "$T/tree/sub/c.txt"  50
mk "$T/tree/sub/d.log"   1
mkdir -p "$T/tree/empty"

# [1] two runs, identical, the first equal to the golden
timeout 30 "$T/du" --top 10 "$T/tree" > "$T/one.txt" 2>&1 || note "[1] first run exited non-zero"
timeout 30 "$T/du" --top 10 "$T/tree" > "$T/two.txt" 2>&1 || note "[1] second run exited non-zero"
cmp -s "$T/one.txt" "$T/two.txt" || note "[1] two runs printed different output"

sed "s|$T|TMP|g" "$T/one.txt" > "$T/norm.txt"
if [ "$RECORD" = 1 ]; then
    cp "$T/norm.txt" "$golden"; echo "rec  $golden"
else
    [ -f "$golden" ] || { echo "du-check: FAILED (no golden -- run RECORD=1)"; exit 1; }
    cmp -s "$T/norm.txt" "$golden" || { note "[1] output differs from the golden"; diff "$golden" "$T/norm.txt" | head -10; }
fi

# [2] the totals against literals. 200+100+50+1 = 351 bytes in 4 files, and three
# directories OPENED (tree, sub, empty). A golden alone would go on matching a
# wrong number after a RECORD=1 run, which is why the arithmetic is spelled here.
cat > "$T/want.txt" <<'WANT'
largest files
       200 B  TMP/tree/b.md
       100 B  TMP/tree/a.txt
        50 B  TMP/tree/sub/c.txt
         1 B  TMP/tree/sub/d.log

by extension
       200 B       1  .md
       150 B       2  .txt
         1 B       1  .log

total 351 B in 4 files, 3 directories
WANT
cmp -s "$T/norm.txt" "$T/want.txt" || { note "[2] the totals are not the expected ones"; diff "$T/want.txt" "$T/norm.txt" | head -10; }

# [3] THE LEAK PROOF, which is why this program is in the tree. --selftest walks
# the tree in strict mode N times and reads the shim's own counters back.
#
# [3a] runs over a tree with nothing unreadable in it, so every walk completes
# and the open count is a LITERAL: 200 reps x 3 openable directories (tree, sub, empty) = 600. That
# is the negative control for the counters themselves -- a gate that reads
# `live 0` off a shim which never counted would pass forever.
timeout 60 "$T/du" --selftest=200 "$T/tree" > "$T/self.txt" 2>&1 || note "[3a] --selftest exited non-zero over a fully readable tree"
grep -q '^reps 200 errs 0$' "$T/self.txt" || { note "[3a] a fully readable tree produced errors"; grep '^reps' "$T/self.txt"; }
grep -q '^live 0$' "$T/self.txt" || { note "[3a] live is not 0 after 200 walks -- positive is a leaked DIR*, negative a double free"; grep '^live' "$T/self.txt"; }
grep -q '^ok$'     "$T/self.txt" || note "[3a] --selftest did not report ok"
opens=$(sed -n 's/^opens \([0-9]*\) closes.*/\1/p' "$T/self.txt")
closes=$(sed -n 's/^opens [0-9]* closes \([0-9]*\)$/\1/p' "$T/self.txt")
[ "$opens" = 600 ] || note "[3a] expected 600 opens over 200 walks of 3 openable directories, got $opens"
[ "$opens" = "$closes" ] || note "[3a] opens ($opens) and closes ($closes) do not balance"

# [3b] the same walk with an unreadable directory in it, which is the path that
# matters: every rep now propagates an Err out of a `walk` whose Dir handle is
# still owned -- the `or_return` unwind -- and `live` must STILL be 0. How many
# directories each rep opened before it aborted depends on readdir order, so
# that count is deliberately not asserted; the balance is.
mkdir -p "$T/tree/locked"; chmod 000 "$T/tree/locked" 2>/dev/null
if [ "$(id -u)" = 0 ]; then
    echo "du-check: SKIP legs [3b] and [4] -- running as root, which chmod 000 does not stop"
else
    timeout 60 "$T/du" --selftest=200 "$T/tree" > "$T/self2.txt" 2>&1
    grep -q '^reps 200 errs 200$' "$T/self2.txt" || { note "[3b] 200 strict walks over an unreadable directory did not error 200 times -- the is_null path did not fire"; grep '^reps' "$T/self2.txt"; }
    grep -q '^live 0$' "$T/self2.txt" || { note "[3b] live is not 0 after 200 unwinds -- a handle owned by an unwinding frame was not freed"; grep '^live' "$T/self2.txt"; }
    o2=$(sed -n 's/^opens \([0-9]*\) closes.*/\1/p' "$T/self2.txt")
    c2=$(sed -n 's/^opens [0-9]* closes \([0-9]*\)$/\1/p' "$T/self2.txt")
    [ "$o2" = "$c2" ] || note "[3b] opens ($o2) and closes ($c2) do not balance across 200 error unwinds"
fi

# [4] the unreadable directory is COUNTED, not silently dropped, in the
# non-strict report -- the distinction `list_dir` cannot make and the reason this
# program has a shim at all.
if [ "$(id -u)" != 0 ]; then
    timeout 30 "$T/du" --top 10 "$T/tree" > "$T/locked.txt" 2>&1 || note "[4] the run with an unreadable directory exited non-zero"
    grep -q '1 skipped' "$T/locked.txt" || { note "[4] the unreadable directory was not reported as skipped"; tail -2 "$T/locked.txt"; }
    timeout 30 "$T/du" --strict --top 10 "$T/tree" > "$T/strict.txt" 2>&1 && note "[4] --strict exited ZERO over an unreadable directory"
    grep -q 'cannot open directory' "$T/strict.txt" || { note "[4] --strict did not name the directory it could not open"; head -2 "$T/strict.txt"; }
fi
chmod 755 "$T/tree/locked" 2>/dev/null

# [5] the three handle rules this program is the live example of, refused with a
# real shim behind them rather than in a one-file fixture: an opener result that
# is never bound (FRICTION 123), a `free:` naming a function that was never
# declared, and one declared at the wrong type (FRICTION 126). tests/reject/
# pins all three against a stub; this pins them against the program that would
# leak if they stopped holding.
refuse() {  # refuse <name> <expected-substring> <<source
    cat > "$T/r.ty"
    if $TYCHOC "$T/r.ty" -o "$T/r" --shim tools/tycho-du/du.c > "$T/r.log" 2>&1; then
        note "[5] $1 COMPILED -- it must be refused"
    else
        grep -q "$2" "$T/r.log" || { note "[5] $1 was refused, but not for the stated reason"; head -2 "$T/r.log"; }
    fi
}
refuse "an unbound opener result" "must be bound to a variable" <<'TY'
handle Dir:
    free: dw_close

extern fn dw_open(p: string) -> Dir
extern fn dw_close(d: Dir) -> int

fn main():
    dw_open(".")
TY
refuse "a free: naming an undeclared function" "dw_shut" <<'TY'
handle Dir:
    free: dw_shut

extern fn dw_open(p: string) -> Dir

fn main():
    d := dw_open(".")
    if is_null(d):
        println("no")
TY
refuse "a destructor declared at the wrong type" "dw_close" <<'TY'
handle Dir:
    free: dw_close

extern fn dw_open(p: string) -> Dir
extern fn dw_close(d: ptr) -> int

fn main():
    d := dw_open(".")
    if is_null(d):
        println("no")
TY

# [6] the option surface, by name
timeout 30 "$T/du" --nope "$T/tree" > "$T/opt.txt" 2>&1 && note "[6] an unknown option exited zero"
grep -q 'unknown option' "$T/opt.txt" || note "[6] an unknown option was not named"
timeout 30 "$T/du" --top abc "$T/tree" > "$T/opt2.txt" 2>&1 && note "[6] --top abc exited zero"
grep -q 'wants a number' "$T/opt2.txt" || note "[6] --top abc was not refused by name"

[ "$fail" = 0 ] || { echo "du-check: FAILED"; exit 1; }
echo "du-check: green (built with --shim, the handle-plus-shim pair the FFI docs recommend and nothing else in the tree demonstrates; two runs identical and equal to the golden; 351 B in 4 files over 3 directories against literals; 200 strict walks of a clean tree open and close 600 streams and end live=0; 200 more over an unreadable directory error 200 times, unwinding through or_return out of a frame that still owns its handle, and still end live=0 with opens==closes; the unreadable directory counted as skipped and named under --strict; an unbound opener, an undeclared free: and a destructor at the wrong type all refused with a real shim behind them; unknown option and a non-numeric --top refused by name)"
