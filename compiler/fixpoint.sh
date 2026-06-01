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
$CC -O2 -o "$T/B" "$T/cA.c"         2>/dev/null     || { echo "FAIL: emitted C (cA) did not compile"; exit 1; }
"$T/B" < "$H" > "$T/cB.c"           2>/dev/null     || { echo "FAIL: B could not compile hierc0.hi"; exit 1; }
$CC -O2 -o "$T/C" "$T/cB.c"         2>/dev/null     || { echo "FAIL: emitted C (cB) did not compile"; exit 1; }
if ! cmp -s "$T/cA.c" "$T/cB.c"; then echo "FAIL: B != C (self-emission not a fixed point)"; rm -rf "$T"; exit 1; fi
echo "ok   B == C : hierc0 reproduces itself byte-identically ($(wc -l <"$T/cA.c") lines C)"
fail=0
for f in tests/*.hi examples/*.hi; do
    nm=$(basename "$f")
    # soa.hi exercises SOA arrays — a C-compiler feature not yet in hierc0's
    # self-hosting subset, so B (hierc0) cannot compile it. (make test still
    # runs it under ASan/UBSan.) The first test outside hierc0's coverage.
    case "$nm" in soa.hi) continue;; esac
    ./hierc "$f" -o "$T/ref" >/dev/null 2>&1 || continue
    ref="$("$T/ref" </dev/null 2>/dev/null)"
    "$T/B" < "$f" > "$T/g.c" 2>/dev/null && $CC -O2 -o "$T/g" "$T/g.c" 2>/dev/null \
        && [ "$ref" = "$("$T/g" </dev/null 2>/dev/null)" ] || { echo "FAIL $nm (B differs from the C compiler)"; fail=1; }
done
rm -rf "$T"
[ "$fail" -eq 0 ] && echo "fixpoint: all green (self-hosting; B==C and B matches the C compiler)" || { echo "fixpoint: FAIL"; exit 1; }
