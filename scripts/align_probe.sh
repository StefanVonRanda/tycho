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

# Leg [6]'s subject: the CEILING, over every user type a program can put on the
# arena -- a vector nested in a struct, in an enum payload, in an array, as a map
# value and captured by a closure. `align(N)` is refused above 8 at the parser
# (tests/reject/align_too_big.ty), which covers the one construct that STATES an
# alignment; this covers the one that IMPLIES one.
mkdir -p "$T/q"
cat > "$T/q/q.ty" <<'TY'
align(8) struct Pair:
    a: u8
    b: u8

struct Holder:
    v: vector[4]float
    p: Pair
    tag: int

enum Box:
    Full(Holder)
    Empty

fn main():
    h := Holder([1.0, 2.0, 3.0, 4.0], Pair(to_u8(1), to_u8(2)), 7)
    hs := [h, h]
    m := []int: Holder
    m[1] = h
    b := Full(h)
    f := fn() -> int: len(hs) + h.tag
    match b:
        Full(x): println(str(x.tag) + str(len(m)) + str(f()))
        Empty: println("no")
TY

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

    # [6] THE CEILING, and the answer to "must every arena_alloc site be audited?"
    #     No: alignment cannot exceed 8 because it cannot ENTER the type system
    #     above 8. Exactly two constructs emit an alignment attribute in either
    #     compiler -- `align(N) struct`, refused above 8 by the parser
    #     (src/tychoc.c@parse_struct, compiler/parse/parse.ty@structd), and
    #     `vector[N]T`, pinned at `packed, aligned(8)` (src/tychoc.c:13178,
    #     compiler/emit/emit.ty:6695). So the invariant is checked where it is
    #     established, over every user type the emitted TU defines, rather than
    #     at the arena_alloc sites -- which the two emitters have 61 of between
    #     them, all of them templates instantiated per monomorphisation.
    "$TYCHOC" "$T/q/q.ty" --emit-c -o "$T/q/q" >"$T/q6.log" 2>&1 \
        || { bad "$TYCHOC: [6] could not emit the nesting probe"; sed 's/^/      /' "$T/q6.log"; continue; }
    # Both spellings: the two emitters name these types differently (TychoArrC0
    # against TychoArrK0, a typedef here and a bare tag there), so the sweep reads
    # typedef names AND struct tags rather than one compiler's convention.
    { sed -n 's/^typedef struct .*[ *]\([A-Za-z0-9_]*\);$/\1/p' "$T/q/q.c"
      sed -n 's/^struct \([A-Za-z0-9_]*\) {.*/struct \1/p' "$T/q/q.c"
    } | grep -E '^(struct )?(S_|E_|Env_|TychoArr|TychoMap|TychoVec)' | sort -u > "$T/q/names"
    n6="$(wc -l < "$T/q/names" | tr -d ' ')"
    if [ "$n6" -lt 6 ]; then
        bad "$TYCHOC: [6] found only $n6 user types in the emitted C -- the assertions below would be vacuous"
        continue
    fi
    { echo '#define main tycho_program_main'; echo '#include "q.c"'; echo '#undef main'
      while read -r n; do printf '_Static_assert(_Alignof(%s) <= 8, "%s over-aligns the arena");\n' "$n" "$n"; done < "$T/q/names"
    } > "$T/q/assert.c"
    if "$CC" -O0 -fwrapv -c -o /dev/null "$T/q/assert.c" -I"$T/q" >"$T/q/cc6.log" 2>&1; then
        echo "      [6] all $n6 user types in the emitted TU are <= 8-aligned ($(tr '\n' ' ' < "$T/q/names"))"
    else
        bad "$TYCHOC: [6] a user type over-aligns the arena"
        grep -m4 'static assertion failed' "$T/q/cc6.log" | sed 's/^/          /'
    fi

    # [6b] CONTROL: remove the cap `vector[N]T` carries and the alignment must
    #      CLIMB -- through the struct, the enum payload, the array, the map and
    #      the closure env. Without this, [6] passes on a probe whose types were
    #      never capable of over-aligning, and it is what shows the propagation
    #      is transitive rather than per-declaration.
    sed 's/__attribute__((packed, aligned(8)))/ /' "$T/q/q.c" > "$T/q/qc.c"
    if cmp -s "$T/q/q.c" "$T/q/qc.c"; then
        bad "$TYCHOC: [6b] the substitution did not apply -- no vector cap to remove"
        continue
    fi
    sed 's|#include "q.c"|#include "qc.c"|' "$T/q/assert.c" > "$T/q/assertc.c"
    if "$CC" -O0 -fwrapv -c -o /dev/null "$T/q/assertc.c" -I"$T/q" >"$T/q/cc6b.log" 2>&1; then
        bad "$TYCHOC: [6b] the assertions stayed GREEN with the vector cap removed -- leg [6] proves nothing"
    else
        echo "      [6b] control: vector cap removed -> $(grep -c 'static assertion failed' "$T/q/cc6b.log") of $n6 types over-align"
    fi
done

[ "$rc" = 0 ] && echo "align-probe: ok (both compilers, 6 legs + the stripped and uncapped controls)"
exit "$rc"
