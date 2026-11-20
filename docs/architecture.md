# Architecture & project status

How the project is built, what each verification gate proves, what's shipped,
and what's a decided non-goal. The argument for the model is in
[thesis.md](thesis.md).

## The pieces

**One compiler ships.** `compiler/` is 26,729 lines of Tycho across 25 files,
built as `tychoc1` and installed as `tychoc`. Six packages: `lex`, `parse`,
`ast`, `types`, `emit`, `driver`. The [spec](spec/) is normative; the compiler
is the reference implementation in practice, and every suite here defaults to
it (`tests/run.sh:180@TYCHOC`).

`bootstrap/tycho_compiler.c` is a generated cold-start artifact: the compiler
source emitted as C by the last known-good compiler. It is not a second
implementation and is not edited by hand. The old C compiler, `src/tychoc.c`,
is gone from the tree (last at commit `c7d46528`); older records that cite it
resolve against that revision ([archive note](internals/archived-c-compiler.md)).
It is never used to settle a disagreement: the spec is normative and the
self-hosted compiler is the reference implementation.

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

The fixpoint is gated: `make fixpoint-check` (`scripts/fixpoint_check.sh`,
step `[1e/13]` of `make ci`) has `tychoc1` emit the compiler's C, builds that
generation `-O0`, has *it* emit the compiler again, and requires the two C files
byte-identical — `f(f(x)) == f(x)`. It compares C rather than binaries, so it
is free of link-time noise. The corelib is resolved relative to the running
binary, so a generation built elsewhere must run from the repo root.

On Linux `TYCHOC1_CFLAGS` (`Makefile@TYCHOC1_CFLAGS`) links `-static-pie`, not
`-static`: a plain static link leaves `.eh_frame` unsorted, so the first unwind
scans the whole table — 201,604 Ir on every compile regardless of input size.
Darwin has no static linking, so there the flags are LTO and `-fPIE` only.

## The verification surface

**One class the lanes below do not cover**: ASan sees addresses, UBSan
undefined behaviour, LSan leaks, TSan races — **none sees a read of memory that
was never written**. A valgrind memcheck pass over the fixtures, corelib,
examples and tools came back clean, with the instrument proved able to fire
(method: `git show ba952872:docs/internals/probe-uninit-arena-2026-09-11.md`).

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
| `make bench-guard` | tree-alloc wall: Tycho must run under 0.60× C's time (perf-regression gate). |
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

- Traits / typeclasses (see below)
- A package manager (vendoring, Odin-style —
  [ROADMAP](../ROADMAP.md#what-production-ready-requires))
- A C-style ternary `?:` (the need is met by expression-valued `if`/`match` in tail position)
- Hindley-Milner inference
- Copy-on-write / reference counting
- Manual memory-management escape hatches as the *idiomatic* path
- FFI variadics / callbacks-into-Tycho / struct-by-value / auto-bindgen
- Hosted CI

### Traits / typeclasses — reopened 2026-09-11, re-closed the same day

The *dynamic* form
(trait objects held in fields or heterogeneous containers) is impossible under
value semantics ([thesis §5](thesis.md)), while the *static* form is largely
already present via post-substitution generics, leaving only overloading as the
demand-gated gap ([ROADMAP](../ROADMAP.md#near-term)). The full argument is in
[internals/value-semantics-limits.md](internals/value-semantics-limits.md).

## Known limits

Pointer-shaped, structurally-shared data (graphs, DAGs) cannot share by
reference; the idiom is a flat index-pool, which runs at C-class memory on the
benchmarked trie and dijkstra rows. The full win/loss accounting is in
[internals/value-semantics-limits.md](internals/value-semantics-limits.md).
