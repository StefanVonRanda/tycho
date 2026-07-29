#!/bin/sh
# Bootstrap test harness for tychoc0 (Stage 1 of docs/bootstrap.md)
# — RETIRED 2026-07-29. This lane no longer runs and no longer builds tychoc0.
#
# WHAT IT PROVED, WHILE IT RAN
# ----------------------------
# The FIRST rung of the bootstrap ladder: that the subset compiler written in
# Tycho agreed with the C reference compiler on the small fixture set under
# compiler/tests/. For each fixture P it required cc(tychoc0(P)) and tychoc's own
# binary to print identically — a differential, not a golden, so a shared wrong
# answer was the only way past it. tychoc0 read source on stdin (or a path arg)
# and wrote C on stdout, which is the interface every later tychoc0 lane used.
#
# WHY IT WAS RETIRED
# ------------------
# On 2026-07-29 the language took a BREAKING change (three-clause `for` and bare
# `for:` replace `for i in range(...)`; the `range` builtin is deleted).
# compiler/tychoc0.ty is FROZEN and unmaintained, so it cannot parse the new
# syntax. See the header of compiler/fixpoint.sh for the full reasoning and
# ROADMAP.md / docs/architecture.md for what the project gave up.
#
# WHAT IS LOST
# ------------
# The cheapest tychoc0 differential — the one that reddened first and pointed at
# a named fixture. It was never in `make ci` (no Makefile target, no step of
# scripts/ci.sh invoked it); it was hand-run. compiler/tests/*.ty stays on disk;
# those fixtures are plain Tycho programs and nothing about them was tychoc0-
# specific.
echo "bootstrap: RETIRED 2026-07-29 — this lane no longer runs."
echo "           It proved tychoc0 matched the C compiler on compiler/tests/*.ty."
echo "           See the header of this file, ROADMAP.md and docs/architecture.md."
exit 0
