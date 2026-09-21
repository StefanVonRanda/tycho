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

`compiler/` — 21,498 lines of Tycho across 13 files (measured 2026-09-11; this
line read 18,254 until then, which is the drift a counted number always has),
the same language written in itself, built as `tychoc1`.
[ROADMAP](../ROADMAP.md#the-self-hosted-compiler) tracks its status and the one
question about it that is not decided. Six packages: `lex`, `parse`, `ast`, `types`,
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

The fixpoint is now gated rather than measured by hand: `make fixpoint-check`
(`scripts/fixpoint_check.sh`) has `tychoc1` emit the compiler's C, builds that
one generation `-O0` (it only has to run), has *it* emit the compiler again, and
requires the two C files byte-identical — `f(f(x)) == f(x)`. It compares the
emitted C rather than binaries, so it is free of link-time noise, and costs
~2.3s. It is `[1e/13]` in `make ci`.

### Which compiler is right

The two are **not** peers, and saying so turns "keep them agreeing forever" from
a standing tax into a procedure with a known shape:

- **`src/tychoc.c` is normative.** It defines what the language does. It is also
  the bootstrap, and the README's "one dependency-free C file" is a promise
  about it.
- **`tychoc1` is the oracle.** Its value is being a second implementation that
  can disagree. It is what every suite here defaults to
  (`tests/run.sh:175@TYCHOC`), so it is exercised far harder than a spare would
  be.

**When they disagree the question is "which is right", not "make `tychoc1`
match".** A divergence is a finding: one of the two is wrong, and which one is
decided on the merits. FRICTION 87 is the worked example — the two worded a
diagnostic differently, `tychoc1`'s answer was better, and **`src/tychoc.c` was
changed to match it**. The oracle improving the reference is the mechanism
working, not an inversion of it.

## The verification surface

**One class the lanes below do not cover**, stated so it is not mistaken for
covered: ASan sees addresses, UBSan undefined behaviour, LSan leaks, TSan races
— **none sees a read of memory that was never written**, and `arena_alloc` hands
back non-zeroed memory inside a live block, which ASan considers entirely valid.
Probed 2026-09-11 with valgrind memcheck and every arena handout marked
undefined: **371 programs clean** — all 288 flat fixtures, all 46 corelib test
packages and all 37 corelib examples — against an instrument first proved able
to fire. `tools/` was covered on 2026-09-18 by driving each tool through its own `run.sh` gate, which already supplies the arguments: 27 tools, 972 runs, 0 reads (only `tycho-debug`'s compiler-lookup leg excepted, which this method cannot reach). No lane was added because it found nothing; the method is recorded
in [`internals/probe-uninit-arena-2026-09-11.md`](internals/probe-uninit-arena-2026-09-11.md)
so it can be re-run after codegen work that touches initialisation.

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

- Traits / typeclasses (re-closed 2026-09-11 on new grounds — see below)
- A package manager (re-affirmed 2026-08-15: vendoring, Odin-style —
  [ROADMAP](../ROADMAP.md#what-production-ready-requires) §2)
- A C-style ternary `?:` (the need is met by expression-valued `if`/`match` in tail position)
- Hindley-Milner inference
- Copy-on-write / reference counting
- Manual memory-management escape hatches as the *idiomatic* path
- FFI variadics / callbacks-into-Tycho / struct-by-value / auto-bindgen
- Hosted CI

Each of those stands on a rationale of its own, independent of the one below.

### Traits / typeclasses — reopened 2026-09-11, re-closed the same day

It was reopened for a real reason and closed again for a different one, and both
halves are recorded because the first rationale is dead and citing it again would
be a mistake.

**Why it was reopened.** Traits were closed on the grounds that *"the language is
feature-complete for the thesis it exists to prove"* — decisive while proving the
thesis was the goal, and retired on 2026-08-15 when the goal became a
production-ready language
([ROADMAP](../ROADMAP.md#what-production-ready-requires)). A non-goal resting on a
retired premise is not settled, it is merely unexamined.

**Why it is closed again.** Three reasons, none of which depends on the old one.

**1. The dynamic form is impossible, not declined.** A trait object is a pointer
to someone else's value plus a vtable. There is no reference type, and that is the
load-bearing constraint of the whole model — a value escapes in exactly two ways,
both visible in the syntax ([thesis §1](thesis.md)). A `dyn Trait` stored in a
struct field is exactly the aliasing value semantics forbids. So the shape most
people mean when they ask for this — a Go or Java `interface` held in a field, a
heterogeneous list of implementors — can never exist here, whatever is decided
about the static form. [thesis §5](thesis.md) states it as a boundary of the
model rather than a missing feature.

**2. The static form is largely already present, and the gap is smaller than
traits.** A generic body is type-checked **after** substitution, not as a
template against its constraints. So this compiles and runs today, with no
constraint mechanism involved at all:

```tycho
package main

struct Point:
    x: int
    y: int

fn show(p: Point) -> string:
    return "(" + str(p.x) + "," + str(p.y) + ")"

fn render(v: $T) -> string:
    return show(v)

fn main():
    println(render(Point(1, 2)))
```

```output
(1,2)
```

Verified 2026-09-11 to hold under generic-calling-generic nesting, over `[$T]`
containers, and alongside a `where` clause on a second parameter. What blocks it
from covering the trait use-cases is not the absence of a constraint system: it is
that a name may have exactly one definition, so `show(Point)` and `show(Name)`
cannot coexist. That is **overloading**, and it is a far smaller feature than a
nominal constraint system with coherence and instance resolution.

The usual argument for nominal bounds is that structural checking produces
unreadable errors at a distant instantiation. Measured here, it does not — the
compiler names the failing call inside the generic *and* the call that
instantiated it:

```text
main.ty:14: error: argument type mismatch: argument 1 of 'show' is Name, expected Point
   14 |     return show(v)
main.ty:17: note: required from here -- this call instantiated the generic
```

**3. What traits uniquely buy is open-world extension, and this language has
opted out of the ecosystem that needs it.** Coherence rules, the orphan rule and
instance resolution exist to manage strangers adding instances to your
abstractions across a dependency graph nobody has read. Vendoring rejects exactly
that situation. Paying for coherence with no registry is the cost without the
benefit — and it lands in a 15k-line single-file compiler where the constraint
diagnostics would have to be as good as the ones above.

**The evidence that prompted the reopening was misfiled, and has been fixed
separately.** `core:io` having no stream type — `io.open_lines` returning a raw
`ptr` — was a missing **concrete type**, not a missing abstraction. It is now an
opaque `LineReader` returned in a `Result`, which is what Go's `os.Open` and
Odin's `os.open` do and what the other seventeen `Result`-returning functions in
`core:io` already did. Notably it is *not* a `handle`: a `handle` is affine and
only an `extern fn` may return one, which would have moved the opener into C and
taken the interior-NUL guard with it. So the gap closed without any new
abstraction mechanism — which is the same point reason 2 makes about generics.

**What stays open, demand-gated:** *overloading*, listed in
[ROADMAP](../ROADMAP.md#near-term). The trigger is a real program that needs one
generic to run over several user types and hits the one-definition wall — not
before. An honest adjacent limit to record when that happens: the five built-in
constraints (`numeric`, `comparable`, `has_str`, `hashable`, `defaultable`,
`src/tychoc.c@constraint_ok`) are satisfiable only by built-in scalars, so
`str(myStruct)` inside a generic stays impossible until `has_str` widens,
overloading or no overloading.

## Known limits

Pointer-shaped, structurally-shared data (tries, graphs) costs ~1.55× C in RAM because
children are stored by value, with no sharing. This is benched honestly; the recommended
idiom (a flat index-pool) is documented but deliberately not presented as "the model."
See [internals/value-semantics-limits.md](internals/value-semantics-limits.md).
