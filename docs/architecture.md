# Architecture & project status

Where the project stands: how it's built, what each verification gate proves, what's
shipped, and what's a decided non-goal.

Tycho tests one claim: value semantics
makes hierarchical arena allocation fully implicit, with no whole-program analysis. It
transpiles to C and is MIT-licensed. For the argument itself see [thesis.md](thesis.md);
for a full accounting of where the model wins and loses see
[internals/value-semantics-limits.md](internals/value-semantics-limits.md).

## The pieces

**One compiler ships.** `compiler/` is 25,642 lines of Tycho across 13 files,
built as `tychoc1` and installed as `tychoc`. Six packages: `lex`, `parse`,
`ast`, `types`, `emit`, `driver`. The [spec](spec/) is normative; the compiler
is the reference implementation in practice, and every suite here defaults to
it (`tests/run.sh:175@TYCHOC`).

`bootstrap/tycho_compiler.c` is a generated cold-start artifact: the compiler
source emitted as C by the last known-good compiler. It is not a second
implementation and is not edited by hand. The historical C compiler,
`src/tychoc.c`, is archived — not built, not tested, not updated — and remains
only because earlier design and defect records cite it.

### Which compiler is right

The self-hosted compiler is the reference implementation; the spec is
normative. The archived C compiler is not a second opinion and is never used to
resolve a disagreement.

### Bootstrapping

`make tychoc1` builds in two stages (`Makefile@tychoc1`):

```
./bootstrap-tychoc  compiler/main.ty -o tychoc1-stage1
./tychoc1-stage1    compiler/main.ty -o tychoc1
```

The first stage is built from the checked-in bootstrap C with `cc`; the shipped
`tychoc1` is emitted by a compiler written in Tycho, so a defect in its own code
generation reaches the binary you run instead of hiding one generation back.
`make bootstrap-check` proves the checked-in C builds and emits itself
byte-for-byte.

The fixpoint holds. Generating three further compilers from `compiler/main.ty`
gives byte-identical C at every hop — gen2 == gen3 == gen4. The corelib is
resolved relative to the running binary, so a generation built somewhere else
must be run from the repo root or it cannot find `core:strings`.

`TYCHOC1_CFLAGS` (`Makefile@TYCHOC1_CFLAGS`) links `-static-pie`, not `-static`:
a plain static link leaves `.eh_frame` unsorted, so the first unwind runs
`classify_object_over_fdes` over the whole table — 201,604 Ir on every compile
regardless of input size.

The fixpoint is gated rather than measured by hand: `make fixpoint-check`
(`scripts/fixpoint_check.sh`) has `tychoc1` emit the compiler's C, builds that
one generation `-O0` (it only has to run), has *it* emit the compiler again, and
requires the two C files byte-identical — `f(f(x)) == f(x)`. It compares the
emitted C rather than binaries, so it is free of link-time noise. It is
`[1e/13]` in `make ci`.

## The verification surface

**One class the lanes below do not cover**, stated so it is not mistaken for
covered: ASan sees addresses, UBSan undefined behaviour, LSan leaks, TSan races
— **none sees a read of memory that was never written**. That gap was probed
with valgrind memcheck over the fixtures, corelib packages, examples, and all
tools driven through their gates — clean — with the instrument proved able to
fire in both directions. The method is recorded in
[`internals/probe-uninit-arena-2026-09-11.md`](internals/probe-uninit-arena-2026-09-11.md)
and the controls in [controls.md](controls.md).

`make ci` runs the whole gate locally — there is no hosted CI, by policy. What each
step proves:

| Gate | Proves |
|---|---|
| `make test` | golden-output tests pass under ASan/UBSan/LSan; `tests/reject/` must fail with a non-empty diagnostic; `tests/abort/` must die with a `tycho:` message; `tests/diag/` and `tests/warn/` match their recorded stderr. |
| `make ilp32` | the same fixture suite rebuilt under `gcc -m32`, golden-compared: Tycho `int` stays 64-bit off LP64. |
| `make asan-self` | the checked-in compiler bootstrap built with ASan+UBSan, compiling the whole corpus (the compiler's *own* memory safety). |
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

These are settled — please don't propose them:

- Traits / typeclasses (re-closed on new grounds — see below)
- A package manager (re-affirmed 2026-08-15: vendoring, Odin-style —
  [ROADMAP](../ROADMAP.md#what-production-ready-requires) §2)
- A C-style ternary `?:` (the need is met by expression-valued `if`/`match` in tail position)
- Hindley-Milner inference
- Copy-on-write / reference counting
- Manual memory-management escape hatches as the *idiomatic* path
- FFI variadics / callbacks-into-Tycho / struct-by-value / auto-bindgen
- Hosted CI

### Traits / typeclasses — reopened 2026-09-11, re-closed the same day

Traits were first closed as "complete for the thesis", then reopened when that
premise was retired — and closed again on new grounds: the *dynamic* form
(trait objects held in fields or heterogeneous containers) is impossible under
value semantics ([thesis §5](thesis.md)), while the *static* form is largely
already present via post-substitution generics, leaving only overloading as the
demand-gated gap ([ROADMAP](../ROADMAP.md#near-term)). The full argument is in
[internals/value-semantics-limits.md](internals/value-semantics-limits.md).

## Known limits

Pointer-shaped, structurally-shared data (tries, graphs) costs ~1.55× C in RAM because
children are stored by value, with no sharing. The recommended
idiom (a flat index-pool) is documented but not presented as "the model."
See [internals/value-semantics-limits.md](internals/value-semantics-limits.md).
