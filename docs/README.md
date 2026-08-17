# Tycho documentation

## Start here

1. **[The README](../README.md)** — what Tycho is, and whether it's for you.
2. **[Tutorial](tutorial.md)** — write and run your first programs.
3. **[From `malloc` to implicit arenas](from-c-to-arenas.md)** — the memory model
   in five steps from familiar C. The gentlest route to the core idea.
4. **[Language reference](reference/index.md)** — how each feature behaves.

## Where to look for what

| You want | Go to |
|---|---|
| What a feature does, precisely | **[`reference/`](reference/)** — one terse page per topic |
| Why a feature works that way | **[`guides/`](guides/)** — the design reasoning |
| The exact rule for an edge case | **[`spec/`](spec/)** — grammar and normative semantics |
| A program is misbehaving | **[`guides/debugging.md`](guides/debugging.md)** |
| The numbers behind the claims | **[`performance.md`](performance.md)** |
| The idea the language argues for | **[`thesis.md`](thesis.md)** |

**[`reference/`](reference/)** covers basics, types, functions, arrays and slices,
structs and tuples, maps, enums and options, generics, subscripts, packages,
concurrency, FFI, and builtins.

**[`guides/`](guides/)** covers the memory model, arrays and structs, map values
and mutation, generics, packages, concurrency, FFI, the corelib, performance, and
debugging.

## Also here

- **[`architecture.md`](architecture.md)** — how the project is built and what
  each verification gate proves.
- **[`internals/`](internals/README.md)** and **[`rfc/`](rfc/README.md)** — design
  notes, proposals, and the running record of what fought back. Written for
  maintainers.

## Contributing

See **[CONTRIBUTING.md](../CONTRIBUTING.md)** for the build and which gate to run.
