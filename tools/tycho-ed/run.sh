#!/bin/sh
# Gate for tycho-ed, the terminal text editor in tools/tycho-ed/ -- buf/ (the
# line buffer, the byte/codepoint boundary and the undo journal) and main.ty
# (the --script driver that stands in for a terminal).
#
# Re-record the golden with:  RECORD=1 sh tools/tycho-ed/run.sh
#
# WHY THIS IS NOT A GOLDEN LANE WITH EXTRA STEPS. The subject is UTF-8, and a
# recorded transcript is the one instrument that cannot see a UTF-8 bug: a
# backspace that removes one BYTE of "é" leaves a lone 0xc3 that most terminals
# and every diff render as a plausible-looking line, and `cmp` against a golden
# recorded from the same broken build is green by construction. So the numbers
# -- 13 bytes to 11, 11 codepoints to 10 -- are asserted against literals HERE,
# where RECORD=1 cannot reach them. The golden is leg [1] of five and the
# weakest of them.
#
# WHAT IT ASSERTS
#   [1] THE DEMO TRANSCRIPT, twice. `--script=demo.ed` is run twice and both
#       runs must be cmp-identical to each other and the first to the golden.
#       The editor takes no paths, reads no clock and spawns nothing, so a
#       difference between two runs is uninitialised state, not scheduling.
#   [2] UTF-8 BY BYTE COUNT, against literals in this runner. Two deletes at
#       two widths, each asserted as a pair of numbers and the whole hex dump
#       of the line:
#         - backspace over "é" (2 bytes): 13 bytes -> 11, 11 codepoints -> 10,
#           and the surviving bytes are `68 6c 6c 6f ...` with no stray c3.
#         - forward delete of "字" (3 bytes): 19 -> 16, 13 -> 12.
#       The codepoint count comes from utf8.count, which returns -1 on a
#       malformed line and makes `dump` print INVALID UTF-8 -- so the pair of
#       numbers catches both the wrong-width delete and the mid-sequence one.
#       A byte-wise bug still produces plausible TEXT; it cannot produce these
#       numbers.
#   [3] UNDO AND REDO ROUND TRIP, on a script this runner writes itself. Six
#       edits (two of them multi-byte), then six undos, then six redos. The
#       buffer at the bottom must be empty -- 1 line, 0 bytes, 0 codepoints --
#       and the dump after the redos must be BYTE-IDENTICAL to the dump before
#       the undos, cursor and journal depths included. The demo only undoes
#       past the bottom and redoes three; the closed loop is what proves the
#       journal inverts exactly, and nothing else in the tree runs it.
#   [4] EVERY BufErr VARIANT exits NON-ZERO with its own whole message and an
#       empty stdout. --script deliberately does NOT die on one -- it prints
#       `ERR ...` and keeps going, because a script is a test and an error is
#       an observation (tools/tycho-ed/main.ty:32-33) -- so this needs a
#       different caller:
#       the runner copies buf/ into its temp dir and builds a probe whose
#       `main` returns Err(buf.err_str(e)). Nothing is written into the repo.
#       Each arm calls the API that OWNS the variant rather than constructing
#       the enum, and the variant list is READ out of the enum, so a variant
#       added tomorrow reddens here instead of arriving ungated.
#   [5] THE ERRORS ARE REACHED FROM A SCRIPT TOO. Four of the seven variants
#       are provoked by demo.ed itself; this asserts their `ERR` lines against
#       literals, for the same reason as [2].
#   [6] THE GAP BACKEND IS THE SAME EDITOR. `--backend=gap` replaces the line
#       being edited with a mutable byte array and a hole, so an insert writes
#       into the hole instead of rebuilding the line
#       (tools/tycho-ed/buf/buf.ty@_g_focus). It is a PERFORMANCE change and it
#       is allowed to move no output at all: demo.ed and the [3] roundtrip are
#       both re-run under it and must be cmp-identical to the string-backend
#       runs above. That is strictly stronger than a golden of its own -- the
#       byte counts, the hex dumps and the UTF-8 literals in [2] all transfer by
#       construction, and a gap that split a codepoint, dropped the tail after
#       the hole, or forgot to flush before a `dump` moves a byte and reddens.
#       The measurement the backend exists for is NOT asserted, for the reason
#       below; it is recorded in main.ty's header and docs/thesis.md §4b.
#
# WHAT IT DELIBERATELY DOES NOT ASSERT
#   `--stress N`, under EITHER backend. It is a MEASUREMENT -- ns/edit per
#   bucket -- and its numbers move with the machine and the load. What it
#   measured, and the controls that make the reading credible, are recorded in
#   main.ty's header with the date, and the result is written up in
#   docs/thesis.md §4b. A gate that asserted a timing would be a coin toss;
#   nothing here runs it. [6] gates the gap backend's OUTPUT, which is the half
#   that can be wrong silently.
#   The terminal. There is no terminal in this slice -- that is main.ty's first
#   paragraph, not an omission here.
#
# NO HOST DETAIL REACHES THE GOLDEN -- the program prints no paths. Every run
# below is bounded by $TO where a timeout(1) exists: a gate that HANGS tells a
# reader nothing, and tools/tycho-db/run.sh hit exactly that with a bare `wait`.
set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "tycho-ed: no ./tychoc -- run 'make' first"; exit 2; }
TYCHOC="$PWD/tychoc"          # absolute: the probe in [4] is built after a cd
export TYCHO_CORELIB="$PWD/corelib"
RECORD="${RECORD:-0}"
golden="$PWD/tools/tycho-ed/expected.out"
src="$PWD/tools/tycho-ed"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT INT TERM
fail=0
bad() { echo "FAIL: $*"; fail=1; }

# Nothing here spawns a background process, so there is no `wait` to hang on.
# What can hang is a run: `for at > 0 and ...` in _prev_start walks backwards
# over continuation bytes, and a stepping bug that stops decrementing is an
# infinite loop, not a wrong answer. An unbounded gate would sit there until
# CI's own timeout killed it with no verdict.
if command -v timeout >/dev/null 2>&1; then TO="timeout 60"
elif command -v gtimeout >/dev/null 2>&1; then TO="gtimeout 60"
else TO=""; fi

ED="$T/tycho-ed"
if ! "$TYCHOC" "$src/main.ty" -o "$ED" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/build.log"
    echo "tycho-ed: FAIL"; exit 1
fi

out="$T/all.out"
: > "$out"

# `edrun <label> <script> <file>` -- one bounded run, exit 0 and a silent stderr
# required. A run that dies or warns is a failure whatever its stdout says.
edrun() {
    _lbl=$1; _s=$2; _f=$3; _be=${4:-}
    $TO "$ED" "--script=$_s" $_be > "$_f" 2> "$T/e.err"
    _rc=$?
    [ "$_rc" -eq 0 ] || { bad "$_lbl: exited $_rc, expected 0"; sed 's/^/      /' "$T/e.err"; }
    [ -s "$T/e.err" ] && { bad "$_lbl: wrote to stderr"; sed 's/^/      /' "$T/e.err"; }
    return 0
}

# ---------------------------------------------------------------------------
# [1] the demo, twice
# ---------------------------------------------------------------------------
[ -f "$src/demo.ed" ] || bad "demo: $src/demo.ed is gone -- legs [1], [2] and [5] assert NOTHING"
edrun "demo run 1" "$src/demo.ed" "$T/d.1"
edrun "demo run 2" "$src/demo.ed" "$T/d.2"
cmp -s "$T/d.1" "$T/d.2" || {
    bad "the demo transcript is not deterministic (run 1 vs run 2)"
    diff "$T/d.1" "$T/d.2" | sed 's/^/      /'
}

printf '=== demo\n' >> "$out"
cat "$T/d.1" >> "$out"

# ---------------------------------------------------------------------------
# [2] UTF-8, by the numbers
#
# Asserted HERE and not left to the golden: RECORD=1 rewrites the golden, and
# "one keystroke removes one CHARACTER" is precisely the claim that must not be
# blessable from a broken build. Each pair is the same line before and after one
# delete, so a width that is wrong in either direction moves one of the two
# numbers and the pair stops matching.
# ---------------------------------------------------------------------------
ln_() {
    grep -qxF "$1" "$T/d.1" || bad "utf8: expected line missing -- '$1'"
}
# Backspace over a 2-byte codepoint. 13 -> 11 bytes is the width; 11 -> 10
# codepoints is the count still parsing, which is what rules out a delete that
# took one byte and left a lone lead behind.
ln_ '  line 1: 13 bytes, 11 codepoints'
ln_ '  bytes: 68 c3 a9 6c 6c 6f 20 77 c3 b6 72 6c 64'
ln_ '  line 1: 11 bytes, 10 codepoints'
ln_ '  bytes: 68 6c 6c 6f 20 77 c3 b6 72 6c 64'
# Forward delete of a 3-byte codepoint, at the other width. 19 -> 16 and
# 13 -> 12, and the untouched 字-sized neighbour (e6 96 87) survives whole.
ln_ '  line 1: 19 bytes, 13 codepoints'
ln_ '  bytes: 68 c3 a9 6c 6c 6f 20 77 c3 b6 72 6c 64 e5 ad 97 e6 96 87'
ln_ '  line 1: 16 bytes, 12 codepoints'
ln_ '  bytes: 68 c3 a9 6c 6c 6f 20 77 c3 b6 72 6c 64 e6 96 87'
# Stepping right four characters over "héllo" must land on BYTE 5, not 4 --
# one 2-byte codepoint crossed as a single step.
ln_ '  --- cursor line 1 char 4 (byte 5)  undo 6 redo 0 ---'
# And the whole-buffer counter must never report a malformed line. `dump`
# prints this instead of a codepoint count when utf8.count returns -1, so one
# grep covers every dump in the transcript.
grep -n 'INVALID UTF-8' "$T/d.1" | sed 's/^/      /' | grep . && \
    bad "a dump reported INVALID UTF-8 -- an edit left a line that is not a codepoint sequence"

# ---------------------------------------------------------------------------
# [3] undo to empty, and redo back
#
# The demo undoes past the bottom and redoes three of twelve, which proves the
# journal does not corrupt the buffer but never closes the loop. This does:
# six edits, six undos, six redos, and the two dumps either side must be
# byte-identical -- text, cursor line, cursor CHARACTER column (not byte), and
# both journal depths. An undo that restored the text but not the cursor, or
# that left an entry behind, moves one of those fields.
# ---------------------------------------------------------------------------
cat > "$T/roundtrip.ed" <<'EOF'
# Six edits: an insert with a 2-byte codepoint, a split, an insert with two
# 3-byte ones, a backspace over one of them, a second split, a plain insert.
# Every Op variant the journal records -- Ins, Del, Split, Join is exercised by
# the demo -- appears here except Join, whose inverse is Split.
ins héllo
nl
ins 字文
bs
nl
ins ok
dump
undo
undo
undo
undo
undo
undo
dump
redo
redo
redo
redo
redo
redo
dump
EOF
edrun "roundtrip" "$T/roundtrip.ed" "$T/rt.out"
# Split the three dumps out. A dump runs from its `--- N lines` header to its
# `--- cursor` footer inclusive; nothing else in the transcript matches either.
awk '
    /^  --- [0-9]+ lines,/ { n++; on = 1 }
    on { print > (out "." n) }
    /^  --- cursor / { on = 0 }
' out="$T/rt.dump" "$T/rt.out"
if [ ! -f "$T/rt.dump.3" ]; then
    bad "roundtrip: fewer than 3 dumps in the transcript -- this leg asserts NOTHING"
    sed 's/^/      /' "$T/rt.out" | head -20
else
    [ -f "$T/rt.dump.4" ] && bad "roundtrip: more than 3 dumps -- the script and this runner disagree"
    grep -qxF '  --- 1 lines, 0 bytes, 0 codepoints ---' "$T/rt.dump.2" || {
        bad "roundtrip: six undos did not empty the buffer"
        sed 's/^/      /' "$T/rt.dump.2"
    }
    grep -qxF '  --- cursor line 0 char 0 (byte 0)  undo 0 redo 6 ---' "$T/rt.dump.2" || {
        bad "roundtrip: at the bottom the journal is not empty with all six edits redoable"
        sed 's/^/      /' "$T/rt.dump.2"
    }
    cmp -s "$T/rt.dump.1" "$T/rt.dump.3" || {
        bad "roundtrip: six undos and six redos did not return the buffer to where it started"
        diff "$T/rt.dump.1" "$T/rt.dump.3" | sed 's/^/      /'
    }
    # The floor: if the edits ever stopped producing a multi-byte buffer, the
    # cmp above would still pass and prove nothing about UTF-8.
    grep -qxF '  --- 3 lines, 13 bytes, 10 codepoints ---' "$T/rt.dump.1" || {
        bad "roundtrip: the starting buffer is not the 13-byte/10-codepoint one this leg was written for"
        sed 's/^/      /' "$T/rt.dump.1"
    }
fi
printf '=== roundtrip (6 edits, 6 undos, 6 redos)\n' >> "$out"
cat "$T/rt.out" >> "$out"

# ---------------------------------------------------------------------------
# [4] every BufErr variant, exiting non-zero with its own whole message
#
# The --script driver REPORTS these and exits 0 by design. A caller that
# propagates one has to die by it, and this is that caller:
# `main() -> Result(void, string)` returning Err(buf.err_str(e)) puts the
# message on stderr and the failure in the exit status. buf/ is COPIED into the
# temp dir -- nothing is written into the repo, and a renamed package reddens
# here.
# ---------------------------------------------------------------------------
P="$T/pkg"; mkdir -p "$P"
[ -d "$src/buf" ] || bad "probe: $src/buf is gone -- this leg asserts NOTHING"
cp -R "$src/buf" "$P/" 2>/dev/null
cat > "$P/probe.ty" <<'EOF'
package main

import "buf"

# One variant per run, named on the command line. Each arm calls the API that
# owns the variant rather than constructing the enum -- a probe that built
# BufErr by hand would assert err_str and nothing else.
fn main() -> Result(void, string):
    a := args()
    if len(a) < 2:
        return Err("usage: probe <variant>")
    if a[1] == "BadLine":
        b := buf.from_text("one")
        match buf.goto(&b, 5, 0):
            Ok(): return Err("goto ACCEPTED a line the buffer does not have")
            Err(e): return Err(buf.err_str(e))
    if a[1] == "BadCol":
        b := buf.from_text("héllo")
        match buf.goto(&b, 0, 99):
            Ok(): return Err("goto ACCEPTED a column past the end of the line")
            Err(e): return Err(buf.err_str(e))
    if a[1] == "NotUtf8":
        # A lone lead byte, sliced out of a well-formed one so the source file
        # itself stays valid UTF-8. Stepping onto it must refuse rather than
        # guess a width -- guessing is how "delete one character" corrupts a
        # line it merely walked past.
        lead := "é"[0:1]
        b := buf.from_text("x" + lead + "y")
        match buf.goto(&b, 0, 3):
            Ok(): return Err("goto WALKED a byte sequence that is not UTF-8")
            Err(e): return Err(buf.err_str(e))
    if a[1] == "NothingToUndo":
        b := buf.from_text("")
        match buf.undo(&b):
            Ok(): return Err("undo ACCEPTED an empty journal")
            Err(e): return Err(buf.err_str(e))
    if a[1] == "NothingToRedo":
        b := buf.from_text("")
        match buf.redo(&b):
            Ok(): return Err("redo ACCEPTED an empty redo stack")
            Err(e): return Err(buf.err_str(e))
    if a[1] == "AtStart":
        b := buf.from_text("one\ntwo")
        match buf.left(&b):
            Ok(): return Err("left MOVED from the first character of the buffer")
            Err(e): return Err(buf.err_str(e))
    if a[1] == "AtEnd":
        b := buf.from_text("one")
        buf.line_end(&b)
        match buf.right(&b):
            Ok(): return Err("right MOVED from the last character of the buffer")
            Err(e): return Err(buf.err_str(e))
    return Err("unknown variant " + a[1])
EOF
if ! "$TYCHOC" "$P/probe.ty" -o "$T/probe" >"$T/probe.log" 2>&1; then
    bad "probe: tychoc could not build the BufErr probe"
    sed 's/^/      /' "$T/probe.log" | head -8
else
    # <variant> <the whole message it must die with>
    errcase() {
        _v=$1; _msg=$2
        $TO "$T/probe" "$_v" > "$T/c.out" 2> "$T/c.err"
        _rc=$?
        if [ "$_rc" -eq 0 ]; then
            bad "$_v: EXITED 0 -- the API accepted what the variant exists to refuse"
        elif ! grep -qxF "$_msg" "$T/c.err"; then
            bad "$_v: failed but not with its own whole message"; sed 's/^/      /' "$T/c.err"
        fi
        [ -s "$T/c.out" ] && bad "$_v: wrote to STDOUT"
        printf '=== err %s (via the buf API)\n' "$_v" >> "$out"
        cat "$T/c.err" >> "$out"
    }
    errcase BadLine       'no line 5 (buffer has 1)'
    errcase BadCol        'byte column 99 past end of a 5-byte line'
    errcase NotUtf8       'line 0 byte 1 is not a UTF-8 boundary'
    errcase NothingToUndo 'nothing to undo'
    errcase NothingToRedo 'nothing to redo'
    errcase AtStart       'already at start of buffer'
    errcase AtEnd         'already at end of buffer'
fi

# The coverage floor: the enum is READ, not remembered.
COVERED='BadLine BadCol NotUtf8 NothingToUndo NothingToRedo AtStart AtEnd'
found=0
for v in $(awk '
        $0 == "enum BufErr:" { on = 1; next }
        on && /^[^ \t]/ { on = 0 }
        on && $1 ~ /^#/ { next }
        on && NF { v = $1; sub(/\(.*/, "", v); print v }
    ' "$src/buf/buf.ty"); do
    found=$((found + 1))
    hit=0
    for c in $COVERED; do [ "$v" = "$c" ] && hit=1; done
    [ "$hit" -eq 1 ] || bad "BufErr variant $v has no leg in this runner -- it is UNGATED"
done
[ "$found" -eq 7 ] || bad "found $found BufErr variant(s) in buf.ty, expected 7 -- the scan is broken and [4]'s floor asserts nothing"

# ---------------------------------------------------------------------------
# [5] the same errors, reached from a script
#
# [4] proves each variant carries its own message out through a return; this
# proves four of them are reachable by pressing keys, which is the only way a
# user meets them. Against literals, not the golden, same reason as [2].
# ---------------------------------------------------------------------------
ln_ '  ERR already at start of buffer'
ln_ '  ERR already at end of buffer'
ln_ '  ERR nothing to undo'
# The surplus undos at the end of demo.ed must report, not corrupt: the buffer
# under them is the empty one, and the journal has every edit redoable.
ln_ '  --- cursor line 0 char 0 (byte 0)  undo 0 redo 12 ---'

# ---------------------------------------------------------------------------
# [6] the gap backend, byte-for-byte the same editor
#
# Nothing is appended to $out: the assertion is EQUALITY WITH THE RUNS ABOVE,
# which the golden already covers. A second recorded transcript would only add
# a thing RECORD=1 can bless.
# ---------------------------------------------------------------------------
edrun "demo run 3 (gap)" "$src/demo.ed" "$T/d.gap" --backend=gap
cmp -s "$T/d.1" "$T/d.gap" || {
    bad "the gap backend changes the demo transcript -- it is a representation, not a behaviour"
    diff "$T/d.1" "$T/d.gap" | sed 's/^/      /'
}
edrun "roundtrip (gap)" "$T/roundtrip.ed" "$T/rt.gap" --backend=gap
cmp -s "$T/rt.out" "$T/rt.gap" || {
    bad "the gap backend does not undo and redo to the same bytes as the string backend"
    diff "$T/rt.out" "$T/rt.gap" | sed 's/^/      /'
}
# The floor: if --backend=gap were silently ignored, both cmps above would pass
# and this leg would assert nothing. An unknown argument is a hard error in
# main(), so a rejected flag proves the selector is wired.
$TO "$ED" "--script=$src/demo.ed" --backend=nonesuch >/dev/null 2>&1 && \
    bad "--backend=nonesuch was ACCEPTED -- the selector is not parsed, so [6] proves nothing"

# ---------------------------------------------------------------------------
# the golden
# ---------------------------------------------------------------------------
if [ "$RECORD" = 1 ]; then
    if [ "$fail" -ne 0 ]; then echo "FAIL: refusing to RECORD from a failed run"; exit 1; fi
    cp "$out" "$golden"; echo "rec  tycho-ed"
fi
if [ ! -f "$golden" ]; then
    bad "no golden -- run RECORD=1 sh tools/tycho-ed/run.sh"
elif ! cmp -s "$out" "$golden"; then
    bad "transcript != golden"; diff "$golden" "$out" | sed 's/^/      /'
fi

if [ "$fail" -eq 0 ]; then
    echo "tycho-ed: green (demo transcript byte-identical over 2 runs and equal to the golden; a backspace over a 2-byte codepoint took 13 bytes to 11 and 11 codepoints to 10, a forward delete of a 3-byte one took 19 to 16 and 13 to 12, and no dump reported INVALID UTF-8; six edits undone to an empty buffer and redone back to a byte-identical dump; $found BufErr variants each exit non-zero with their own whole message and an empty stdout; the gap backend reproduces the demo and the roundtrip byte-for-byte and an unknown --backend is refused)"
else
    echo "tycho-ed: FAIL"; exit 1
fi
