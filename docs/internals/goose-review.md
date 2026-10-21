# Goose review: what transfers to Tycho

Status: research note, 2026-10-21. No language or runtime change is implied by
this file.

Sources: [aardappel/goose](https://github.com/aardappel/goose), read at
`master` on 2026-10-21: its README, [language spec](https://github.com/aardappel/goose/blob/master/docs/goose_spec.md),
[reference](https://github.com/aardappel/goose/blob/master/docs/reference.md),
[implementation notes](https://github.com/aardappel/goose/blob/master/docs/implementation.md),
[packages](https://github.com/aardappel/goose/blob/master/docs/packages.md),
[standard library](https://github.com/aardappel/goose/blob/master/docs/stdlib.md),
and the samples/benchmark notes. Goose's performance claims are its author's
measurements, not independently reproduced here.

Constraints for this review:

- Tycho keeps value semantics, implicit arenas and no reference type.
- No stored borrows, slices, relative links or stable references are coming
  back through this work.
- A feature is only worth adding when a real program needs it, with a measured
  control and a gate.

## The useful split

Goose's headline features are mostly consequences of a different memory model.
Its data stacks make growth stable and free; references and slices are cheap
because the compiler proves their roots; variable-size records are inline
because there is no heap. Tycho does not need those mechanisms to get the
useful parts.

What transfers is narrower:

1. **Fresh values should be built where they are stored**, not in a temporary
   and copied afterwards.
2. **A function should be able to state a bounds contract once**, and the
   compiler should report whether any check survives.
3. **A fixed-size sum value can be stored inline**; Tycho boxes user-defined
   enum payloads, while the reference compiler's `Option`/`Result` are already
   inline.

Everything else is either incompatible with the value model or belongs behind
a concrete caller.

## 1. In-place construction for `push` (highest value)

Goose makes this a language guarantee: a value constructed into an array slot,
a return slot or a field is built there, through calls and branches
(spec section 4.3). Tycho already has the return-slot move, the caller-arena
return and the O3 last-use move into `push`. The gap is fresh expressions that
are neither a named local nor a return value.

A local probe makes the gap visible. For an outer `[string]`:

```text
out: [string] = []
for i := 0; i < N; i += 1:
    push(out, str(i) + "...")
```

the emitted C is, in shape:

```c
tycho_arr_str_push(&owner,
    tycho_str_copy(&owner, tycho_str_concat(&owner, ...)));
```

The concatenation has already built a fresh string in the destination arena;
`push` then allocates and copies it again before storing the pointer. The
original buffer is left as dead scratch. The same issue applies to a fresh
struct or array passed to `push`, and to a call result that is copied into the
receiver.

The fix is not "add references". It is a compiler destination:

- Evaluate the pushed expression in the array's owning arena when it is a
  fresh value and cannot alias the receiver.
- Store it with the existing `push_move` path, or construct it directly in the
  slot when the element is inline.
- Keep the current copy path for places, borrowed values, views, a value that
  can reach the receiver, and every case the analysis cannot prove.

This should become a documented guarantee only after it is enforced for
returns, `push`, array literals and control-flow branches. Until then it is a
conservative optimization.

Validation: a fixture that pushes fresh strings and fresh records into an
outer array, compiled with a `TYCHOC_NO_FRESH_PUSH` control. Measure peak RSS
and wall time (median of five), require byte-identical output, then run the
sanitizer lanes and the compiler fixpoint. The existing O3 `push_move` path and
`bench/opt/movebuild` are the precedent, not a replacement for this test.

## 2. Bounds contracts and a check-free report

Goose's strongest compiler-side idea is not a new type: it is the **contract**.
`assert` establishes a fact once, index types retain bounds as values, and
`checkfree fn` rejects a kernel if a bounds check survives
(reference sections 15 and 16; spec sections 7.14 and 10.5). The compiler also
prints the surviving checks per line with `--bce-lines`.

Tycho already removes the checks in monotone `len(xs)` loops
(`TYCHOC_NO_BOUNDS_ELISION=1` is the control), but it has no assert builtin and
no way to state or inspect a cross-call length relation. Two pieces are worth
considering separately:

- **Facts from guards.** A prototype proved `if j >= 0 and j < len(xs)` in a
  branch and removed the check from `xs[j]`. On 5,000,000 elements x 12 passes
  it was 43 ms against 40 ms for the control: the C compiler already folds the
  duplicate condition and the branch predicts. No measured win; the prototype
  was reverted. A real check-free path needs index types that retain validation,
  not more branch facts.
- **A reporting mode.** `tychoc --bce-report` (name unresolved) should print
  every bounds check that survives in a function, with its source line. A
  `checkfree` source annotation is the stronger version, but only add it when a
  real kernel wants the guarantee.

Full Goose index types do not map directly. Tycho arrays are values that can
shrink; a `u32 in nodes` needs a stable, named, grow-only array. A later
version could tie an index type to a `bounded[N]T` or a new grow-only array,
but that is a separate design. Do not smuggle references in to get it.

Validation: a graph or stencil kernel with a data-dependent index and an
explicit length precondition. Build with the report and with
`TYCHOC_NO_BOUNDS_ELISION=1`; compare checks, output, RSS and time. The gate is
the report's accuracy, not a promised speedup.

## 3. Inline fixed-size enum payloads

Goose's fixed-mode enum stores tag plus the largest payload inline; variable
mode stores each value at its own size and gives up indexing. Tycho stores user-defined enums as pointers to one tagged cell, with the
payload inline in that cell (`docs/internals/design-enums.md`). `Option` and
`Result` are already inline value structs; user enums are the remaining boxed
case.

A 1,000,000-value probe compared a fixed three-variant user enum with the
equivalent packed tag-plus-payload record (the lower bound for unboxing):

| value | peak RSS | wall time |
|---|---:|---:|
| `[Kind]` old pointer cell | 28,068 KB | 7 ms |
| `[Kind]` inline packed tag+union | **19,872 KB** | **5 ms** |

That is 29% less memory and about 29% less time. The same effect appeared for
`Option(int)` against a packed record: 28,500 KB / 8 ms versus 19,500 KB /
6 ms. `Option` and `Result` already have the inline rep; they pay padding, not
an allocation. An inline representation for *user* enums whose variants are all
fixed-size and non-recursive would remove the per-value cell and pointer chase;
recursive enums stay boxed. This is a layout optimization, not variable-size
records and not variable-mode enums; the value model is unchanged.

**Status 2026-10-21: kept in the shipped `tychoc1` for scalar payloads.** A
fixed user enum with scalar payloads is now a packed tag+union value; recursive,
generic, fieldless, package-mangled and heap-payload enums keep the pointer
cell. The control `TYCHOC_NO_INLINE_ENUM=1` reproduces the old representation.
The reference compiler still uses the old representation; porting it is a
parity follow-up.

## 4. Static function values: measure first, do not restrict closures

Goose passes function values as compile-time entities: they cannot escape, so
every higher-order call is direct and inlinable (spec section 7.6). Tycho
deliberately supports escaping closures and arrays of closures. Copying
Goose's restriction would be a regression.

The possible transfer is an optimization: specialize a call when a function
literal or named function reaches a higher-order parameter and the value does
not escape. The C backend can then inline the closure body and skip the
environment.

Measure before building it. A local `apply` probe with 10,000,000 calls was
already ~7 ms, which is consistent with the C compiler devirtualizing the
constant function values. The `bench/prongB/dispatch` workload is the opposite
case: an array of escaping closures, which Goose's static values cannot express
and Tycho can. Tycho's more expressive closure model should stay; treat this as
a compiler optimization only if a real higher-order workload shows a gap.

## 5. Lower-priority ergonomics

These are real Goose conveniences, but none is a reason to change the model:

- `guard` and long-distance `return ... from` (spec 7.9) versus Tycho's
  `Result` + `or_return`. Tycho's errors are explicit values; hidden
  non-local control flow is not obviously an improvement.
- Field defaults and `default<T>()` remove constructor boilerplate, but
  `Option` and explicit constructors already cover the use cases.
- Case functions (spec 8.2) need overloading, which Tycho has deliberately left
  demand-gated; `match` already gives exhaustive dispatch.
- `embed_file` makes a single-file program self-contained. It is small and
  useful for tools and tests, but it is a builtin, not a memory-model change.
- `simd fn` and `inline fn` are implementation directives. Tycho's C backend
  already relies on the C compiler for those choices.

## Reject: incompatible or already covered

- **Data stacks / no heap / no allocator.** Tycho's arena model exists to let
  values escape and copy safely. Replacing it would remove the language's
  central guarantee.
- **References, slices as stored values, relative or narrow links, stable
  pointers, growth-preserving references.** These are the reference model the
  project deliberately does not have.
- **`reusable` pools with stale-slot reuse.** `core:pool` already gives Tycho
  index-plus-generation handles and rejects a stale handle. It is the safer
  version of the same idiom; keep it conditional, as the benchmark says.
- **Inline variable-size records and variable-mode enums.** Goose's largest
  memory wins, but they need an address-stable layout and sequential access,
  and the current Tycho data-layout measurements do not justify a new record
  model. Revisit only with a concrete caller.
- **A package manager.** Tycho's vendoring policy is a decided non-goal.
- **Static-only function values.** Tycho's escaping closures are a feature, not
  an accidental cost.

## Recommendation

1. Ship destination construction for fresh `push` values. It earned its keep:
   ~35–43% less peak RSS and ~17–20% less wall time on the string case.
2. Treat guard-derived bounds facts as a rejected experiment; a reporting mode
   may still be useful, but no speed claim is made.
3. Keep inline fixed-size *scalar-payload* enums in `tychoc1`; port the
   reference compiler as a parity follow-up.
4. Leave static function specialization and the small ergonomics demand-gated.
