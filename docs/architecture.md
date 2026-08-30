# Architecture & project status

Where the project stands: how it's built, what each verification gate proves, what's
shipped, and what's a decided non-goal. Everything here is checked against the
transpilers and gates, not asserted from memory.

Tycho tests one claim: value semantics
makes hierarchical arena allocation fully implicit, with no whole-program analysis. It
transpiles to C and is MIT-licensed. For the argument itself see [thesis.md](thesis.md);
for an honest accounting of where the model wins and loses see
[internals/value-semantics-limits.md](internals/value-semantics-limits.md).

## The pieces

**Two compilers ship, and only one is normative.**

`src/tychoc.c` — 14,529 lines of C, the reference implementation. It and [the
spec](spec/) define the language: where the two disagree, this one is right by
definition.

`compiler/` — 18,254 lines of Tycho across 13 files, the same language written
in itself, built as `tychoc1`. Six packages: `lex`, `parse`, `ast`, `types`,
`emit`, `driver`. It exists twice over — to prove the language can carry a real
program, and to be a second opinion on the first. `make parse-check` scores its
front end against `./tychoc`'s own answers file by file; `make tychoc1-check`
runs the whole fixture corpus and every tool lane with `TYCHOC=./tychoc1`.

### Bootstrapping

`make tychoc1` builds in two stages (`Makefile@tychoc1`):

```
./tychoc          compiler/main.ty -o tychoc1-stage1
./tychoc1-stage1  compiler/main.ty -o tychoc1
```

The second stage is not ceremony. Stage 1 is emitted by the C compiler; the
shipped `tychoc1` is emitted by a compiler that was itself written in Tycho, so
a defect in tychoc1's own code generation reaches the binary you actually run
instead of hiding one generation back.

The fixpoint holds. Generating three further compilers from `compiler/main.ty`
gives byte-identical C at every hop — gen2 == gen3 == gen4, measured
2026-08-30. Note that the corelib is resolved relative to the running binary,
so a generation built somewhere else must be run from the repo root or it
cannot find `core:strings`.

`TYCHOC1_CFLAGS` (`Makefile@TYCHOC1_CFLAGS`) links `-static-pie`, not
`-static`: a plain static link leaves `.eh_frame` unsorted, so the first unwind
runs `classify_object_over_fdes` over the whole table — 201,604 Ir on every
compile regardless of input size.

## The verification surface

`make ci` runs the whole gate locally — there is no hosted CI, by policy. What each
step proves:

| Gate | Proves |
|---|---|
| `make test` | golden-output tests pass under ASan/UBSan/LSan; `tests/reject/` must fail with a non-empty diagnostic; `tests/abort/` must die with a `tycho:` message; `tests/diag/` and `tests/warn/` match their recorded stderr. |
| `make ilp32` | the same fixture suite rebuilt under `gcc -m32`, golden-compared: Tycho `int` stays 64-bit off LP64. |
| `make asan-self` | `src/tychoc.c` itself built with ASan+UBSan, compiling the whole corpus (the compiler's *own* memory safety). |
| `make corelib` | every corelib package + examples + the `site` dogfood vs recorded goldens. |
| `make raytrace` / `make mandelbrot` | float-heavy value-semantics dogfoods (a ray tracer, a 16-core `parallel for` reduction): tychoc == ASan (+ TSan), golden-locked. |
| `make conc` | spawn / parallel-for / channels: native + ASan + TSan vs goldens; aborts fire, rejects are refused. |
| `make ffi` | `extern` FFI vs golden, ASan-clean; affine-handle misuse and library-name injection refused. |
| `make fuzz` | random valid programs: tychoc's native `-O2` and ASan/UBSan builds must agree byte-for-byte and neither may fault. |
| `make fuzz-reject` | malformed input: tychoc must fail closed — never crash, and anything it accepts must emit valid C. |
| `make fuzz-leak` | LeakSanitizer: no arena / owner leaks. |
| `make tools-check` | formatter idempotence + semantic preservation + LSP smoke. |
| `bench-guard` | tree-alloc wall: Tycho must beat C (perf-regression gate). |
| `make recursion` | deep input fails closed (no stack-overflow DoS). |
| `make spec-check` | the spec's grammar matches the prose, its fixtures exist, and its examples produce the documented output. |

The `make hooks` pre-push gate runs the full deterministic lane set plus a fast fuzz
smoke, so a red `make ci` can't reach `main`: a green `make test` is *not* a green tree.

## Shipped

- **Types:** int/float/bool/string/char/bytes, the full fixed-width numeric family
  (`u8`…`u64`, `i8`…`i64`, `f32` — first-class, defined wrap), arrays + nested, maps
  (`[K:V]`, scalar **and** composite keys and values), tuples, structs, enums,
  `Option`/`Result`, struct-of-arrays, newtypes, typed FFI handles.
- **Language:** generics (monomorphized — structs/enums/fns, `where` constraints,
  recursive + nested), pattern `match`, expression-valued `if`/`match`, closures
  (capture by value at creation -- upward closures ship; the Scheme interpreter
  in tools/tycho-scheme returns capturing closures and mutates captured
  bindings via explicit set! semantics), UFCS methods, f-strings, `or_return`, compound assignment,
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
