# Plan: five transfers from Goose

Status: ACTIVE 2026-11-17. Owner asked for all five ("Do all 5", 2026-11-17),
which is the sign-off for the surface additions in phases 1 and 8–9.
Background: [goose-review.md](goose-review.md) (2026-10-21) and the
2026-11-17 follow-up comparison. Value semantics, implicit arenas and "no
reference type" are NOT up for change in any phase.

## Rules for every phase (read before starting)

- **Load the `tycho-syntax` skill before writing or editing any `.ty` file.**
- One compiler: `compiler/` built as `./tychoc1` (`make tychoc1`). Runtime is
  `runtime/tycho_rt.c` (+ `corelib/tycho.h`). `bootstrap/tycho_compiler.c` is a
  generated artifact; never hand-edit it. If a phase changes the runtime ABI
  the bootstrap C depends on, regenerate it with the current compiler and prove
  `make bootstrap-check` and `make fixpoint-check`.
- Every optimization or representation change gets a `TYCHOC_NO_<NAME>=1`
  control (precedent: `TYCHOC_NO_INLINE_ENUM`, listed in `compiler/main.ty`
  help text) unless the phase line says otherwise. Measure with `bench/peakrss.c`,
  median of five, byte-identical output, and record the numbers in the doc the
  phase names.
- A phase whose measurement shows no win records the negative result in its doc,
  reverts the code, and still ticks and commits (the doc is the deliverable).
- **One fast check, then commit (owner, 2026-11-17: no "test theater").** Run
  only the single quickest check that proves your own change: your new fixture
  compiled and run by hand, or one make target. Do NOT run `make ci`,
  `make test`, `parse-check`, `fixpoint-check`, `corpus-check` or any other
  suite "to be safe". Exception: if you change `surface.lock` contents, run
  `python3 scripts/surface_lock.py --record`. Phase "done when" lists below are
  superseded by this rule. List the skipped gates in the commit message.
- Time box: about 35 minutes per phase. If the phase is not done by then,
  commit what is safe (docs, probe, measurements), leave the box unticked, and
  report what remains.
- Several phases may run at once. Commit only your own files
  (`git commit -- <paths>`), and tick your own plan line immediately before
  committing. Do not edit `CHANGELOG.md`; the wrap-up phase does it.
- Adding a `tests/reject/` fixture bumps census counts in several places
  (`compiler/reject_class.tsv` via `scripts/classify_rejects.py`,
  `compiler/run.sh`, `compiler/verdict_diff.py`, `tests/run.sh`). Follow the
  existing pattern; `make corpus-check` tells you what is off.
- Commit straight to `main`, one commit per phase. Message: what was wrong or
  missing, how it was checked (commands + results), what was not checked, and
  "full make ci deferred to the batch-end sweep". **No `Co-Authored-By` trailer.**
  Never `--no-verify`, never amend existing commits.
- Docs follow the repo's existing voice. Update `CHANGELOG.md` under the
  unreleased section for any user-visible change.

## 1. Index ergonomics (corelib, no compiler change)

- [x] 1.1 Add index helpers and a typed id to the corelib — files: `corelib/arrays/`, `corelib/pool/pool.ty`, `docs/reference/corelib.md`, `docs/idioms.md`, `surface.lock`, `CHANGELOG.md`, new `examples/corelib/` or `tests/` fixture — do: (a) `arrays.push_index(xs: inout [$T], v: $T) -> int` returning the new element's index; (b) a typed pool id so a handle minted by one element type cannot be used on a pool of another type: first test whether a generic newtype (`type Id($T) = int` or equivalent) is expressible; if yes add `Id($T)` plus `typed` variants of `add/get/set/remove` alongside the existing `Handle` API (corelib may gain, never change, signatures); if not expressible, use a generic single-field struct `Id($T)` and say why in the doc; (c) an `OptId`-style "maybe index" with a `-1` sentinel and `is_none`/`get_or` helpers, stored as one int (no `Option` padding); (d) a short "pools with subscripts" section in `docs/idioms.md` showing `subscript node(g: Graph, i: int) -> inout Node` so `g.node(i).weight = 10` replaces `g.nodes[i].weight`; (e) rewrite one existing index-pool example (e.g. `examples/triepool.ty`) with the helpers if it gets shorter — done when: a golden fixture exercises all helpers incl. a wrong-type id rejected at compile time (a `tests/reject/` fixture), `make corelib`, `make surface-check`, `make test-fast`, `make docs-fences` pass.

## 2. Bounds checks

- [x] 2.1 Bounds-check report — files: `compiler/driver/driver.ty`, `compiler/emit/emit.ty`, `compiler/main.ty` (help text), `docs/debugging.md` or `docs/performance.md`, new test script under `scripts/` or `tests/` — do: add `tychoc --bounds-report <file.ty>` that compiles normally and prints, per function, every surviving bounds check as `path:line: kept <kind>` and every elided one as `path:line: elided <reason>`, plus a per-function summary; add annotation-driven verification like Goose's `--bce-test`: a fixture may carry `# bounds: keep` / `# bounds: elide` comments on lines and a test asserts the report agrees — done when: a fixture with known elided (monotone `len(xs)` loop) and kept (data-dependent index) checks passes the annotation test, the report is wired into `make test` or its own make target listed in `make ci`, `make parse-check` + `make fixpoint-check` pass.
- [ ] 2.2 Elision differential fuzzer — files: `fuzz/` (new `run_bounds.py`, follow `fuzz/run_eqparity.py`), `Makefile` (target, add to the fuzz lane) — do: generate random loop-shaped programs indexing arrays with affine/data-dependent indices including off-by-one mutations; build each with default elision and with `TYCHOC_NO_BOUNDS_ELISION=1`; require identical stdout and identical abort behaviour (an out-of-range access must abort in both) — done when: target runs N≥500 programs clean in under ~2 minutes, and a deliberately broken elision (temporarily, not committed) is caught; note the catch in `fuzz/README.md`.
- [x] 2.3 Loop versioning (check once before the loop) — no implementation, measured (checks cost <5%; table in `docs/performance.md`) — files: `compiler/emit/emit.ty`, `compiler/main.ty`, `tests/bounds_elision.ty` (+ new fixtures), `docs/performance.md`, `docs/internals/goose-review.md` — do: first measure whether bounds checks are actually the cost in the flat-pipeline losses named in `docs/performance.md` (build those benches with and without `TYCHOC_NO_BOUNDS_ELISION=1` and with checks fully disabled in a scratch build); if checks cost <5%, write that up, skip the implementation, tick. Otherwise implement: for a counted loop whose array indices are affine in the induction variable (`xs[i+c]`, `xs[k*i+c]`), with arrays not resized inside the loop, emit one pre-loop range test; on pass run a check-free body, on fail run the existing checked body (so aborts still name the right line). Control: `TYCHOC_NO_LOOP_VERSION=1`. Extend the 2.1 report to say `elided versioned`. Run 2.2's fuzzer — done when: fuzzer clean, fixtures pass, measurement table in `docs/performance.md`, `make fixpoint-check` passes.

## 3. Memory density

- [ ] 3.1 Compact array descriptor — files: `runtime/tycho_rt.c`, `corelib/tycho.h`, `compiler/emit/emit.ty`, `docs/memory-model.md`, `docs/internals/design-aggregates.md`, `docs/performance.md` — do: today a `[T]` value is a 24-byte `{data,len,cap}` header (`tycho_rt.c` ~2127). Evaluate and, if it wins, implement an 8-byte representation: one pointer to a buffer prefixed with `{len,cap}` (as strings already prefix their length), with the empty array as NULL (no allocation). Start with a measurement probe (struct with 3 array fields × 1M, `[[int]]` adjacency like `bench/dijkstra`, and the bench suite memory rows) using a hand-written C model of both layouts to predict the win before touching the emitter. FFI POD-array crossing (`docs/reference/ffi.md`) must keep working. Control `TYCHOC_NO_COMPACT_ARRAY=1` if feasible; if keeping both representations in the emitter is impractical, say so in the doc and gate by measurement + full test instead — done when: `make test`, `make corelib`, `make ffi`, `make conc`, `make ilp32`, `make fixpoint-check`, `make bootstrap-check` pass, and a before/after RSS+time table (bench suite memory rows + the probe) is in `docs/performance.md`.
  - Probe done: [probe-compact-array-2026-11-17.md](probe-compact-array-2026-11-17.md), verdict go (conditional).

## 4. Strings: smaller, and views that don't copy

- [x] 4.1 String representation design + probe — files: new `docs/internals/design-strings-v2.md` — do: read how strings are represented today (`char*` with 8-byte length header before the pointer, NUL-terminated, `tycho_rt.c` ~1355), every runtime function and emitter path that depends on that, the FFI string crossing, `[string]` as `char**`, maps keyed by string, and the bootstrap. Design a 16-byte `{ptr,len}` value representation where a NUL is produced only when a string crosses into C, plus small-string inline storage (≤15 bytes inside the 16 bytes, one flag bit). Write the migration order (runtime API shim, emitter switch, bootstrap regeneration, FFI), the risks, the control var plan, and a C probe measuring RSS/time on a records workload (Goose's order example: `Order{id, customer: string, items: [Item{sku: string, qty, cents}]}` × 1M) and on `bench/` string-heavy rows for: today, `{ptr,len}`, `{ptr,len}`+SSO — done when: doc committed with the probe's measured table and a go/no-go per sub-change.
- [ ] 4.2 Switch strings to `{ptr,len}` — files: per 4.1's doc — do: implement the `{ptr,len}` representation per the 4.1 design if 4.1 said go. Strings passed to C get a NUL-terminated copy only when needed (or keep the invariant that owned buffers carry a trailing NUL while views may not, if 4.1 chose that). Regenerate the bootstrap. If 4.1 said no-go, tick with a one-line note — done when: `make test`, `make corelib`, `make ffi`, `make conc`, `make ilp32`, `make tools-check`, `make fixpoint-check`, `make bootstrap-check`, `make asan-self` pass; RSS/time table for the string rows in `docs/performance.md`.
- [ ] 4.3 Zero-copy string slices passed down — files: `compiler/emit/emit.ty`, `docs/reference/arrays-slices.md`, `docs/reference/strings.md`, fixtures — do: make `s[a:b]` / `substr` passed to a read-only parameter a view with no copy, exactly the rule arrays already follow (stored, returned or pushed still deep-copies); then let `for part in split(s, sep)` (and similar corelib iteration where the parts are only read) avoid allocating parts. Control `TYCHOC_NO_STR_VIEW=1`. Update the "there is no zero-copy string view" sentence in arrays-slices.md — done when: fixtures prove value semantics (mutating the source after storing a slice never changes the copy; a view never outlives its source), ASan clean, measured win on a split-heavy workload recorded.
- [ ] 4.4 Small-string inline storage — files: per 4.1's doc — do: if 4.1 said go for SSO, implement strings ≤15 bytes stored inline in the 16-byte value (no arena allocation). Control `TYCHOC_NO_SSO=1`. If no-go, tick with a note — done when: same gates as 4.2, plus the records probe from 4.1 re-measured against the real compiler and recorded.

## 5. Serialization and a packed record log

- [ ] 5.1 Compiler-generated `encode`/`decode` — files: `compiler/types/tcheck.ty`, `compiler/emit/emit.ty`, `runtime/tycho_rt.c`, `docs/reference/builtins.md`, `docs/spec/` (the builtins chapter), `surface.lock`, `CHANGELOG.md`, fixtures in `tests/` and `tests/abort/` or `tests/reject/` — do: add builtins `encode(v) -> bytes` and `decode$(T)(b: bytes) -> Result(T, DecodeErr)` (pick names that do not collide with the existing packed-struct `to_bytes`/`from_bytes`, which stay unchanged) for every value type that has no `ptr`/handle/channel/closure/Task inside: ints, sized ints, floats, bool, char, string, bytes, `[T]`, `[N]T`, `bounded[N]T`, maps, tuples, structs, enums incl. recursive ones, `Option`, `Result`. Format: little-endian, LEB128 lengths and zigzag ints, one-byte tags, fields in declaration order, a 1-byte format version up front. Decoder verifies every length against the remaining bytes, every tag, every bool, canonical varints, UTF-8 for `string`, and never aborts on hostile input (returns `Err`). Types containing non-serializable parts are rejected at compile time with a clear diagnostic. Add a fuzz target that mutates encoded bytes and requires decode to return `Err` or a value that re-encodes identically, never crash — done when: round-trip fixtures for every type class, reject fixture for a closure field, the mutation fuzzer clean under ASan, `make surface-check`, `make fixpoint-check`, `make parse-check` pass.
- [ ] 5.2 Packed record log in the corelib — files: new `corelib/packlog/` (or a name matching corelib conventions), `docs/reference/corelib.md`, `surface.lock`, example + golden — do: `Log($T)` storing records back to back in one `bytes` buffer using 5.1's encoding: `push(log, v)`, `len(log)`, iteration via `each`/an iterator decoding one record at a time, `clear`, no random access (document the trade-off, Goose's "sequential arrays"). Measure Goose's order example × 1M as `[Order]` vs `Log(Order)` for RSS and iteration time and record it — done when: corelib golden passes, `make corelib`, `make surface-check`, measurement table in the package doc and `docs/idioms.md`.

## 6. Wrap-up

- [ ] 6.1 Docs and record — files: `docs/internals/goose-review.md`, `README.md` ("What you give up" if any claim changed), `docs/thesis.md` if a measured number changed, `STATUS.md`, `ROADMAP.md`, rename this file to `plan-goose-transfers-DONE.md` and fix links — do: append a "2026-11 follow-up" section to goose-review.md summarizing what each phase shipped or rejected with its numbers; fix the FFI non-goal inconsistency (`docs/architecture.md` non-goals say "callbacks-into-Tycho" while `docs/reference/ffi.md` allows named-function callbacks — make architecture.md match ffi.md) — done when: `make docs-fences`, `make check-links`, `make docs-counts-check` pass.
