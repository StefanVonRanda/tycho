# What comes next

> 2026-08-04: the build tool is complete (02a2a0d, golden 9d65fd0). Three
> owner-directed phases, each verified and committed on its own, in this order:
> housekeeping, deterministic hash, map memory. The closing `make ci` (one
> sweep, at the end of the chain — the convention) lands as the final step of
> phase 3.

## Phase 1 — housekeeping: the value-semantics doc, the clean tree

`docs/internals/value-semantics-limits.md` carries the owner's uncommitted edit
describing `core:pool`'s generational indices and the `arena_recycle` reuse
story — it has sat in the working tree since before the intern phase. Commit it
on its own (it documents the pool phase it belongs with), and re-verify the
tree is otherwise clean.

Gate: doc gates only (`check_citations.py`, `check_links.sh`). Nothing else can
redden — no code moves.
Expected: one commit, tree clean (modulo later phases).

## Phase 2 — deterministic generic hash

The deferred follow-on from the generic-hash phase. Today `hash(x)` is
per-process seeded (map-consistent, in-process tables/dedup); there is no
cross-run hash for any composite value. A deterministic variant:

- **Runtime** (`runtime/tycho_rt.c`): fixed-seed forms of the scalar hashers
  (`tycho_ik_hash`, `tycho_si_hash` — the seeds are globals) and the array
  hashers (`tycho_arr_int/str/float_hash`), so a deterministic path exists
  without touching the map's seeded hashing (DoS defense stays).
- **Compiler** (`src/tychoc.c`): `gen_hash` gains a deterministic mode for the
  public `hash(x)`; composite types get fixed-seed generated hashes
  (`tycho_dhash_S_*/T*/arr_C*`) alongside the seeded map-key ones, gated by the
  same `hash_keyused` tracker — a `hash()` on a type also used as a map key
  then emits BOTH families (the map keeps its seeded functions).
- **Contract**: `hash(x)` switches to deterministic, documented as "equal
  values hash equal, stable across runs — usable for checksums and content
  addressing over composites; NOT DoS-hardened (that is the map's seeded
  internal hashing)". Exact-value test assertions become possible.
- Tests: `corelib/test/hash/main.ty` gains exact-value assertions (cross-run
  stability is now testable), intern is unaffected (it never calls `hash`).

Gate: `make corelib` + `make test` (compiler + runtime change) + `make
selfhost-check` (emitted C changes only for programs using `hash()` on
composites — the fixed point must re-prove) + doc gates. Citations re-point
(the 7th shift).
Expected: hash.out delta is the new exact-value lines; 589+ fixtures green;
fixed point holds.

## Phase 3 — map memory: the lru and idiomatic-trie gaps

The two biggest remaining losses share one root — the map's storage. lru is
now the suite's largest gap: **32.6 MB vs C 11.5 (~2.8× C)** on a delete-heavy
`[int:int]` churn workload (C's edge: backward-shift delete — no tombstones —
and one allocation per rehash, freed on growth, where the arena retains every
intermediate). The idiomatic trie (~1.56× C) is the same question at small
composite scale. A measurement-first phase:

1. **Decompose first** (the observability from the arena phase): run the lru
   and trie benches with `TYCHO_ARENA_STATS=full` and account the 32.6 MB /
   57.2 MiB — how much is the live map (four parallel arrays + idx table vs
   C's two), how much is retained growth intermediates (the arena keeps every
   doubling buffer, C frees them).
2. **Pick the levers the numbers support** — candidates, in order of
   estimated value: backward-shift delete (tombstone-free — removes `elive`
   and the compaction pass), a `reserve`-style pre-size for maps (kills the
   retained growth intermediates — the same one-line fix that closed the trie
   pool), a leaner descriptor. Small-map inline storage is the trie-specific
   stretch; explicit defer if the lru work lands first.
3. **Implement + re-measure** against the lru and trie lanes; update
   `bench/lru/RESULTS.md` / `bench/trie/RESULTS.md` and the README rows.
4. **Closing `make ci`** — the one full sweep for the whole chain.

Gate: `make test` (~8 min — the map is everywhere) + `make corelib` + `make
lru`/`sh bench/lru/run.sh` + `sh bench/trie/run.sh` (the target lanes) +
`make selfhost-check` (map codegen changes) + doc gates. Citations re-point.
Expected: lru and trie rows move measurably toward C; the decomposition is
recorded in the bench RESULTS docs before and after.

## Not in scope

- A map representation that returns memory mid-scope (the arena's high-water
  behavior is the model, not a bug — `reserve` is the answer).
- The build-tool candidate is closed; nothing else is pending after phase 3.

> Phase 2 evidence — 2026-08-04: all gates green. `make test` 590/0 (unchanged),
> `make corelib` all green, `make selfhost-check` green (the fixed point holds —
> existing emission is unchanged unless `hash()` is used), doc gates ok,
> goldens-check ok.
>
> Implementation: the runtime gained fixed-seed twins — `tycho_ik_hash_det`
> (SplitMix64 with the golden-ratio seed), `tycho_si_hash_det` (SipHash-1-3 with
> the reference key, `tycho_siphash13` parameterized by k0/k1), and
> `tycho_arr_int/str/float_hash_det` — while the map's seeded hashing is
> untouched (DoS defense stays). The compiler's `gen_hash` gained a
> `g_hash_det` mode (src/tychoc.c:9207), and composite types used by `hash()`
> emit a deterministic twin of their hash function (`tycho_dhash_S_*`/`T*`/
> `arr_C*` — fixed fold seed 0x9e3779b97f4a7c15, det leaves) alongside the
> seeded map-key ones, gated by the same `hash_keyused`. `hash(x)` now returns
> the deterministic hash; exact values are locked in the hash.out golden (the
> byte-locked golden is the cross-run proof — a seeded hash would redden it
> every run). Docs updated: 16-builtins §29.7, core:hash header, guide. 22
> citations re-pointed (+2..+61 by region).
