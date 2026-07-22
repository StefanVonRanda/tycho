# Architecture & project status

Where the project stands: how it's built, what each verification gate proves, what's
shipped, and what's a decided non-goal. Everything here is checked against the
transpilers and gates, not asserted from memory.

Tycho is an **experimental proof-of-concept** that tests one claim: value semantics
makes hierarchical arena allocation fully implicit, with no whole-program analysis. It
transpiles to C and is MIT-licensed. For the argument itself see [thesis.md](thesis.md);
for an honest accounting of where the model wins and loses see
[internals/value-semantics-limits.md](internals/value-semantics-limits.md).

## The pieces

| Piece | Path | Role |
|---|---|---|
| `tychoc` | `src/tychoc.c` (~11k LoC) | **Reference** transpiler (C). Full language. Emits C, invokes `cc`. |
| `tychoc0` | `compiler/tychoc0.ty` (~15k LoC) | **Self-hosted** transpiler, written in Tycho — a subset that includes itself. |
| runtime | `runtime/tycho_rt.c` (~2k LoC) | Arena allocator + string/map/channel primitives, embedded into emitted C. |
| corelib | `corelib/` (36 packages) | Standard library, imported `core:<name>`. |
| tooling | `tools/` | `tychofmt` (formatter), `tycho-lsp` (LSP), VS Code / Zed extensions. |

**Self-hosting (the fixpoint).** `A = tychoc·tychoc0.ty`, `B = A·tychoc0.ty`,
`C = B·tychoc0.ty`; `make fixpoint` asserts `B == C` byte-identical (tychoc0 reproduces
its own emitted C) and that `B`'s program output matches the reference. tychoc0 is a
*subset* transpiler; the breadth gap versus `tychoc` is tracked by the parity gates
below, which are currently broad and clean.

## The verification surface

`make ci` runs the whole gate locally — there is no hosted CI, by policy. What each
step proves:

| Gate | Proves |
|---|---|
| `make test` | golden-output tests pass under ASan/UBSan/LSan (incl. `tests/reject/` = must-fail, differential). |
| `make fixpoint` | self-host `B==C` + single files + packages + standalone driver + tychoc0 self-split. |
| `make corelib` | every corelib package + examples: C transpiler vs tychoc0, goldens match (three ways). |
| `make raytrace` / `make mandelbrot` | float-heavy value-semantics dogfoods (a ray tracer, a 16-core `parallel for` reduction): tychoc == tychoc0 == ASan (+ TSan), golden-locked. |
| `make conc` | spawn / parallel-for / channels under ASan+TSan + tychoc0 parity. |
| `make ffi` | `extern` FFI: both transpilers vs golden, ASan-clean. |
| `make fuzz` | differential tychoc-vs-tychoc0 on random valid programs + ASan/UBSan. |
| `make fuzz-reject` | malformed input: both transpilers fail closed (never crash). |
| `make fuzz-leak` | LeakSanitizer: no arena / owner leaks. |
| `make fuzz-pkg` | cross-package differential: random two-package programs, three build paths must match. |
| `make tools-check` | formatter idempotence + semantic preservation + LSP smoke. |
| `make typeparity` / `parforparity` / `eqparity` / `unaryparity` | tychoc and tychoc0 **agree on accept/reject** (the fixpoint is output-only and blind to that; these lanes close the hole). |
| `bench-guard` | tree-alloc wall: Tycho must beat C (perf-regression gate). |
| `make recursion` | deep input fails closed in both transpilers (no stack-overflow DoS). |
| `make rtparity` | the two runtimes (`runtime/tycho_rt.c` and the one `tychoc0` emits) agree on env knobs, `tycho:` traps and arena-stats rows — fixpoint is blind to this drift. |
| `make spec-check` | the spec's grammar matches the prose, its fixtures exist, and its examples produce the documented output on both transpilers. |

The `make hooks` pre-push gate runs the full deterministic lane set plus a fast fuzz
smoke, so a red `make ci` can't reach `main`: a green `make test` is *not* a green tree.

## Shipped

- **Types:** int/float/bool/string/char/bytes, the full fixed-width numeric family
  (`u8`…`u64`, `i8`…`i64`, `f32` — first-class, defined wrap), arrays + nested, maps
  (`[K:V]`, scalar **and** composite keys and values), tuples, structs, enums,
  `Option`/`Result`, struct-of-arrays, newtypes, typed FFI handles.
- **Language:** generics (monomorphized — structs/enums/fns, `where` constraints,
  recursive + nested), pattern `match`, expression-valued `if`/`match`, closures
  (downward value-capture), UFCS methods, f-strings, `or_return`, compound assignment,
  slices, destructuring, bidirectional type inference, Odin-style packages.
- **Concurrency:** `spawn`/`Task`/`wait` (affine + implicit join), `parallel for`,
  lock-free channels, `select`, a bounded spawn cap (fork-bomb fails closed).
- **Safety:** defined two's-complement wrap (`-fwrapv`), checked div/mod/bounds/substr,
  hash-flooding-resistant maps (SipHash + random seed), byte-safe strings.
- **FFI:** `extern` over scalars/string/bytes/opaque `ptr`/typed handles, sized-int
  boundary types, nullable-`Option(string)` returns, `inout` out-params.

## Decided non-goals

These are deliberate, argued, and settled — please don't propose them:

- Traits / typeclasses
- A package manager
- A C-style ternary `?:` (the need is met by expression-valued `if`/`match` in tail position)
- Hindley-Milner inference
- Copy-on-write / reference counting
- Manual memory-management escape hatches as the *idiomatic* path
- FFI variadics / callbacks-into-Tycho / struct-by-value / auto-bindgen
- Hosted CI

## Known limits

Pointer-shaped, structurally-shared data (tries, graphs) costs ~1.55× C in RAM because
children are stored by value, with no sharing. This is benched honestly; the recommended
idiom (a flat index-pool) is documented but deliberately not presented as "the model."
See [internals/value-semantics-limits.md](internals/value-semantics-limits.md).
