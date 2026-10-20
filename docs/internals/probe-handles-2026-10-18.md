# Probe: entity store with generational handles — 2026-10-18

Model: Muse Spark 1.3 Contributor (same family as the maintainer's assistant;
the external-model route was unavailable — the `opencode` API key is expired
and `openai-codex` is rate-limited — so the probe ran as a fresh agent with no
compiler, internals, RFCs, or prior findings in context, in a stripped tree).
Host: Linux x86_64. Compiler: `tychoc 0.8.7`.

Program: `store.ty` (257 lines, single file, no imports; thrown away after
this record). An entity store with opaque `Ent` handles (`type Ent = int`,
slot index low 16 bits, generation above), slots in a `bounded[8]Slot` table.
Destroy bumps the generation; every accessor funnels through `check()`, so a
stale handle returns `Err`, never a wrong value; slot reuse never resurrects
the old handle; snapshot is one `name|val|gen|alive` line per slot with
generation-preserving reload. MUST-use coverage: `newtype` handle,
`bounded[N]` table, closures (capturing predicate), `Option`, `Result`
everywhere, `or_return` and `or_else` on both `Option` and `Result`, file
persistence including a stale-rejected-after-reload check.

## What went right

First-draft architecture survived intact, modeled on `corelib/pool/pool.ty`:
`check()` as the single validation funnel, `Result`/`Option`/`or_return`/
`or_else` exactly as `docs/reference/enums-options.md` describes.
`bounded[CAP]Slot` with a `const CAP`, `push`/`len`/index through `inout`,
`str()` on ints, string concatenation for serialization, `Result(void,
string)` with bare `Ok()` and `Ok():` arms (copied from `corelib/io/io.ty`)
all compiled and ran first time. The stale-handle story works end to end:
destroy-then-read rejected, reuse mints a new generation, the old handle
reports the mismatch, the snapshot round-trip preserves both live values and
staleness. An unused-`Result` warning caught one genuinely swallowed error
during the draft.

## Findings (adjudicated against `main`, all reproduced)

1. No `string -> int` builtin (`to_int` is `float -> int` only); the strict
   parse the loader needs is `core:strings.parse_int_checked`, behind an
   import. Real gap: the probe hand-rolled 20 lines of strict parsing after a
   three-page doc hunt. (Its package-mode worry was partly imagined — the
   probe directory held no competing `.ty` files, so the import would have
   worked — but the underlying missing-builtin point stands.)
2. `spawn` is a reserved keyword, met only as a compiler error when naming an
   allocator. Stated in `docs/reference/basics.md:94` (points at appendix B),
   so this is discoverability, not a gap: a reserved-word note on the
   concurrency page, the one place a newcomer meets `spawn`, would have
   prevented it.
3. `read_file` fails open (`""` for missing/unreadable/empty alike). Same
   finding as the graph probe, same day: the fail-closed alternative requires
   the import from (1). Two independent probes tripping on the same tradeoff
   is the signal.
4. `Ok():` as a match arm for `Result(void, E)` is used in `corelib/io` but
   the reference page only documents bare-`Ok(v)` inference generally
   (`enums-options.md:213`). Substantively documented; nit at most.
5. `or_return`/`or_else` need an enclosing compatible function, so fallible
   logic moved into `demo() -> Result(void, string)` with `main` a thin
   wrapper. Small, but it shapes every program. Observation, not a defect.
6. `bounded` has no `pop`/slicing (documented with a workaround), so no
   free-list-as-stack; `create` scans for the first dead slot. Newtype
   opacity is by convention only — any caller can `to_int` a handle open.

## Verification

Rebuilt clean from a scratch directory under `./tychoc` and `./tychoc1`:
byte-identical output; ASan+UBSan build identical, no findings. The
generational-handle discipline holds, but finding 6 stands as the part-2
signal: identity-by-discipline, not by construction.
