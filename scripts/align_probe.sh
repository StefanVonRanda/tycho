#!/bin/sh
# `align(N) struct` (L1) -- the only lane that reads a REAL ADDRESS.
#
# _Alignof is a compile-time claim about a TYPE and says nothing about where the
# allocator put the value; the one unacceptable outcome for this attribute is a
# declared alignment the arena does not honour. So this includes the emitted
# translation unit (runtime and all) with its `main` renamed, allocates out of a
# real Arena, and checks the addresses it gets back modulo N.
#
# It is also where the >8 REFUSAL is grounded: leg [3] measures that the arena
# does NOT deliver 16-byte alignment, which is why align(16) is a diagnostic.
#
# Runs both compilers: a feature landing in one and not the other is the hole
# scripts/diag_coverage.py exists to keep shut.
set -u
cd "$(dirname "$0")/.." || exit 2
CC="${CC:-cc}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
rc=0
bad() { echo "align-probe: FAIL -- $*"; rc=1; }

cat > "$T/p.ty" <<'TY'
align(8) struct Pair:
    a: u8
    b: u8

fn main():
    p := Pair(to_u8(1), to_u8(2))
    println(str(p.a) + str(p.b))
TY

# The harness. Every leg PRINTS its counts: "not aligned" and "the sweep never
# ran" are different findings and a bare verdict cannot tell them apart.
cat > "$T/probe.c" <<'C'
#define main tycho_program_main
#include "p.c"
#undef main
#include <stdint.h>
struct W16 { unsigned char a, b; } __attribute__((aligned(16)));

static int sweep(Arena *a, size_t sz, size_t al, int n, const char *tag) {
    int off = 0;
    for (int i = 0; i < n; i++)
        if ((uintptr_t)arena_alloc(a, sz) % al) off++;
    printf("%s: %d of %d off a %zu-byte boundary\n", tag, off, n, al);
    return off;
}

int main(void) {
    Arena a = arena_new(0);
    /* [1] single values, the compiler's own emitted struct */
    printf("sizeof=%zu alignof=%zu\n", sizeof(struct S_Pair_), _Alignof(struct S_Pair_));
    if (sweep(&a, sizeof(struct S_Pair_), 8, 4096, "[1] Pair"))
        { puts("FAIL [1]"); return 1; }
    /* [2] the shape a `[Pair]` array uses: one block, every ELEMENT read back.
     *     This is the leg the ATTRIBUTE moves -- see the stripped control. */
    struct S_Pair_ *v = (struct S_Pair_ *)arena_alloc(&a, 64 * sizeof *v);
    int off2 = 0;
    for (int i = 0; i < 64; i++) if ((uintptr_t)&v[i] % 8) off2++;
    printf("[2] Pair[64]: %d of 64 off an 8-byte boundary\n", off2);
    if (off2) { puts("FAIL [2]"); return 1; }
    /* [3] CONTROL, and the ground for the refusal: the arena does NOT deliver 16.
     *     If this ever comes back 0 the ceiling in parse_struct can be raised. */
    if (sweep(&a, sizeof(struct W16), 16, 4096, "[3] control 16") == 0)
        { puts("FAIL [3] the arena delivered 16-byte alignment on every one of "
               "4096 allocations -- the >8 refusal is no longer grounded"); return 1; }
    /* [4] CONTROL that leg [2] can fail at all. Leg [1] cannot: arena_alloc
     *     rounds every REQUEST to 8, so a single value is 8-aligned whatever
     *     its type -- which is exactly why N<=8 costs the allocator nothing.
     *     What the attribute buys is a SIZE that is a multiple of N, so element
     *     k of an array stays aligned; a 3-byte stride must therefore go off. */
    unsigned char *raw = (unsigned char *)arena_alloc(&a, 64 * 3);
    int off4 = 0;
    for (int i = 0; i < 64; i++) if ((uintptr_t)(raw + 3 * i) % 8) off4++;
    printf("[4] control stride 3: %d of 64 off an 8-byte boundary\n", off4);
    if (off4 == 0) { puts("FAIL [4] a 3-byte stride stayed 8-aligned -- leg [2] proves nothing"); return 1; }
    puts("all green (4 legs)");
    return 0;
}
C

for TYCHOC in ./tychoc ./tychoc1; do
    [ -x "$TYCHOC" ] || { echo "align-probe: no $TYCHOC -- run 'make' first"; exit 2; }
    "$TYCHOC" "$T/p.ty" --emit-c -o "$T/p" >"$T/emit.log" 2>&1 \
        || { echo "align-probe: FAIL -- $TYCHOC could not emit C"; sed 's/^/      /' "$T/emit.log"; exit 1; }
    if ! grep -q '__attribute__((aligned(8)))' "$T/p.c"; then
        bad "$TYCHOC emitted no aligned(8) -- the sweep below would measure nothing"
        continue
    fi
    cp "$T/p.c" "$T/p_orig.c"
    "$CC" -O1 -fwrapv -o "$T/probe" "$T/probe.c" -I"$T" -lm -lpthread >"$T/cc.log" 2>&1 \
        || { echo "align-probe: FAIL -- cc rejected the harness ($TYCHOC)"; sed 's/^/      /' "$T/cc.log"; exit 1; }
    "$T/probe" > "$T/probe.out" 2>&1
    prc=$?
    echo "  $TYCHOC:"
    sed 's/^/      /' "$T/probe.out"
    [ "$prc" -eq 0 ] || bad "$TYCHOC: the address sweep did not pass (exit $prc)"

    # [5] NEGATIVE CONTROL on the COMPILER, not on the arena: strip the attribute
    #     it emitted and leg [2] must go RED. Without this the legs above would
    #     pass on a struct that was never over-aligned in the first place.
    sed 's/} __attribute__((aligned(8)));/};/' "$T/p_orig.c" > "$T/p.c"
    if cmp -s "$T/p_orig.c" "$T/p.c"; then
        bad "$TYCHOC: [5] the substitution did not apply -- no aligned(8) line to strip"
        continue
    fi
    if ! "$CC" -O1 -fwrapv -o "$T/probe5" "$T/probe.c" -I"$T" -lm -lpthread >"$T/cc5.log" 2>&1; then
        bad "$TYCHOC: [5] cc rejected the stripped harness"
        continue
    fi
    "$T/probe5" > "$T/probe5.out" 2>&1
    p5=$?
    if [ "$p5" -eq 0 ]; then
        bad "$TYCHOC: [5] the sweep stayed GREEN with aligned(8) removed -- it is measuring the arena, not the attribute"
    else
        echo "      [5] control: attribute stripped -> exit $p5"
        grep -E '^(sizeof|\[2\]|FAIL)' "$T/probe5.out" | sed 's/^/          /'
    fi
done

[ "$rc" = 0 ] && echo "align-probe: ok (both compilers, 4 legs + the stripped control)"
exit "$rc"
