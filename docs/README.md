# Tycho documentation

New here? Read these in order:

1. **[The README](../README.md)** — what Tycho is and why, in a few minutes.
2. **[Tutorial](tutorial.md)** — write and run your first programs, start to finish.
3. **[From `malloc` to implicit arenas](from-c-to-arenas.md)** — the memory model
   explained in five steps from familiar C. The gentlest introduction to the core idea.
4. **[Language reference](reference/index.md)** — the canonical, per-topic description of
   how each feature behaves. This is the source of truth.

## How the docs are organized

Different documents answer different questions. If two files cover the same topic, this
is why:

- **[`reference/`](reference/)** — *what a feature does.* The canonical, terse language
  reference, one page per topic (`basics`, `types`, `maps`, `generics`, `concurrency`,
  `ffi`, `packages`, …). If you want the precise behavior, look here.
- **[`guides/`](guides/)** — design notes: *why a feature is the way it is.* Long-form
  essays on the reasoning behind each subsystem (`memory-model`, `generics`,
  `concurrency`, `ffi`, `packages`, `arrays-structs`, `map-values`, `map-mutation`,
  `perf`, `corelib`, `debugging`). And **[thesis.md](thesis.md)** — the argument the whole
  language exists to test — is the best place to start;
  **[guides/debugging.md](guides/debugging.md)** is the one to read when a program
  misbehaves (gdb/lldb over the generated C, `--emit-c -o`, the sanitizer builds).
- **[`spec/`](spec/)** — the formal specification: grammar, per-construct semantics, and
  a conformance suite. For implementers and edge-case reasoning.
- **[`architecture.md`](architecture.md)** — how the project is built, what each
  verification gate proves, what's shipped, and the decided non-goals.
- **[`performance.md`](performance.md)** — the measurements behind the README's
  claims: the cross-language memory tables, the flat-memory JSON parser, and the
  honest costs.
- **[`bootstrap.md`](bootstrap.md)** — how Tycho self-hosted: the bootstrap stages,
  which script ran each one, and why the frozen `compiler/tychoc0.ty` is no longer
  built by any gate. History, not a build instruction.
- **[`internals/`](internals/)** — three design notes:
  [`value-semantics-limits.md`](internals/value-semantics-limits.md) on where the
  memory model costs, [`design-aggregate-ref.md`](internals/design-aggregate-ref.md)
  and [`design-scalar-match.md`](internals/design-scalar-match.md). Also
  [`FRICTION.md`](internals/FRICTION.md), the running record of what fought back
  while writing real programs against this language, and nine `plan-*-DONE.md`
  archives — this line claimed until 2026-08-13 that those were pruned on
  2026-08-03 and lived on only under the `docs-archive` tag, which `git ls-files`
  disagrees with. **[`rfc/`](rfc/)** — proposals, resolved and open:
  [`value-lifetime-regions.md`](rfc/value-lifetime-regions.md) and
  [`parallel-for-width.md`](rfc/parallel-for-width.md) (BUILT 2026-08-13 — the
  `parallel(W) for` width slot, with what shipped recorded at its end). Not user documentation; kept for contributors
  and provenance.

## Contributing

See **[CONTRIBUTING.md](../CONTRIBUTING.md)** for the build and the local `make ci` gate.
Until 2026-07-26 this paragraph also named a parity rule — every feature had to work in
*both* compilers or `make fixpoint` went red. `compiler/tychoc0.ty` is frozen and those
lanes were retired on 2026-07-29; [`bootstrap.md`](bootstrap.md) records what they proved
and what their loss costs.
