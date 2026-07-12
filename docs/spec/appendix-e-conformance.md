# Appendix E — Conformance test map

This appendix maps every normative clause of the specification to at least one
test that exercises it, so that "conforming" is a checkable claim rather than a
prose assertion. It is a **living artifact**, populated during Phase 5 of the
build plan (`docs/internals/spec-plan.md`).

## E.1 The conformance oracle

Conformance is defined against the **two-implementation oracle**
([§1.3](00-conventions.md#13-conformance)): across the conformance suite an
implementation MUST accept exactly the programs `tychoc` accepts, reject exactly
those it rejects, and produce byte-identical output. The suite is drawn from:

- the existing golden fixtures under `tests/` (behavioral) and `tests/reject/`
  (must-fail);
- the corelib fixtures under `corelib/test/`;
- the differential-fuzz and accept/reject parity corpora (`fuzz/`, the
  `typeparity`/`eqparity`/`unaryparity`/`parforparity` lanes);
- new probe fixtures written to pin previously-untested corners (the resolved
  items in `spec-plan.md §6a` each become a fixture).

## E.2 The coverage matrix (to be populated)

Each row will bind a normative clause (section + requirement) to one or more
fixtures. A clause with no fixture is flagged, exactly as an untested branch is.

| Clause | Requirement (abbrev.) | Fixture(s) |
|---|---|---|
| §3.4 | indentation / mixed-tabs-spaces rejected | `tests/tab_indent`, a reject fixture |
| §5.5 | structural `==`; function identity | `tests/*_eq`, `eqparity` |
| §7.2 | `where` predicate rejection | `tests/reject/where_*` |
| §11.2 | `inout` exclusivity rejected | *new fixture — also covers the tychoc0 gap (spec-plan §6a)* |
| §13.4 | `match` subject evaluated once | *new probe fixture* |
| §14.4 | `range` step 0 → empty loop | *new probe fixture* |
| §30.2 | the abort set (div0, bounds, …) | `tests/reject/*`, runtime probes |
| … | *(remaining clauses populated in Phase 5)* | … |

## E.3 The `make spec-check` gate

A gate (to be added) MUST assert that every fenced Tycho example in `docs/spec/`
compiles and runs on **both** compilers with the shown output — the same
discipline the reference pages already satisfy. A failing example is a
specification defect (the rule or the example is wrong) and blocks release.
