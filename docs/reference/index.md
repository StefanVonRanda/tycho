# Tycho language reference

How each part of Tycho behaves, with runnable examples. New here?
Start with the [tutorial](../tutorial.md). Curious why the language
works this way? Read [From `malloc` to arenas](../from-c-to-arenas.md),
then the [thesis](../thesis.md) for the full argument.

Every example compiles and prints the output shown.

## Pages

| Page | Covers |
| --- | --- |
| [Basics](basics.md) | procedures, parameters (`inout`), declarations and assignment, expressions and operators, control flow |
| [Types](types.md) | the scalar types, `bytes`, string interpolation, distinct `type` newtypes, bidirectional type inference |
| [Strings](strings.md) | a `string` is BYTES: `len`, `s[i]` as a byte value, byte slicing; `utf8.count` vs `len`, stepping by codepoint with the mandatory `nb <= 0` guard, validating before you trust, and where `core:strings` and `core:utf8` divide |
| [Arrays and slices](arrays-slices.md) | `[T]`, nested arrays, element places, slices `xs[a:b]`, fixed-capacity `bounded[N]T` |
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

- **One spelling per idea.** Where two spellings could do the same job,
  Tycho keeps one. Each page names it.
- **Values copy.** `b := a` gives `b` its own independent copy. Two
  variables never share mutable storage. Each page shows what that means
  for its own types.
- **Examples run.** Every sample compiles under `tychoc`. Output shown
  in a comment is the actual output.

Before the first real program, read [Quiet results](../quiet-results.md) —
the seven places the language answers instead of refusing.
