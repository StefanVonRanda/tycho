set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "tycho-vm: no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
RECORD="${RECORD:-0}"
golden="$PWD/tools/tycho-vm/expected.out"
progs="$PWD/tools/tycho-vm/progs"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL: $*"; fail=1; }

VM="$T/tycho-vm"
if ! "$TYCHOC" tools/tycho-vm/main.ty -o "$VM" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/build.log"
    echo "tycho-vm: FAIL"; exit 1
fi

W="$T/w"; mkdir -p "$W"
cd "$W" || exit 2
out="$T/all.out"
: > "$out"

# ---------------------------------------------------------------------------
# [1] asm is deterministic, and [2] dis round-trips
#
# Two assemblies of the same source must be byte-identical -- the writer must
# not iterate a map or embed a path. Then the disassembly is fed back to the
# assembler: dis prints jump targets as absolute pcs and re-uses the assembler's
# own escape vocabulary for constants, so the second .tyc must equal the first.
# A dropped operand, a renamed mnemonic or an escape the printer emits and the
# parser rejects all land here.
# ---------------------------------------------------------------------------
for p in fib gcd sort; do
    for n in 1 2; do
        "$VM" asm "$progs/$p.tasm" -o "$W/$p.$n.tyc" 2>"$T/e" || {
            bad "asm $p (run $n) exited non-zero"; sed 's/^/      /' "$T/e"; continue
        }
    done
    cmp -s "$W/$p.1.tyc" "$W/$p.2.tyc" || bad "asm $p is not deterministic"

    "$VM" dis "$W/$p.1.tyc" > "$W/$p.dis.tasm" 2>"$T/e" || {
        bad "dis $p exited non-zero"; sed 's/^/      /' "$T/e"; continue
    }
    "$VM" asm "$W/$p.dis.tasm" -o "$W/$p.rt.tyc" 2>"$T/e" || {
        bad "re-asm of dis $p exited non-zero"; sed 's/^/      /' "$T/e"; continue
    }
    cmp -s "$W/$p.1.tyc" "$W/$p.rt.tyc" || bad "dis $p does not round-trip"
done

# ---------------------------------------------------------------------------
# [3] the listings and the program output, against the golden
# ---------------------------------------------------------------------------
for p in fib gcd sort; do
    printf '=== dis %s\n' "$p" >> "$out"
    cat "$W/$p.dis.tasm" >> "$out"
done

for p in fib gcd sort; do
    printf '=== run %s\n' "$p" >> "$out"
    "$VM" run "$W/$p.1.tyc" > "$T/o" 2> "$T/e"
    rc=$?
    cat "$T/o" >> "$out"
    [ "$rc" -eq 0 ] || { bad "run $p: exited $rc, expected 0"; sed 's/^/      /' "$T/e"; }
    [ -s "$T/e" ] && { bad "run $p: wrote to stderr"; sed 's/^/      /' "$T/e"; }
done

for p in fib gcd sort; do
    "$VM" trace "$W/$p.1.tyc" > "$W/$p.tr1" 2>"$T/e" || {
        bad "trace $p exited non-zero"; sed 's/^/      /' "$T/e"; continue
    }
    "$VM" trace "$W/$p.1.tyc" > "$W/$p.tr2" 2>/dev/null
    cmp -s "$W/$p.tr1" "$W/$p.tr2" || bad "trace $p is not deterministic"
done

printf '=== trace fib (first 20 lines)\n' >> "$out"
head -n 20 "$W/fib.tr1" >> "$out"
printf '=== trace lines\n' >> "$out"
for p in fib gcd sort; do
    printf '%s %s\n' "$p" "$(wc -l < "$W/$p.tr1" | tr -d ' ')" >> "$out"
done

if [ "$RECORD" = 1 ]; then
    if [ "$fail" -ne 0 ]; then echo "FAIL: refusing to RECORD from a failed run"; exit 1; fi
    cp "$out" "$golden"; echo "rec  tycho-vm"
fi
if [ ! -f "$golden" ]; then
    bad "no golden -- run RECORD=1 sh tools/tycho-vm/run.sh"
elif ! cmp -s "$out" "$golden"; then
    bad "transcript != golden"; diff "$golden" "$out" | sed 's/^/      /'
fi

# ---------------------------------------------------------------------------
# [5] the runtime traps
#
# traps <label> <expected stderr substring> <prog.tyc> -- non-zero exit, that
# substring on stderr, stderr beginning `pc <n>:`, and ZERO bytes on stdout.
# The empty-stdout half is the load-bearing half: a VM that prints half a
# program's output and then dies hands the caller a truncated result it cannot
# tell is truncated.
# ---------------------------------------------------------------------------
traps() {
    _lbl=$1; _want=$2; _tyc=$3
    "$VM" run "$_tyc" > "$T/r.out" 2> "$T/r.err"
    _rc=$?
    if [ "$_rc" -eq 0 ]; then
        bad "$_lbl: EXITED 0 -- the trap did not fire"
    elif ! grep -qF "$_want" "$T/r.err"; then
        bad "$_lbl: failed but not for the expected reason (want: $_want)"
        sed 's/^/      /' "$T/r.err"
    elif ! grep -qE '^pc [0-9]+:' "$T/r.err"; then
        bad "$_lbl: the message does not name a pc"
        sed 's/^/      /' "$T/r.err"
    fi
    if [ -s "$T/r.out" ]; then
        bad "$_lbl: wrote $(wc -c < "$T/r.out") bytes to STDOUT before failing"
    fi
}

# asmok <name> -- assemble $W/<name>.tasm, which must succeed. These fixtures
# are ACCEPTED by the assembler and only fail when executed; a failure here
# would mean the assembler grew a check and the trap below stopped being
# reachable, which is worth a distinct message.
asmok() {
    "$VM" asm "$W/$1.tasm" -o "$W/$1.tyc" 2>"$T/e" ||
        { bad "$1.tasm: expected to ASSEMBLE, but asm failed"; sed 's/^/      /' "$T/e"; }
}

cat > "$W/divzero.tasm" <<'EOF'
    PUSH 1
    PUSH 0
    DIV
    HALT
EOF
asmok divzero
traps 'division by zero' 'division by zero' "$W/divzero.tyc"

cat > "$W/underflow.tasm" <<'EOF'
    POP
    HALT
EOF
asmok underflow
traps 'stack underflow' 'operand stack underflow' "$W/underflow.tyc"

# 1024 pushes with no pop. A loop rather than 1025 literal lines, so the
# fixture stays readable and STACKMAX can move without editing it.
cat > "$W/overflow.tasm" <<'EOF'
loop:
    PUSH 1
    JMP loop
EOF
asmok overflow
traps 'stack overflow' 'operand stack overflow' "$W/overflow.tyc"

# The assembler rejects a target ABOVE len(code) but allows one EQUAL to it --
# the one-past-the-end address, which is exactly the off-by-one a jump table
# produces. It is the runtime that refuses it, and this is that leg.
cat > "$W/badjump.tasm" <<'EOF'
    PUSH 1
    POP
    JMP 3
EOF
asmok badjump
traps 'jump out of range' 'jump out of range: 3' "$W/badjump.tyc"

# operand() only rejects a NEGATIVE slot, so an out-of-frame one assembles.
cat > "$W/badslot.tasm" <<'EOF'
    LOAD 99
    HALT
EOF
asmok badslot
traps 'bad slot index' 'bad slot index 99' "$W/badslot.tyc"

cat > "$W/deep.tasm" <<'EOF'
    CALL f 0
    HALT
f:
    CALL f 0
    RET
EOF
asmok deep
traps 'call depth exceeded' 'call depth exceeded' "$W/deep.tyc"

cat > "$W/carnonpair.tasm" <<'EOF'
    PUSH 1
    CAR
    HALT
EOF
asmok carnonpair
traps 'car/cdr of a non-pair' 'car/cdr of a non-pair' "$W/carnonpair.tyc"

cat > "$W/setcarnonpair.tasm" <<'EOF'
    PUSH 1
    PUSH 2
    SET-CAR
    HALT
EOF
asmok setcarnonpair
traps 'set-car! on a non-pair' 'set-car! on a non-pair' "$W/setcarnonpair.tyc"

# MAXCELLS conses with no reuse. A loop, like the stack-overflow fixture, so
# MAXCELLS can move without editing it.
cat > "$W/heapfill.tasm" <<'EOF'
loop:
    NIL
    NIL
    CONS
    POP
    JMP loop
EOF
asmok heapfill
traps 'pair heap overflow' 'pair heap overflow' "$W/heapfill.tyc"

# THE HEX-PATCHED ONE. `LOADK 9` is refused at assembly time -- operand()
# range-checks a K_CONST against the pool it has read so far -- so the only
# route to the runtime's own `bad const index` is to assemble a VALID LOADK and
# edit the encoded operand.
#
# The container is fixed-width and documented in main.ty's header: "TYC1" + u32
# nconsts + u32 ncode is 12 bytes, one const of length 1 is u32 + 1 byte = 5,
# so code starts at 17; each instruction is u8 op + i32 a + i32 b, so the low
# byte of instruction 0's `a` is at offset 18. Byte 17 is asserted to be
# OP_LOADK (1) BEFORE the patch, so a layout change fails here loudly instead
# of silently patching some other field and testing nothing.
cat > "$W/badconst.tasm" <<'EOF'
.const "x"

    LOADK 0
    PRINT
    HALT
EOF
asmok badconst
opbyte=$(od -An -tu1 -j17 -N1 "$W/badconst.tyc" | tr -d ' \n')
if [ "$opbyte" != "1" ]; then
    bad "badconst: byte 17 is $opbyte, expected 1 (OP_LOADK) -- the .tyc layout moved"
else
    printf '\143' | dd of="$W/badconst.tyc" bs=1 seek=18 conv=notrunc status=none
    traps 'bad const index' 'bad const index 99' "$W/badconst.tyc"
fi

# ---------------------------------------------------------------------------
# [6] malformed .tasm
#
# refuses <label> <expected stderr substring> <name> -- `asm` must exit
# non-zero, name a LINE, and print nothing on stdout. Every fixture puts its
# error on line 3, so a diagnostic hardcoded to line 1 cannot pass.
# ---------------------------------------------------------------------------
refuses() {
    _lbl=$1; _want=$2; _n=$3
    "$VM" asm "$W/$_n.tasm" -o "$W/$_n.tyc" > "$T/r.out" 2> "$T/r.err"
    _rc=$?
    if [ "$_rc" -eq 0 ]; then
        bad "$_lbl: EXITED 0 -- the source was accepted"
    elif ! grep -qF "$_want" "$T/r.err"; then
        bad "$_lbl: failed but not for the expected reason (want: $_want)"
        sed 's/^/      /' "$T/r.err"
    fi
    if [ -s "$T/r.out" ]; then
        bad "$_lbl: wrote $(wc -c < "$T/r.out") bytes to STDOUT before failing"
    fi
}

cat > "$W/badmnem.tasm" <<'EOF'
# line 1
    PUSH 1
    FROB 1
    HALT
EOF
refuses 'unknown mnemonic' 'line 3: unknown mnemonic: FROB' badmnem

cat > "$W/badlabel.tasm" <<'EOF'
# line 1
    PUSH 1
    JMP nowhere
    HALT
EOF
refuses 'unknown label' 'line 3: unknown label: nowhere' badlabel

cat > "$W/badarity.tasm" <<'EOF'
# line 1
    PUSH 1
    ADD 1
    HALT
EOF
refuses 'wrong operand count' 'line 3: ADD takes 0 operand(s), got 1' badarity

cat > "$W/badimm.tasm" <<'EOF'
# line 1
    PUSH 1
    PUSH 1x
    HALT
EOF
refuses 'bad immediate' 'line 3: bad immediate: 1x' badimm

if [ "$fail" -eq 0 ]; then
    echo "tycho-vm: green (3 programs assembled twice byte-identically; dis round-trips to the same .tyc; listings + run output == golden; trace deterministic over 3 programs; 10 runtime traps and 4 malformed-source diagnostics all refused with empty stdout)"
else
    echo "tycho-vm: FAIL"; exit 1
fi
