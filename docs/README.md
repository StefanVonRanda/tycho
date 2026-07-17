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
  language exists to test — is the best place to start.
- **[`spec/`](spec/)** — the formal specification: grammar, per-construct semantics, and
  a conformance suite. For implementers and edge-case reasoning.
- **[`architecture.md`](architecture.md)** — how the project is built, what each
  verification gate proves, what's shipped, and the decided non-goals.
- **[`internals/`](internals/)** and **[`rfc/`](rfc/)** — historical design records and
  resolved proposals. Not user documentation; kept for contributors and provenance.

## Contributing

See **[CONTRIBUTING.md](../CONTRIBUTING.md)** for the build, the local `make ci` gate, and
the parity rule: every language feature must work in *both* compilers, or the fixpoint
goes red.
