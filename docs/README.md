# Tycho documentation

## Learn

1. **[The README](../README.md)** — what Tycho is, and whether it's for you.
2. **[Tutorial](tutorial.md)** — write and run your first programs in about an hour.
3. **[From `malloc` to implicit arenas](from-c-to-arenas.md)** — the memory model
   in five steps from familiar C. Read this right after the tutorial.

## Use

4. **[Language reference](reference/index.md)** — how each feature behaves, one
   page per topic, every example runnable.
5. **[Quiet results](quiet-results.md)** — the seven places the language answers
   instead of refusing. Short; read it before the first real program.

| You want | Go to |
|---|---|
| What a feature does, precisely | **[`reference/`](reference/)** — one page per topic |
| The exact rule for an edge case | **[`spec/`](spec/)** — grammar and normative semantics |
| An operation answered when you expected it to refuse | **[`quiet-results.md`](quiet-results.md)** |
| A program is misbehaving | **[`debugging.md`](debugging.md)** |
| A build fails, or your platform is unusual | **[`platforms.md`](platforms.md)** — gated platforms, skips, C toolchain matrix |

## Trust

The case that the model works, and what it costs:

- **[The thesis](thesis.md)** — the argument: why value semantics makes implicit
  arenas work, where it doesn't, with measured numbers.
- **[The memory model](memory-model.md)** — the practitioner's view: what the
  compiler emits and what to expect in practice.
- **[Performance](performance.md)** — the measurements behind the claims.
- **[Controls](controls.md)** — why the evidence is worth reading: four cases
  where the test gates proved they can fail.
- **[Architecture](architecture.md)** — how the project is built, what each
  verification gate proves, what's shipped, and the decided non-goals.

## Maintain

- **[`internals/`](internals/README.md)** and **[`rfc/`](rfc/README.md)** — design
  notes, proposals, and investigation records. Written for maintainers.

## Contributing

See **[CONTRIBUTING.md](../CONTRIBUTING.md)** for the build and which gate to run.
