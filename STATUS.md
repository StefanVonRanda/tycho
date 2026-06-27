# Tycho — project status & map

> One authoritative map of where the project stands: architecture, the verification
> surface (what each gate proves), what's shipped, what's a decided non-goal, and what's
> actually open. If you're trying to orient — start here, not in `docs/internals/` (those
> are reference + historical design records; see the index at the bottom).
>
> Everything here is checked against the compilers/gates, not asserted from memory.
> Last synced: 2026-06-27.

## What this is

Tycho is an experimental, AOT-compiled, statically-typed systems language whose thesis is a
**value-semantics + implicit-arena** memory model: no pointers, no `malloc`/`free`, no GC,
no use-after-free, and value equality that just works. Proof-of-concept, Apache-2.0. The
honest accounting of where the model wins and loses is `docs/internals/value-semantics-limits.md`.

## Architecture

| Piece | Path | Role |
|---|---|---|
| `tychoc` | `src/tychoc.c` (~9.5k LoC) | **Reference** compiler (C). Full language. Emits C, invokes `cc`. |
| `tychoc0` | `compiler/tychoc0.ty` (~11.7k LoC) | **Self-hosted** compiler, written in Tycho — a subset that includes itself. |
| runtime | `runtime/tycho_rt.c` (~2k LoC) | Arena allocator + string/map/channel primitives, embedded into emitted C. |
| corelib | `corelib/` (25 packages) | Stdlib, imported `core:<name>` (resolved via `TYCHO_CORELIB`). |
| tooling | `tools/` | `tychofmt` (formatter), `tycho-lsp` (LSP), VS Code/Zed extensions. |

**Self-hosting relationship (the fixpoint):** `A = tychoc·tychoc0.ty`, `B = A·tychoc0.ty`,
`C = B·tychoc0.ty`; assert `B == C` byte-identical (tychoc0 reproduces its own emission) and
`B` matches the reference. `make fixpoint`. tychoc0 is a *subset* compiler — the breadth gap
vs `tychoc` is tracked by the parity gates below, and as of this sync is broad + clean.

## The verification surface (gate map)

`make ci` runs all of these (16 steps). What each proves:

| Gate | Proves |
|---|---|
| `make tychoc` | the reference compiler builds. |
| `make test` | **245** golden-output tests pass under ASan/UBSan/LSan (incl. `tests/reject/` = must-fail, differential). |
| `make fixpoint` | self-host `B==C` + single files + packages + standalone driver + tychoc0 self-split. |
| `make corelib` | every corelib package + examples + the site dogfood: C-compiler vs tychoc0, goldens match (3-way). |
| `make conc` | spawn / parallel-for / channels under ASan+TSan + tychoc0 parity. |
| `make ffi` | `extern` FFI: both compilers vs golden, ASan-clean. |
| `make fuzz` | differential tychoc-vs-tychoc0 on random valid programs + ASan/UBSan (`make fuzz-quick` = fast 60-seed loop). |
| `make fuzz-reject` | malformed input: both compilers fail closed (never crash). |
| `make fuzz-leak` | LeakSanitizer: no arena / owner-0 leaks. |
| `make tools-check` | formatter idempotence + semantic preservation + LSP smoke. |
| `make typeparity` / `parforparity` / `eqparity` / `unaryparity` | tychoc and tychoc0 **agree on accept/reject** for binops / parallel-for gates / composite `==` / unary ops (the fixpoint is output-only and blind to accept/reject divergence — these lanes close that hole). |
| `bench-guard` | tree-alloc wall: Tycho must beat C (perf-regression gate). |
| `make recursion` | deep input fails closed in both compilers (no stack-overflow DoS). |

Local-only CI (no hosted CI by policy): `make ci`, or the `make hooks` pre-push gate (test + fixpoint).
Fast inner loop while editing the compiler: `make fuzz-quick` (~1–2 min) + `make test`.

## Shipped (capabilities)

Types: int/float/bool/string/char/bytes, arrays + nested, maps (`[K:V]`, scalar **and composite
keys** — struct/tuple/array/fieldless-enum/newtype — and composite values), tuples, structs,
enums, `Option`/`Result`, soa (struct-of-arrays), newtypes (`type X = …`), typed FFI handles.

Language: generics (binding-based monomorphization — generic structs/enums/fns, `where`
constraints, recursive + nested, through containers/channels/Tasks), pattern `match` (variant +
`Option`/`Result` + `_`), closures (downward value-capture), UFCS methods (`x.f(a)`), f-strings,
`or_return`, compound assignment, slices, destructuring, bidirectional type inference (Pierce-Turner),
Odin-style packages/imports.

Concurrency: `spawn`/`Task`/`wait` (affine + implicit join), `parallel for` (+ channel-drain),
lock-free channels, `select`, bounded spawn cap (fork-bomb fails closed).

Safety: defined two's-complement int wrap (`-fwrapv`), div/mod-by-zero + bounds + substr checks
abort cleanly, hash-flooding-resistant maps (SipHash + random seed), byte-safe strings.

FFI: `extern` over scalars/string/bytes/opaque `ptr`/typed handles, nullable-`Option(string)`
returns, `mut` out-params; cc-line linking with shell-injection guard.

## Decided non-goals (do not propose these)

Traits / typeclasses · package manager · ternary operator · Hindley-Milner inference · COW /
refcounting · manual memory-management escape hatches as the *idiomatic* path (e.g. index-pool
trie — deliberately not the benchmark answer; the point is the idiomatic model's honest cost) ·
FFI variadics / callbacks-into-Tycho / struct-by-value / auto-bindgen · hosted CI.

## Known limits & open work

- **Fundamental (accepted, documented):** pointer-shaped / structurally-shared data (tries, graphs)
  costs ~3× C in RAM because children are stored by value (no sharing). Honestly benched; the
  recommended idiom (flat index-pool) is documented but intentionally not "the model." See
  `docs/internals/value-semantics-limits.md`.
- **Genuinely open (minor):** explicit early `close(h)` on a typed handle (handles already auto-free
  at scope exit — this is just an early-release optimization). `docs/internals/typed-handles-design.md`.
- No larger open backlog: an audit (2026-06-27) cross-checked every "deferred/not-yet" marker in
  `docs/internals/` against the shipped compilers — all but the above mapped to shipped or decided.

## Doc index (status-tagged)

- `docs/` (`memory-model`, `concurrency`, `ffi`, `generics`, `packages`, `corelib`, `perf`, `thesis`) — **reference**, current.
- `docs/internals/value-semantics-limits.md` — **reference** (evergreen honest-cost analysis).
- `docs/internals/open-gaps-handoff.md` — **historical**: a working-TODO doc whose gaps are now all CLOSED (kept as a record; safe to retire).
- `docs/internals/{generics-gap-fixes-plan,generics-stage2-body-cloning,composite-map-keys-design,parfor-channel-drain-design,sink-prototype,typed-handles-design,map-hash-dos-plan,integer-overflow,hylo-mvs-research}.md` — **design records** for shipped (or, for stage2-body-cloning, *declined*) work. Not open tasks.
- `docs/rfc/{ffi-threading-design-review,limited-references-spike}.md` — **decided** (limited-references: resolved as a decision, not adopted).
