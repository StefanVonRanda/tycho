# Examples

Runnable Tycho programs, from one-liners to real applications. Single-file examples run
directly:

```
$ ./tychoc examples/hello.ty && ./examples/hello
```

Each subdirectory is a multi-file program with its own README or `run.sh`.

## Start here

| File | What it shows |
|---|---|
| [`hello.ty`](hello.ty) | Ask for a name and greet it — the smallest complete program. |
| [`arrays.ty`](arrays.ty) | `[int]` arrays: literals, `push`, `len`, indexing, and value-semantic copies. |
| [`structs.ty`](structs.ty) | Pure-value structs: construction, nested fields, copies. |
| [`strings.ty`](strings.ty) | String builtins: `len`, indexing, `substr`, `find`, ordering, concat. |
| [`generics_tour.ty`](generics_tour.ty) | A `$T` type parameter, monomorphized by the compiler. |

## Language, feature by feature

| File | What it shows |
|---|---|
| [`records.ty`](records.ty) | Structs with heap-bearing fields (`string`, `[string]`) and their value semantics. |
| [`words.ty`](words.ty) | `[string]` arrays with `split`/`join`. |
| [`wordcount.ty`](wordcount.ty) | Word-frequency count into a `[string: int]` map. |
| [`array_fns.ty`](array_fns.ty) | Arrays across function boundaries: pass down (borrow), return up. |
| [`inout.ty`](inout.ty) | `inout` parameters — the explicit borrow-for-mutation. |
| [`collect.ty`](collect.ty) / [`context.ty`](context.ty) / [`memo.ty`](memo.ty) | Mutable state threaded through `inout`. |
| [`accumulate.ty`](accumulate.ty) / [`accumulate_big.ty`](accumulate_big.ty) | In-place string building: the O(n²)→O(n) `acc = acc + x` optimization. |
| [`demo.ty`](demo.ty) | A grab-bag: arithmetic, control flow, and the core constructs in one file. |

## Real programs (single file)

| File | What it is |
|---|---|
| [`json.ty`](json.ty) | A JSON parser + serializer over a recursive sum type — the memory-model showcase. |
| [`grep.ty`](grep.ty) | A working `grep` CLI. |
| [`invindex.ty`](invindex.ty) | An inverted-index text search engine over a corpus. |
| [`optimize.ty`](optimize.ty) | A real compiler pass: a bottom-up tree-rewrite optimizer. |
| [`triepool.ty`](triepool.ty) | The flat node-pool + integer-index idiom for pointer-shaped data. |
| [`raytrace.ty`](raytrace.ty) | A tiny diffuse ray tracer (float + struct value semantics). |

## Applications (directories)

| Directory | What it is |
|---|---|
| [`webserver/`](webserver) | A web server serving a small Markdown site — the flagship dogfood. |
| [`site/`](site) | A static-site generator composing several corelib modules. |
| [`weblog/`](weblog) | A Combined Log Format access-log analyzer. |
| [`fetch/`](fetch) | A CLI that GETs a URL, summarizes it, and caches the result. |
| [`sqlite/`](sqlite) | FFI against a real C library — in-memory SQLite. |
| [`raytrace/`](raytrace) / [`mandelbrot/`](mandelbrot) | Float-heavy compute; `mandelbrot` uses a 16-core `parallel for`. |
| [`life/`](life) / [`snake/`](snake) / [`minesweeper/`](minesweeper) | Games dogfooding recursion and mutable grids over the arena model. |

## Core library

[`corelib/`](corelib) has one small runnable example per standard-library module
(strings, math, io, json, http, and more), each checked against a golden by `make ci`.
