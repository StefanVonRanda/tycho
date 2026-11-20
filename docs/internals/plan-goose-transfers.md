# Plan: five transfers from Goose

Status: ACTIVE. The owner signed off on all five transfers, including their
surface additions. Background: [goose-review.md](goose-review.md). Value
semantics, implicit arenas and "no reference type" are NOT up for change in any
phase.

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
- Docs follow the repo's existing voice.

Done (see `git log --grep=phase`): 1.1 index helpers and typed pool ids;
2.1 `tychoc --bounds-report`; 2.2 the bounds-elision differential fuzzer;
2.3 loop versioning, measured and not built (checks cost <5%, table in
`docs/performance.md`); 4.1 the string design and probe
([design-strings-v2.md](design-strings-v2.md)); 4.3 zero-copy split views
(`TYCHOC_NO_STR_VIEW=1`).

## 3. Memory density

- [ ] 3.1 Compact array descriptor — files: `runtime/tycho_rt.c`, `corelib/tycho.h`, `compiler/emit/`, `docs/memory-model.md`, `docs/internals/design-aggregates.md`, `docs/performance.md` — do: today a `[T]` value is a 24-byte `{data,len,cap}` header (`tycho_rt.c` ~2127). Evaluate and, if it wins, implement an 8-byte representation: one pointer to a buffer prefixed with `{len,cap}` (as strings already prefix their length), with the empty array as NULL (no allocation). Start with a measurement probe (struct with 3 array fields × 1M, `[[int]]` adjacency like `bench/dijkstra`, and the bench suite memory rows) using a hand-written C model of both layouts to predict the win before touching the emitter. FFI POD-array crossing (`docs/reference/ffi.md`) must keep working. Control `TYCHOC_NO_COMPACT_ARRAY=1` if feasible; if keeping both representations in the emitter is impractical, say so in the doc and gate by measurement + full test instead — done when: `make test`, `make corelib`, `make ffi`, `make conc`, `make ilp32`, `make fixpoint-check`, `make bootstrap-check` pass, and a before/after RSS+time table (bench suite memory rows + the probe) is in `docs/performance.md`.
  - Probe done: [probe-compact-array-2026-11-17.md](probe-compact-array-2026-11-17.md), verdict go (conditional).

## 4. Strings: smaller, and views that don't copy

- [ ] 4.2 Switch strings to `{ptr,len}` — files: per 4.1's doc — do: implement the `{ptr,len}` representation per the 4.1 design if 4.1 said go. Strings passed to C get a NUL-terminated copy only when needed (or keep the invariant that owned buffers carry a trailing NUL while views may not, if 4.1 chose that). Regenerate the bootstrap. If 4.1 said no-go, tick with a one-line note — done when: `make test`, `make corelib`, `make ffi`, `make conc`, `make ilp32`, `make tools-check`, `make fixpoint-check`, `make bootstrap-check`, `make asan-self` pass; RSS/time table for the string rows in `docs/performance.md`.
  - Not done alone: 4.1 measured no win; folded into 4.4.
- [ ] 4.4 Small-string inline storage — files: per 4.1's doc — do: if 4.1 said go for SSO, implement strings ≤15 bytes stored inline in the 16-byte value (no arena allocation). Control `TYCHOC_NO_SSO=1`. If no-go, tick with a note — done when: same gates as 4.2, plus the records probe from 4.1 re-measured against the real compiler and recorded.

## 5. Serialization and a packed record log

- [ ] 5.1 Compiler-generated `encode`/`decode` — files: `compiler/types/tcheck.ty`, `compiler/emit/`, `runtime/tycho_rt.c`, `docs/reference/builtins.md`, `docs/spec/` (the builtins chapter), `surface.lock`, fixtures in `tests/` and `tests/abort/` or `tests/reject/` — do: add builtins `encode(v) -> bytes` and `decode$(T)(b: bytes) -> Result(T, DecodeErr)` (pick names that do not collide with the existing packed-struct `to_bytes`/`from_bytes`, which stay unchanged) for every value type that has no `ptr`/handle/channel/closure/Task inside: ints, sized ints, floats, bool, char, string, bytes, `[T]`, `[N]T`, `bounded[N]T`, maps, tuples, structs, enums incl. recursive ones, `Option`, `Result`. Format: little-endian, LEB128 lengths and zigzag ints, one-byte tags, fields in declaration order, a 1-byte format version up front. Decoder verifies every length against the remaining bytes, every tag, every bool, canonical varints, UTF-8 for `string`, and never aborts on hostile input (returns `Err`). Types containing non-serializable parts are rejected at compile time with a clear diagnostic. Add a fuzz target that mutates encoded bytes and requires decode to return `Err` or a value that re-encodes identically, never crash — done when: round-trip fixtures for every type class, reject fixture for a closure field, the mutation fuzzer clean under ASan, `make surface-check`, `make fixpoint-check`, `make parse-check` pass.
- [ ] 5.2 Packed record log in the corelib — files: new `corelib/packlog/` (or a name matching corelib conventions), `docs/reference/corelib.md`, `surface.lock`, example + golden — do: `Log($T)` storing records back to back in one `bytes` buffer using 5.1's encoding: `push(log, v)`, `len(log)`, iteration via `each`/an iterator decoding one record at a time, `clear`, no random access (document the trade-off, Goose's "sequential arrays"). Measure Goose's order example × 1M as `[Order]` vs `Log(Order)` for RSS and iteration time and record it — done when: corelib golden passes, `make corelib`, `make surface-check`, measurement table in the package doc and `docs/idioms.md`.

## 6. Wrap-up

- [ ] 6.1 Docs and record — files: `docs/internals/goose-review.md`, `README.md` ("What you give up" if any claim changed), `docs/thesis.md` if a measured number changed, `STATUS.md`, `ROADMAP.md`, `CHANGELOG.md` (every user-visible change the phases shipped), then delete this file and the links to it — do: add a section to goose-review.md summarizing what each phase shipped or rejected with its numbers; fix the FFI non-goal inconsistency (`docs/architecture.md` non-goals say "callbacks-into-Tycho" while `docs/reference/ffi.md` allows named-function callbacks — make architecture.md match ffi.md) — done when: `make docs-fences`, `make check-links`, `make docs-counts-check` pass.
