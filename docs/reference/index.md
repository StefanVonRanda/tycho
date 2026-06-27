# Tycho language reference

This is the authoritative description of the Tycho language — what each construct
means and how it behaves. It is organized by topic; read it in order for a complete
tour, or jump to a page for a specific feature.

For *why* the language is shaped this way — the value-semantics and implicit-arena
argument that motivates almost every decision here — see [the thesis](../thesis.md).
For a gentler, project-driven first pass, start with the
[learning guide](../learning-guide.md). This reference assumes you can already read
a small program.

## Pages

| Page | Covers |
| --- | --- |
| [Basics](basics.md) | procedures, parameters (`mut`), declarations and assignment, expressions and operators, control flow |
| [Types](types.md) | the scalar types, `bytes`, string interpolation, distinct `type` newtypes, bidirectional type inference |
| [Arrays and slices](arrays-slices.md) | `[T]`, nested arrays, element places, slices `xs[a:b]` |
| [Structs and tuples](structs-tuples.md) | `struct`, nested and recursive fields, tuples and multiple return values |
| [Maps](maps.md) | `[K: V]` for scalar and composite keys, any value type, `m[k]` as a place |
| [Enums, options, and `match`](enums-options.md) | sum types, `Option(T)`, `Result(T, E)`, exhaustive `match`, `or_return` |
| [Functions and closures](functions.md) | first-class function values, closures, methods (UFCS) |
| [Generics](generics.md) | `$T` type parameters over functions, structs, and enums |
| [Concurrency](concurrency.md) | `spawn`/`wait`, `parallel for`, channels, `select` |
| [FFI](ffi.md) | calling C: `extern fn`, handles, bytes across the boundary |
| [Packages](packages.md) | multi-file packages, `import`, the corelib |
| [Builtins](builtins.md) | the built-in functions, by category |

## Conventions

- **One way to do each thing.** Tycho is deliberately small; where two spellings would
  do, only one exists. The reference says which.
- **Everything is a value.** The single idea behind the whole language is that data has
  *value semantics*: assignment, arguments, and returns copy, and the copy is deep, so
  two variables never share storage. Each page states how this plays out for its types
  rather than restating the principle; [the thesis](../thesis.md) explains why it holds
  and what it buys (no GC, no `free`, no data races on owned values).
- **Examples are real.** Every code sample compiles and runs under both the C reference
  compiler and the self-hosted compiler — the language has two implementations that must
  agree (see [Self-hosting](../../README.md#self-hosting)). Output shown in a comment is
  the actual output.
