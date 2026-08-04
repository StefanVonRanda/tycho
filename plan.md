# What comes next

> 2026-08-04: three owner-directed optimization phases, each validated against
> the tree before admission (evidence quoted below). Order as requested:
> interning, pool iteration, copy diagnostics. The standing build-tool candidate
> stays shelved; the "second caller" the demand rule asks for is the owner's
> explicit request.

## Validation — 2026-08-04, all three missing, all three fit

Each was checked in the source before being admitted. None needs new syntax or a
runtime primitive; all three land on surface the language already has.

### 1. Hash-consing / interning — missing, fits

No user-facing canonicalization exists: the only `intern` hits in the tree are
the C runtime's internal string-literal interning (emitted into generated code),
not a facility programs can call. The ingredients exist: generic constraints
`where hashable(T)` / `where comparable(T)` (`docs/spec/05-generics.md:50-52`,
in use at `corelib/math/math.ty:7`) and structural deep equality on
arrays/structs/maps (`docs/spec/03-types.md:449-456`). An intern table is
index-keyed canonical storage — value semantics' own shape: sharing expressed as
indices into a table that outlives its users by scope placement. One open
question is a *generic* hash (core:hash hashers take strings only,
`corelib/hash/hash.ty:36`), so the phase ships string/`[int]` keys and defers
generic keys.

### 2. core:pool live-slot iteration — missing, fits

The entire public API is `add`/`get`/`set`/`remove`/`alive`/`count`
(`corelib/pool/pool.ty:45-88`); there is no way to walk live values without
hand-skipping freed slots. The closure-over-generic pattern is proven
(`corelib/iter/iter.ty:14`), so collecting live handles is plain library code.

### 3. Val-style copy diagnostics — missing, fits

The compiler already decides every copy — elision on last use
(`src/tychoc.c:9090`), move for dead locals (`src/tychoc.c:10526`) — and already
has a first-class warning channel, `warn_at` (`src/tychoc.c:65`), LSP-parsed like
errors and in production use (`src/tychoc.c:9703`). What is missing is a warning
when a copy is *unavoidable and observable*: a diagnostic over an existing
decision, no new mechanism.

## Phase 1 — `core:intern` (hash-consing / interning)

New package `corelib/intern/`:

- `Interner($K, $V)` with `intern(&i, k) -> Handle` (canonical: first sight adds
  a node and returns its handle, later sights return the same handle),
  `get(i, h)`, `count(i)`.
- Keys `$K` in `{string, [int]}` first; one `[$V]` store, a `[$K: Handle]` map
  for lookup. May reuse `core:pool` for the store — design freedom, decided in
  the phase.
- Test `corelib/test/intern/main.ty` + golden `corelib/test/intern.out`
  (re-record with `RECORD=1 sh corelib/run.sh`, the convention at
  `corelib/run.sh:60`; the `.out` must be git-added or `make goldens-check`
  reddens).
- `docs/guides/corelib.md` entry for the package.

Gate: `make corelib` (~49s) — the only gate that runs corelib tests. `make
goldens-check` (~0.07s) for the new golden. Doc gates for the guide.
NOT `make test`: `tests/run.sh:113` globs `examples/*.ty tests/*.ty` at the top
level and never descends into `corelib/`, so it cannot redden for this phase.
No shim, so no shim-check.
Expected: every existing corelib test green plus the new one; `intern.out`
locked by the first `RECORD=1` run.

> Phase 1 evidence — 2026-08-04: `make corelib` all green incl. `intern`
> (48 tests + 1 new); `make goldens-check` ok (418 goldens tracked); doc gates
> ok (citations + links). One API decision vs the brief: `intern(&i, k, v)`
> takes the value — a `$V` store has to receive its values from somewhere, and
> "first sight wins, later sights return the same handle and ignore `v`" is
> the cache-hit semantics; the two-arg `intern(&i, k)` of the brief cannot
> name where `$V` comes from without a builder fn. Construction needs explicit
> empty-literal types, like pool: `intern.Interner([]int, []string: int)`.

## Phase 2 — `core:pool` live-slot iteration

Add to `corelib/pool/pool.ty`: `fn live(p: Pool($T)) -> [Handle]` — live handles
in insertion order, skipping freed slots. Closure-free, so no new language
surface; a callback `for_each` can follow if a caller appears.

Extend `corelib/test/pool/main.ty` to exercise `live()`; re-record `pool.out`
(`RECORD=1 sh corelib/run.sh`) and git-add the delta.

Gate: `make corelib` + `make goldens-check`. NOT `make test` (same non-descend
reason as Phase 1).
Expected: `pool.out` changes exactly as the new lines print; all other corelib
tests untouched.

## Phase 3 — Val-style copy diagnostics

In `src/tychoc.c`, at the by-value argument copy the compiler could not elide
(heap-bearing local, still live after the call), emit `warn_at` with the fix
text mirroring the existing die at `src/tychoc.c:9108` — "pass a copy you keep
(`y := %s`) or make this its last use" — plus the `inout` suggestion. The
`b := a` assignment-copy site is a separate decision, taken only if cheap.

Behavior contract: warnings go to stderr, which the pass-fixture harness
discards (`tests/run.sh:78` runs programs with `2>/dev/null`), so the new
warning cannot redden an existing fixture — but it must not *noise* the corpus
either: the bar the existing warnings set (`src/tychoc.c:9715` — verified by
emitting the whole fixture suite and finding zero spurious fires) applies to the
new one. Add fixtures for the warn case and the elided non-warn case.

Gate: `make test` (full suite, ~8 min; expected 560 fixtures green per
CLAUDE.md — verify the count on the baseline run and watch it), iterating with
`make test-fast` (~1 min). NOT the doc gates. `scripts/tools_check.sh` only if
the LSP's diagnostic shape changes — `warn_at` is already LSP-parsed, so expect
no change.
Expected: baseline count green, the two new fixtures green, zero golden drift.

## Not in scope

- Generic-key interning: needs a generic hash (core:hash is string-only today)
  — a follow-on if a caller appears.
- Any new syntax, runtime primitive, or reference type: each phase lands on
  existing surface.
- The build-tool candidate remains shelved.
