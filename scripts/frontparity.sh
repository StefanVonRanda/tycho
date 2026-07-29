#!/bin/sh
# frontparity — RETIRED 2026-07-29. This lane no longer runs.
#
# WHAT IT PROVED, WHILE IT RAN
# ----------------------------
# One direction of frontend agreement between the two compilers: every program
# `tychoc` (the C reference compiler) ACCEPTS, the frozen self-hosted
# `compiler/tychoc0.ty` also accepts. Both were invoked frontend-only
# (`--emit-c`), so a `cc` failure could never be mistaken for a refusal, and the
# failure line named tychoc0's actual diagnostic rather than a generic
# "differs". It swept examples/*.ty, tests/*.ty, tests/conc/*.ty,
# tests/warn/*.ty, tests/abort/*.ty, tests/diag/*.ty, tools/*.ty,
# compiler/tychoc0.ty, tests/pkg/*/main.ty, and the four per-example entry
# points examples/{fetch,sqlite,weblog,webserver}. It cost ~3s plus a tychoc0
# build.
#
# IT CAUGHT A REAL DEFECT. That is the reason this note is long rather than a
# one-line tombstone. An over-tightening of the newtype path made tychoc0 refuse
# `if dup == ids:` — `tests/newtype_agg.ty:33` exercised the shape and the
# differential reddened. The C compiler was perfectly happy with the program;
# only a second, independent implementation disagreeing revealed that the
# frontend had been narrowed by accident. `tests/warn/*.ty` and `tools/*.ty`
# were front-checked against tychoc0 by NO other gate.
#
# WHY IT WAS RETIRED
# ------------------
# On 2026-07-29 the language took a BREAKING change: the three-clause `for` and
# bare `for:` replace `for i in range(...)`, and the `range` builtin is deleted
# (see plan.md, and docs/spec/10-statements.md for the surviving loop forms).
# `compiler/tychoc0.ty` is FROZEN — it proved self-hosting and is unmaintained —
# so it cannot parse the new loop syntax at all. A frozen compiler that must
# still accept the whole corpus and a corpus that must adopt new syntax are not
# co-satisfiable. The corpus won; this lane was the price.
#
# WHAT IS LOST, PLAINLY
# ---------------------
# Continuous proof that tychoc0 accepts what tychoc accepts. The class of defect
# that will now go UNCAUGHT is exactly the one above: a silent over-tightening of
# the frontend that narrows what the language accepts, where the C compiler alone
# cannot notice because it is the thing that was narrowed. Nothing in `make ci`
# replaces this. It was never in `make ci` either (verified: no Makefile target
# and no step of scripts/ci.sh ever invoked it) — it was hand-run — so retiring
# it does not change what `make ci` covers. It changes what a careful human
# could check by hand, and that is a genuine reduction.
#
# WHAT compiler/tychoc0.ty STILL IS
# ---------------------------------
# It stays on disk, unchanged, and it is not dead weight. It is the artifact that
# proved Tycho self-hosts (ROADMAP.md, docs/architecture.md), it remains the
# largest single Tycho source in the tree (~16k lines), and `scripts/asan_self.sh`
# still feeds it to `tychoc` as INPUT under ASan+UBSan. What ended on 2026-07-29
# is the claim that it is CONTINUOUSLY CHECKED — not the artifact, and not the
# result it established.
echo "frontparity: RETIRED 2026-07-29 — this lane no longer runs."
echo "             It proved tychoc0's frontend accepted every program tychoc accepts."
echo "             Retired because the breaking loop-syntax change makes the frozen"
echo "             compiler unable to parse the corpus. See the header of this file,"
echo "             ROADMAP.md and docs/architecture.md."
exit 0
