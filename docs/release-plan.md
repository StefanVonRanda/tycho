# Hier public-release hardening — campaign plan

This document tracks the work to harden Hier for public release: a documentation
pass followed by a set of language/runtime changes. It is a living plan — each
item records its goal, approach, blast radius, and verification status.

## Verification discipline (applies to every language/runtime change)

Hier has two compilers that must stay in lockstep: the C reference compiler
(`hierc`, `src/hierc.c` + `runtime/hier_rt.c`) and the self-hosted compiler
(`hierc0`, `compiler/hierc0.hi`, which emits its own runtime text). Every change
below lands in **both** and is gated by:

- `make test` — every `tests/*.hi` + `examples/*.hi` built twice (native `-O2`
  and `-fsanitize=address,undefined`), asserting exit 0, no sanitizer report,
  byte-identical output between the two builds, and a match against the committed
  golden.
- `make fixpoint` — the self-host fixed point: the C compiler builds `hierc0`,
  that build rebuilds it, and the last two emissions must be byte-identical
  (B≡C), plus a differential that `hierc0` reproduces the C compiler's output
  across `tests/` + `examples/` + packages.
- `make fuzz` — type-directed random programs compiled by both compilers under
  ASan/UBSan, asserting byte-identical output and no fault.

Rule: **one focused commit per item; nothing is committed until all three are
green.** A new behavior gets a regression test under `tests/` with a recorded
golden.

## Decisions log

Captured from the review discussion:

- **Doc voice:** public-first — design docs lead with the shipped design +
  examples; stage/commit history demoted to a labeled appendix, audit trail kept.
- **Tycho:** never mentioned anywhere (removed).
- **Strings (A1):** make strings byte-safe by length-prefixing everything (the
  header already exists on every string; the ops just have to use it).
- **`inout` (B6):** rename the keyword to `mut`, keep the `&` marker at the call
  site.
- **Package privacy (B3):** a leading-underscore convention — `_name` is
  package-private (resolver blocks cross-package access). Verified non-breaking
  (no current public symbol starts with `_`).
- **Map API (B5):** remove the `map_*` free functions in favor of `m[k]` syntax
  (needs a replacement surface first — see below).
- **Generics (A2):** add **Odin-style** generics only; this reverses the earlier
  "generics: decided against (firm)" stance in `docs/ideas.md`,
  `CONTRIBUTING.md`, `docs/arrays-structs.md`, and the README, which must be
  updated when it lands.
- **Scope:** heavy edits where needed; light proofread on the already-strong docs.

## Phase 0 — documentation review — DONE (commit `db1201b`)

Public-release editorial pass across ~40 docs: public-first rewrites of
`ffi.md` / `packages.md` / `concurrency.md` / `memory-model.md`; the
`map-mutation.md` / `map-values.md` scratchpads rewritten as clean design notes;
every Tycho reference removed; the learning materials re-pitched from "web
developers" to "coming from Python/JS/Ruby"; and README fixes (documented the
shipped `m[k]` sugar and `println`, added the new docs to the index, linked the
learning materials). Also fixed stale "feature absent" claims found along the way
(string slicing, `Map` "if/when added").

## Work items

Status legend: **DONE** · **IN PROGRESS** · **TODO**.

### B1 / B2 — README FAQ — DONE

Bottom-of-README FAQ pre-empting the predictable reactions: "it just transpiles
to C", "deep-copying everything must be slow", "no GC/borrow checker — how is it
safe?", and "is it production-ready?". (Lands with the A1 commit.)

### A1 — byte-safe strings — IN PROGRESS (verifying)

**Problem.** A Hier string carries an 8-byte length header (so `len` is O(1) and
`read_file`/`write_file` are binary-safe), but `copy`/`concat`/`append`/`substr`
recomputed length with `strlen`, silently truncating at an interior `0x00`. So a
value-semantic copy of a NUL-bearing string lost everything after the NUL.

**Approach.** Every string already has the header (heap strings via
`hier_str_alloc`/`hs`, literals via `hier_str_intern`/`hi_intern`), so the fix is
to make the internal ops use the header instead of `strlen`, and to add a
distinct boundary copy for *bare C strings* (no header):

- Internal, header-based: `hier_str_copy`/`scopy`, `concat`/`sc`,
  `concat_char`/`sc_char`, `append`/`hi_append`, `substr`, and a new
  header-based print (`hier_print_s` / `hi_puts`).
- Boundary, `strlen`-based: a new `hier_str_from_c` / `s_from_c`, used wherever
  foreign bytes enter — `getenv`, `argv`, `list_dir` entries, `read_file`'s empty
  fallback, and FFI string returns.
- `chr(0)` now yields a real one-byte NUL string.
- The string-accumulator sidecar length now reads the header, not `strlen`.
- Fixed every "concat-with-empty as a copy" idiom (`sc(ar, x, "")`) in hierc0's
  struct-copy, tuple-copy, and map-key paths to `scopy` (a bare `""` has no
  header). The C `hier_str_split` (manual buffers) and `list_dir` were producing
  non-headered strings and were rebuilt to emit headered ones.

**Verified.** `make test` green (161/0, incl. a new `tests/string_nul.hi`);
ASan/UBSan/LSan clean; `make fixpoint` B≡C holds. `make fuzz` pending as the
final adversarial check.

**Out of scope (documented limits).** `find`/`split` (search), map-key hashing,
and `eprint` still use NUL-terminated (`strlen`/`strstr`) semantics — rare with
embedded NULs; flagged as follow-ups rather than expanding this change.

### A3 — Option / Result papercuts — TODO

Add structural `==`/`!=` on `Option`/`Result` (same machinery as struct/enum
equality) and `or_return` for `Option` (mirror the `Result` lowering). Both
compilers, goldens, fuzzer arm.

### B6 — `inout` → `mut` — TODO

Rename the keyword to `mut`, keeping the `&` call-site marker
(`fn incr(n: mut int)` / `incr(&x)`). Mechanical but wide (~46 files + both
lexers + all docs). One rename pass behind the fixpoint.

### B4 — tab indentation — TODO

`src/hierc.c` (and hierc0's lexer) currently reject tabs. Allow tabs *or* spaces
for indentation, rejecting only a *mix* within one line's leading whitespace (the
single ambiguous case). Both lexers; indentation drives block structure, so test
carefully.

### B3 — package privacy — TODO

A top-level `_name` is package-private: the resolver rejects qualified
cross-package access to it. Additive resolver rule, non-breaking. Both compilers.

### B5 — remove `map_*`, keep `m[k]` — TODO

Breaking API change; the replacement surface must land first, because `m[k]`
does not yet cover everything `map_*` does:

- **read** a composite value: `m[k]` currently returns only scalar values by
  value (composites are place-only) — extend it to read composites by copy.
- **membership**: a `k in m` form (replacing `map_has`).
- **deletion**: a `delete m[k]` (or `del`) form (replacing `map_del` — there is
  no deletion syntax today).
- keep `keys(m)` and `len(m)`.
- decide the fate of a non-zero default (today only `map_get(m, k, d)` gives one).

Then remove the `map_*` builtins and migrate corelib/examples/tests/docs
(`map_*` spans ~35 files).

### A2 — Odin-style generics — TODO (design doc first)

The largest item and a reversal of a "firm" decision, so it starts with a
written design (`docs/generics.md`): `$T`-style parameters, scope
(functions first, structs later), and how monomorphization composes with the
implicit-arena model (the container monomorphization machinery already exists).
Then staged implementation in both compilers behind the fixpoint. The
"decided against" passages in the existing docs get updated when it lands.

## Sequence

Per the chosen order: **A1** (current) → A3 → B6 → B4 → B3 → B5 → A2. Each is its
own commit, fully green before the next.
