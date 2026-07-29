#!/bin/sh
# Stage 4 self-host fixpoint (docs/bootstrap.md) — RETIRED 2026-07-29.
# This lane no longer runs and no longer builds tychoc0.
#
# WHAT IT PROVED, WHILE IT RAN
# ----------------------------
# The 3-stage bootstrap, and that it reached a fixed point:
#   A = tychoc (the C compiler) building compiler/tychoc0.ty   -> exe A
#   B = A building tychoc0.ty (C emitted by A, then cc'd)      -> exe B
#   C = B building tychoc0.ty (C emitted by B, then cc'd)      -> exe C
# A was built by the C compiler; B and C by a Tycho-built compiler. Asserting
# the emitted C of B and C byte-identical (B == C) is the self-hosting result:
# the Tycho compiler reproduces itself exactly. On top of that it checked, over
# tests/*.ty + examples/*.ty, that B differentially reproduced the C compiler's
# golden output; that the package path agreed both via `tychoc --bundle` and via
# the standalone `tychoc0 <entry>` driver over tests/pkg/*/; and the Stage E
# dogfood, where compiler/pkg-split.sh splits tychoc0.ty into a two-package
# program that must self-host (E == F) and emit byte-identical C to the
# single-file compiler on every fixture.
#
# It also carried the OUTPUT half of the differential that scripts/frontparity.sh
# carried the FRONTEND half of, and it is where the newtype over-tightening of
# `tests/newtype_agg.ty` first surfaced (as the undifferentiated `FAIL
# newtype_agg.ty (B differs from the C compiler)`, which is why frontparity.sh
# was written).
#
# WHY IT WAS RETIRED
# ------------------
# On 2026-07-29 the language took a BREAKING change: the three-clause `for` and
# bare `for:` replace `for i in range(...)`, and the `range` builtin is deleted.
# `compiler/tychoc0.ty` is FROZEN and unmaintained, so it cannot parse the new
# loop syntax — and this lane requires a tychoc0-derived binary to compile the
# whole corpus. A frozen compiler and an evolving corpus are not co-satisfiable.
#
# WHAT IS LOST, PLAINLY
# ---------------------
# Continuous proof that the self-hosting fixed point still holds at HEAD, and
# continuous proof that a second independent implementation produces the same
# program output as the C compiler. It was never in `make ci` (verified: no
# Makefile target, no step of scripts/ci.sh invoked it) — it was hand-run — so
# retiring it changes nothing about what `make ci` covers.
#
# WHAT compiler/tychoc0.ty STILL IS
# ---------------------------------
# It stays on disk, unchanged. The self-hosting result it established in 2026 is
# a historical fact and remains true of that commit; ROADMAP.md records it as
# finished work and it still is. `scripts/asan_self.sh` still feeds it to
# `tychoc` as INPUT, the largest single Tycho source in the tree. What ended is
# the claim that the fixed point is RE-PROVEN on every run — not the result.
# compiler/pkg-split.sh, whose only caller was this script, is likewise retired
# in place rather than deleted.
echo "fixpoint: RETIRED 2026-07-29 — this lane no longer runs."
echo "          It proved the 3-stage bootstrap reached a fixed point (B == C) and"
echo "          that the self-hosted compiler matched the C compiler's output."
echo "          Retired because the breaking loop-syntax change makes the frozen"
echo "          compiler unable to parse the corpus. See the header of this file,"
echo "          ROADMAP.md and docs/architecture.md."
exit 0
