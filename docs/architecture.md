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
| `tychoc0` | `compiler/tychoc0.ty` (~15k LoC) | **FROZEN 2026-07-26.** The self-hosted transpiler that proved self-hosting. Not gated, not maintained, diverging — see below. |
| runtime | `runtime/tycho_rt.c` (~2k LoC) | Arena allocator + string/map/channel primitives, embedded into emitted C. |
| corelib | `corelib/` (36 packages) | Standard library, imported `core:<name>`. |
| tooling | `tools/` | `tychofmt` (formatter), `tycho-lsp` (LSP), VS Code / Zed extensions. |

**Self-hosting (the fixpoint) — proved, then frozen.** With `A = tychoc·tychoc0.ty`,
`B = A·tychoc0.ty`, `C = B·tychoc0.ty`, the `fixpoint` gate asserted `B == C`
byte-identical (tychoc0 reproduces its own emitted C) and that `B`'s program output
matched the reference. It held. That is what tychoc0 was built to demonstrate, and the
result stands as recorded.

**On 2026-07-26 tychoc0 was frozen and every gate that ran it was removed** — thirteen of
the nineteen CI steps, including `fixpoint`, `frontparity`, `rtparity`, the four
accept/reject parity lanes, the differential `fuzz`/`fuzz-pkg` halves, and the tychoc0
side of `test`, `corelib`, `conc`, `ffi`, `recursion` and `spec-check`. Nothing mirrors a
language change into it any more.

The consequence to hold on to: **tychoc0 is a snapshot of the language as of the freeze
date, not a second implementation of Tycho today.** The two compilers will accept and
reject different programs from here on, and the first divergence is already in the tree —
`tychoc` rejects a reserved word used as a procedure name (`fn handle(...)`) with a
diagnostic naming the keyword, while `tychoc0` still accepts it. tychoc0 is not
deprecated for being wrong; it correctly compiles the language it was frozen against.
`tychoc` is the reference implementation and [the spec](spec/) is normative.

### 2026-07-29: the freeze lanes were retired — nothing builds `tychoc0` now

The 2026-07-26 freeze removed tychoc0 from **`make ci`**. It did not remove it from the
tree: two hand-run lanes (`compiler/fixpoint.sh`, `scripts/frontparity.sh`) and fourteen
other non-gated runners still built a `tychoc0` and still ran it, right up to
2026-07-29. On that date the language took a breaking change — the three-clause
`for i := 0; i < n; i += 1:` and bare `for:` replace `for i in range(...)`, and the
`range` builtin is deleted — and a frozen compiler that must still compile the whole
corpus stopped being co-satisfiable with a corpus adopting new syntax. `tychoc0` cannot
parse the new loop forms and never will. **Every lane that built it is retired.**

**What ended, in plain words.** Continuous proof that `tychoc0` accepts what `tychoc`
accepts, and that the two produce identical program output. That proof was load-bearing
at least once: an over-tightening of the newtype path made `tychoc0` refuse
`if dup == ids:` (`tests/newtype_agg.ty`), which reddened `compiler/fixpoint.sh`. The C
compiler accepted that program without complaint — the defect was visible *only* because
a second, independent implementation disagreed. **The class of defect now uncaught is
exactly that:** a silent narrowing of what the frontend accepts, where the only compiler
left to consult is the one that was narrowed. Recorded goldens do not catch it, because
a program that is newly rejected never reaches the golden comparison. Nothing in
`make ci` replaces this and nothing is planned to.

Two smaller losses worth naming, because no other lane covered them:
`fuzz/run_pkg.py`'s tychoc0 legs were the only consumers anywhere of the
`tychoc --bundle` post-order package stream, and `tests/rtparity/run.py` was the only
check that the runtime `tychoc` embeds actually wires up the env knobs, abort
diagnostics and arena-stats rows it defines.

**Retired, not deleted.** Every lane keeps its file, with a header recording what it
proved, what its loss costs, and that it stopped running on 2026-07-29 — so a future
reader asking "was this ever checked?" finds an answer rather than an absence.
`compiler/tychoc0.ty` itself is untouched on disk: it is the evidence that Tycho
self-hosts (a fact about the commit that proved it, which retiring a lane cannot undo),
it is still the largest single Tycho program in the tree, and `make asan-self` still
feeds it to `tychoc` as **input** under ASan/UBSan. What ended is the claim that it is
*continuously checked*.

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

> Until 2026-07-26 this table also listed `fixpoint`, `frontparity`, `rtparity`,
> `fuzz-pkg`, `typeparity`, `parforparity`, `eqparity` and `unaryparity`, and several
> rows above asserted that `tychoc` and `tychoc0` **agree**. Every one of those was a
> two-implementation gate. They were removed from `make ci` with the freeze, and the
> scripts themselves were retired on 2026-07-29 (see above). What survives gates
> `tychoc` against a **recorded golden** or a stated fail-closed invariant, never
> against a second compiler. Some of those scripts still exist and still run something
> useful — `fuzz/run_eqparity.py`, `run_unaryparity.py` and `run_parforparity.py` keep
> their written-down `expect` oracle and gate `tychoc` against it — but the second
> opinion is gone from all of them, and their headers say so.

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
