#!/bin/sh
# Stage 4 self-host fixpoint (docs/bootstrap.md). The 3-stage bootstrap:
#   A = hierc (the C compiler) building hierc0.hi              -> exe A
#   B = A building hierc0.hi (C emitted by A, then cc'd)       -> exe B
#   C = B building hierc0.hi (C emitted by B, then cc'd)       -> exe C
# Assert the emitted C of B and C is byte-identical (so B == C): A was built by
# the C compiler, B and C by a Hier-built compiler, so cB == cC proves the Hier
# compiler reproduces itself exactly. Also checks B differentially reproduces
# the C compiler's golden output across tests/ + examples/.
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
[ -x ./hierc ] || { echo "run 'make' first"; exit 2; }
H=compiler/hierc0.hi
T=$(mktemp -d)
./hierc "$H" -o "$T/A"            >/dev/null 2>&1 || { echo "FAIL: C compiler could not build hierc0"; exit 1; }
"$T/A" < "$H" > "$T/cA.c"           2>/dev/null     || { echo "FAIL: A could not compile hierc0.hi"; exit 1; }
$CC -O2 -o "$T/B" "$T/cA.c" -lm     2>/dev/null     || { echo "FAIL: emitted C (cA) did not compile"; exit 1; }
"$T/B" < "$H" > "$T/cB.c"           2>/dev/null     || { echo "FAIL: B could not compile hierc0.hi"; exit 1; }
$CC -O2 -o "$T/C" "$T/cB.c" -lm     2>/dev/null     || { echo "FAIL: emitted C (cB) did not compile"; exit 1; }
if ! cmp -s "$T/cA.c" "$T/cB.c"; then echo "FAIL: B != C (self-emission not a fixed point)"; rm -rf "$T"; exit 1; fi
echo "ok   B == C : hierc0 reproduces itself byte-identically ($(wc -l <"$T/cA.c") lines C)"
fail=0
for f in tests/*.hi examples/*.hi; do
    nm=$(basename "$f")
    ./hierc "$f" -o "$T/ref" >/dev/null 2>&1 || continue
    ref="$("$T/ref" </dev/null 2>/dev/null)"
    "$T/B" < "$f" > "$T/g.c" 2>/dev/null && $CC -O2 -o "$T/g" "$T/g.c" -lm 2>/dev/null \
        && [ "$ref" = "$("$T/g" </dev/null 2>/dev/null)" ] || { echo "FAIL $nm (B differs from the C compiler)"; fail=1; }
done
# Package programs (Stage D): the Hier-built compiler B builds each tests/pkg
# fixture from `hierc --bundle <entry>` (the post-order package stream, each
# `package` header switching B's mangling prefix) and must match the C compiler.
for d in tests/pkg/*/; do
    [ -d "$d" ] || continue
    nm=$(basename "$d")
    entry="$d/main.hi"
    [ -f "$entry" ] || continue
    ./hierc "$entry" -o "$T/pref" >/dev/null 2>&1 || { echo "FAIL pkg_$nm (C compiler could not build it)"; fail=1; continue; }
    ref="$("$T/pref" </dev/null 2>/dev/null)"
    if ./hierc "$entry" --bundle 2>/dev/null | "$T/B" > "$T/pg.c" 2>/dev/null && $CC -O2 -o "$T/pg" "$T/pg.c" -lm 2>/dev/null; then
        [ "$ref" = "$("$T/pg" </dev/null 2>/dev/null)" ] || { echo "FAIL pkg_$nm (B differs from the C compiler)"; fail=1; }
    else
        echo "FAIL pkg_$nm (hierc0 could not compile the bundle)"; fail=1
    fi
    # Standalone driver: B compiles the package directly from disk (read_file /
    # list_dir / args — no `hierc --bundle` middleman) and must match too.
    if "$T/B" "$entry" > "$T/sd.c" 2>/dev/null && $CC -O2 -o "$T/sd" "$T/sd.c" -lm 2>/dev/null; then
        [ "$ref" = "$("$T/sd" </dev/null 2>/dev/null)" ] || { echo "FAIL pkg_$nm (standalone hierc0 <path> differs)"; fail=1; }
    else
        echo "FAIL pkg_$nm (standalone hierc0 could not compile the package)"; fail=1
    fi
done
# Dogfood (Stage E): split hierc0.hi itself into a two-package program
# (compiler/pkg-split.sh -> `main` importing `rt`) and prove the SELF-HOSTED
# compiler handles itself packaged:
#   - the C compiler builds the split (Apkg);
#   - Apkg compiling its own --bundle is a fixed point (Epkg==Fpkg);
#   - the multi-package compiler emits byte-identical C to the single-file
#     compiler (B) on every fixture — repackaging changes no output.
sh compiler/pkg-split.sh "$T/split" 2>/dev/null || { echo "FAIL: could not split hierc0"; fail=1; }
if ./hierc "$T/split/main.hi" -o "$T/Apkg" >/dev/null 2>&1; then
    ./hierc "$T/split/main.hi" --bundle > "$T/sb.hi" 2>/dev/null
    if "$T/Apkg" < "$T/sb.hi" > "$T/eA.c" 2>/dev/null && $CC -O2 -o "$T/Epkg" "$T/eA.c" -lm 2>/dev/null \
       && "$T/Epkg" < "$T/sb.hi" > "$T/eB.c" 2>/dev/null; then
        cmp -s "$T/eA.c" "$T/eB.c" || { echo "FAIL split-hierc0 self-emission is not a fixed point"; fail=1; }
        sdiff=0
        for f in tests/*.hi examples/*.hi; do
            "$T/B"    < "$f" > "$T/u.c" 2>/dev/null
            "$T/Epkg" < "$f" > "$T/v.c" 2>/dev/null
            cmp -s "$T/u.c" "$T/v.c" || { echo "FAIL split-hierc0 differs from single-file on $(basename "$f")"; sdiff=1; }
        done
        [ "$sdiff" -eq 0 ] && [ "$fail" -eq 0 ] && echo "ok   split hierc0 (2 packages) self-hosts E==F and matches the single-file compiler"
    else
        echo "FAIL split-hierc0 could not self-build"; fail=1
    fi
else
    echo "FAIL: C compiler could not build the split hierc0"; fail=1
fi
rm -rf "$T"
[ "$fail" -eq 0 ] && echo "fixpoint: all green (self-hosting; B==C; single files + packages; hierc0 self-split dogfood)" || { echo "fixpoint: FAIL"; exit 1; }
