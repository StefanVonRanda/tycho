# Tycho language reference

This reference catalogs every feature of the language and how it behaves. Each
feature exists to stress-test the arena model in a different dimension — the
[thesis](../thesis.md) explains the argument; this reference documents the
evidence.

Start with the thesis for *why*, then come here for *what*. Every example on
every page compiles on both transpilers and produces the shown output.

## Pages

| Page | Covers |
| --- | --- |
| [Basics](basics.md) | procedures, parameters (`inout`), declarations and assignment, expressions and operators, control flow |
| [Types](types.md) | the scalar types, `bytes`, string interpolation, distinct `type` newtypes, bidirectional type inference |
| [Arrays and slices](arrays-slices.md) | `[T]`, nested arrays, element places, slices `xs[a:b]` |
| [Structs and tuples](structs-tuples.md) | `struct`, nested and recursive fields, tuples and multiple return values |
| [Maps](maps.md) | `[K: V]` for scalar and composite keys, any value type, `m[k]` as a place |
| [Subscripts](subscripts.md) | user-defined projections: `subscript … -> inout U: yield &<place>`, a zero-copy view generalizing `&m[k]` |
| [Enums, options, and `match`](enums-options.md) | sum types, `Option(T)`, `Result(T, E)`, exhaustive `match`, `or_return` |
| [Functions and closures](functions.md) | first-class function values, closures, methods (UFCS) |
| [Generics](generics.md) | `$T` type parameters over functions, structs, and enums |
| [Concurrency](concurrency.md) | `spawn`/`wait`, `parallel for`, channels, `select` |
| [FFI](ffi.md) | calling C: `extern fn`, handles, bytes across the boundary |
| [Packages](packages.md) | multi-file packages, `import`, the corelib |
| [Builtins](builtins.md) | the built-in functions, by category |

## Conventions

- **One way to do each thing.** Tycho is kept small; where two spellings would do, only
  one exists. The reference says which.
- **Everything is a value.** The one idea behind the whole language is that data has
  *value semantics*: assignment, arguments, and returns copy, and the copy is deep, so
  two variables never share storage. Each page says how this plays out for its types
  instead of restating the principle; [the thesis](../thesis.md) explains the claim
  and the evidence (no GC, no `free`, no data races on owned values).
- **Examples are real.** Every code sample compiles and runs under both the C reference
  transpiler and the self-hosted transpiler — the language has two implementations that
  must agree (see [The evidence](../../README.md#the-evidence)). Output shown in a
  comment is the actual output.
