# `parallel for x in ch:` — bounded channel-drain fan-out (design)

Status: DESIGN (implementation in progress). Adds the one genuinely-ergonomic
gap in the concurrency surface: bounded fan-out of an *unknown-length* stream of
work, with K=ncpu workers and channel-capacity backpressure.

## Why this, and why it is not already shipped

Bounded fan-out of a **known** count is already expressible and tested
(`tests/conc/select_parfor.ty`): `parallel for i in range(0, N)` with a `select`
inside, K=ncpu chunks sharing a captured channel. The single-consumer
drain-until-closed loop is also already idiomatic (`tests/conc/select.ty`):
`for true: select { recv(ch,x): ...; closed: break }`.

What is missing is the *parallel* drain of an **unknown-length** stream: N items
not known up front, K workers, each pulling until the channel is closed+drained.
The verbose form requires the programmer to know N to size the `range`. This note
adds `parallel for x in ch:` as the sanctioned sugar for that pattern.

## Surface

```
parallel for x in ch:        # ch : Channel(T), x : T
    <body>                   # runs once per item; reductions (n = n + e) allowed
```

Semantics: K = `tycho_ncpu()` worker chunks share `ch` (a scalar handle, copied
by value into each chunk exactly as `select`-in-parfor already does). Each worker
pulls items via `select`/`recv` until `ch` is closed AND drained, then exits.
Every item is consumed exactly once (Vyukov MPMC CAS guarantees it). Producer
must `close(ch)` when done or workers block (same contract as the manual
`for true: select { ...; closed: break }`).

## Why a codegen drain-mode, not a parse-time desugar

The parser cannot tell `parallel for x in <array>` from `parallel for x in
<channel>` — both are `for x in EXPR` and types are unknown at parse
(`src/tychoc.c:2436`, the foreach desugar emits `len(EXPR)` / `EXPR[i]`, valid
only for arrays/strings). And `parallel for x in xs` over an array is a tested,
shipped path (`tests/conc/parfor.ty:21`) that must stay byte-identical.

So the channel-vs-array choice is **type-directed** and happens where types are
known:

1. **Parse.** For the `parallel` + foreach case only, build a node carrying the
   raw `(var, EXPR, body)` instead of immediately array-desugaring. Sequential
   `for x in arr` is untouched (zero risk to the heavy-use path).
2. **Resolve.** `type_of(EXPR)`:
   - `Channel(T)` → drain-mode; bind `x : T`.
   - array/string → reconstruct the existing index desugar (`x := EXPR[i]` over
     `range(0, len(EXPR))`) — same emission as today, so `parfor.ty` stays green.
3. **Codegen (drain-mode).** Reuse the chunk-proc lift + reduction-join exactly.
   Differences from range-mode: `_pk = tycho_ncpu()` (one worker per chunk, not a
   split range), and each chunk body is the drain loop instead of a counted
   `for i in [lo,hi)`:
   ```c
   for (;;) {
       <select over the single channel `ch`, with one recv(ch,x) arm running BODY,
        and the closed arm -> break>
   }
   ```
   The `select` codegen already exists (`src/tychoc.c:7671`,
   `compiler/tychoc0.ty:6498`); drain-mode emits it with one recv arm + an
   implicit `closed: break`.

## Risks / invariants

- **break-gate.** `parallel for` forbids `break` (`compiler/tychoc0.ty:8865`).
  The drain loop's `break` belongs to the *inner* `for(;;)`, not the parfor —
  the synthesized inner break must be emitted directly in codegen, below the
  body-gate that scans user statements, so the gate never sees it.
- **Reductions.** A drain body doing `sum = sum + x` is an ordinary parfor
  reduction; the existing accumulator fold at the join applies unchanged
  (`src/tychoc.c:6952`).
- **Fixpoint.** `tychoc0.ty` itself uses no `parallel for x in <channel>`, so the
  new path is inert during self-compilation → `cB.c == cC.c` must stay
  byte-identical. Gate: `make fixpoint`.
- **Parity.** Every fixture runs under both compilers with identical stdout
  (`make conc`, `make test`).
- **Termination contract.** A worker only exits on closed+drained. If the
  producer never closes, workers park forever — documented, identical to the
  manual idiom. Not a deadlock the compiler can or should prevent.

## Verification plan (each stage: both compilers identical stdout → make conc →
make test → make fixpoint, all green before next)

1. Worked example `examples/workers.ty` (+ `.out`) using TODAY's primitives
   (known-count `parallel for i in range` + select) — the golden the sugar must
   match. (no compiler change)
2. Parser: route `parallel for x in EXPR` to the type-undecided node; keep array
   path emission identical. Add `tests/conc/parfor_chan.ty` (fails until 3).
3. Resolve + codegen drain-mode (both compilers).
4. Docs: `docs/concurrency.md` "Bounded fan-out" section pointing at the example.
