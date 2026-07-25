#!/bin/sh
# frontparity — gate the POSITIVE lane on tychoc0's frontend verdict, not just tychoc's.
#
# WHAT WAS MISSING, AND WHY THIS EXISTS
# -------------------------------------
# `tests/run.sh:113` compiles each `examples/*.ty` + `tests/*.ty` with `$TYCHOC`
# alone; tychoc0 appears on the positive lane of that script nowhere. It is built
# at `tests/run.sh:148` and used only on the *reject* lane (`:159`, `:178`), the
# *abort* lane (`:199`) and the *diag* goldens (`:262`) — all three of which score
# tychoc0 REFUSING something. Nothing there scores tychoc0 ACCEPTING what tychoc
# accepted. So a program tychoc accepts and tychoc0 refuses can score `all green`.
#
# WHAT ALREADY COVERS IT, HONESTLY
# --------------------------------
# `compiler/fixpoint.sh:24-30` does cover the property over the bulk of the
# corpus, and this lane does NOT pretend otherwise. That loop walks
# `tests/*.ty examples/*.ty`, skips whatever tychoc cannot build (`:26 || continue`),
# and then requires B — a tychoc0-derived binary — to emit C, cc it, and match
# tychoc's golden output (`:28-29`). A tychoc0 refusal makes `"$T/B" < "$f"` exit
# nonzero, so it does redden. That is exactly why it caught plan.md Phase 33's
# over-tightening: `tests/newtype_agg.ty:33` (`if dup == ids:`) exercised the shape,
# and fixpoint printed `FAIL newtype_agg.ty (B differs from the C compiler)`.
# `tests/pkg/*/` is covered the same way (`:41-52`), `tests/conc/*.ty` by
# `tests/conc/run.sh:63-67`, `tests/abort/*.ty` by `tests/run.sh:199`, and
# `corelib/` by `make corelib`.
#
# So Phase 40's eleven over-rejections were NOT missed for want of a gate — they
# were missed for want of a FIXTURE. Nine were `dup := a` (an inferred local of an
# inferred newtype local) over nine underlying types, a shape no committed program
# contained. The two that did correspond to a fixture had been deliberately
# written AROUND: `tests/newtype_over_aggregate.ty` carried `["one": FA([1,2])]`
# and a throwaway `a3 := mk_fa()` with in-file comments saying tychoc0 rejects the
# natural spelling. A second full positive lane over the same glob would have
# missed all eleven for the same reason, which is why this file is not one.
#
# WHAT THIS LANE ADDS THAT NONE OF THE ABOVE DOES
# -----------------------------------------------
# 1. It names the verdict. fixpoint discards tychoc0's stderr (`:28 2>/dev/null`)
#    and reports every cause as one string, "B differs from the C compiler" — a
#    frontend refusal, a cc failure of the emitted C, and a genuine output
#    divergence are indistinguishable in it. Phase 40 had to re-run tychoc0 by hand
#    to learn which. Here the failure line is the refusal and its diagnostic.
# 2. It widens the glob. `tests/warn/*.ty` (6 programs — `tests/run.sh:291-314`
#    runs `$TYCHOC` only) and `tools/*.ty` (3 — `tycho.ty`, `tychofmt.ty`,
#    `lsp.ty`, built by `Makefile:38,:44,:49` with tychoc alone, and among the
#    largest real Tycho programs in the tree) are front-checked against tychoc0 by
#    no other gate. Verified by grep over every `*.sh` and the Makefile.
# 3. It is a fast tripwire. No `cc`, no run, no 3-stage bootstrap: ~3s total
#    against fixpoint's minutes, so it reddens early and cheaply in `scripts/ci.sh`.
#
# THE VERDICT, AND WHY `--emit-c` ON BOTH SIDES
# ---------------------------------------------
# Both compilers are invoked frontend-only. `./tychoc F --emit-c -o X` stops after
# emitting X.c (`tests/run.sh:70` cc's it as a separate step), and `tychoc0 F
# --emit-c` writes C to stdout. Comparing a bare `./tychoc F -o X` against tychoc0
# would conflate tychoc's `cc` step with tychoc0's frontend exit — that is not
# scored here, and must not be.
#
# The subject is ONE direction: tychoc accepts, tychoc0 refuses. If tychoc refuses,
# the program is skipped — tychoc's own verdict is owned by `tests/reject/` and
# `tests/diag/`, and the opposite direction (tychoc0 fail-OPEN on an invalid
# program) is owned by `tests/run.sh:159`/`:178`. All 15 `tests/diag/*.ty` skip
# today by construction; they are in the glob so that the day one of them starts
# being accepted, it is covered without a script edit.
#
# COVERAGE — what is in, and what is NOT
# -------------------------------------
# IN:  examples/*.ty, tests/*.ty, tests/conc/*.ty, tests/warn/*.ty,
#      tests/abort/*.ty, tests/diag/*.ty, tools/*.ty, compiler/tychoc0.ty,
#      tests/pkg/*/main.ty (standalone driver, the `tychoc0 <entry>` form
#      `compiler/fixpoint.sh:48` uses).
# NOT: tests/reject/** — the other direction, already gated (see above).
#      corelib/ and examples/corelib/ — `make corelib` runs both compilers over
#      them with per-module dependency skips this lane does not replicate, the
#      same boundary `scripts/asan_self.sh:69-70` draws.
#      Output equality — that is fixpoint's job and this lane never runs a program.
#
# H0 (env, optional): use an already-built tychoc0 instead of building one. For
# reproducing a divergence against a patched compiler; unset in every gate run.
#
# Exit status: 0 iff no program tychoc accepts is refused by tychoc0.
set -u
cd "$(dirname "$0")/.." || exit 2

TYCHOC="${TYCHOC:-./tychoc}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

[ -x "$TYCHOC" ] || { echo "frontparity: run 'make' first"; exit 2; }

if [ -n "${H0:-}" ]; then
    [ -x "$H0" ] || { echo "frontparity: FATAL — H0='$H0' is not executable"; exit 2; }
    echo "frontparity: using H0=$H0 (not rebuilt)"
else
    H0="$TMP/tychoc0"
    if ! "$TYCHOC" compiler/tychoc0.ty -o "$H0" >"$TMP/build.log" 2>&1; then
        echo "frontparity: FATAL — could not build tychoc0"
        sed 's/^/      /' "$TMP/build.log"
        exit 2
    fi
fi

pass=0
fail=0
skip=0
fails=""

# check_one <entry.ty> <label>
check_one() {
    hi="$1"; name="$2"
    if ! "$TYCHOC" "$hi" --emit-c -o "$TMP/ref" >"$TMP/ref.log" 2>&1; then
        # tychoc refused it. Not this lane's subject — see the header.
        skip=$((skip + 1))
        return
    fi
    if "$H0" "$hi" --emit-c >/dev/null 2>"$TMP/h0.log"; then
        pass=$((pass + 1))
    else
        echo "FAIL  $name  (tychoc ACCEPTED it, tychoc0 REFUSED it)"
        sed 's/^/      /' "$TMP/h0.log" | head -6
        fail=$((fail + 1)); fails="$fails $name"
    fi
    rm -f "$TMP/ref.c"
}

for hi in examples/*.ty tests/*.ty tests/conc/*.ty tests/warn/*.ty \
          tests/abort/*.ty tests/diag/*.ty tools/*.ty compiler/tychoc0.ty; do
    [ -e "$hi" ] || continue
    check_one "$hi" "$hi"
done
for d in tests/pkg/*/; do
    [ -d "$d" ] || continue
    [ -f "${d}main.ty" ] || continue
    check_one "${d}main.ty" "${d}main.ty"
done

echo "-----------------------------------------"
echo "frontparity: agreed: $pass   diverged: $fail   (skipped, tychoc refused: $skip)"
[ "$fail" -eq 0 ] || { echo "failed:$fails"; exit 1; }
echo "frontparity: all green (tychoc0's frontend accepts every program tychoc accepts)"
