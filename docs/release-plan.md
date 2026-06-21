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

### A3 — Option / Result papercuts — DONE

Add structural `==`/`!=` on `Option`/`Result` (same machinery as struct/enum
equality) and `or_return` for `Option` (mirror the `Result` lowering). Both
compilers, goldens, fuzzer arm.

**What was already in place.** `or_return` on an `Option` was already shipped in
both compilers (`hierc` resolver at `src/hierc.c:2782`, hierc0 codegen, regression
`tests/or_return_option.hi`) — no work needed. The structural-eq codegen for
`Option`/`Result` already existed in `hierc`'s `gen_eq` (`src/hierc.c:4999`) and in
hierc0's `eq_field`, but `hierc` *rejected* the operator at the type-check
(`"cannot compare Option/Result values; match on them instead"`) while hierc0
silently *accepted* it — a latent parity violation no test had exercised.

**What this change did.**

- **`hierc`:** removed the two `die_at` gates on `==`/`!=` for `Option`/`Result`
  (`src/hierc.c:3460-3461`), making the operator match the comment directly above
  it ("equality is structural for every type … only void is incomparable"). The
  already-present recursive `gen_eq` does the rest.
- **hierc0:** fixed a pre-existing codegen bug the new test surfaced. hierc0 boxes
  `Option`/`Result` payloads behind `void*`, and `eq_field` dereffed them as
  `*(T*)(x.val)` without wrapping; when the payload is *itself* an
  `Option`/`Result` (e.g. `Option(Option(int))`), the recursion appended
  `.tag`/`.val`, and C's `.` binds tighter than `*`, so `*(T*)(x.val).tag`
  mis-parsed as `*((T*)(x.val).tag)` → uncompilable C. Parenthesized the dereffed
  payloads in `eq_field` (`compiler/hierc0.hi`). `hierc` was immune (inline
  payloads, already `(%s).val`-wrapped).
- New regression `tests/option_eq.hi` (+golden): `==`/`!=` across `Option(int)`,
  `Option(string)`, `Option([int])`, `Option(P)` struct, nested
  `Option(Option(int))`, `Result(int,string)`, and heap `Result([int],string)`.
- Fuzzer arm `opt_res_eq` in `fuzz/gen.py`: emits `==`/`!=` on `Option(int)`,
  `Option(string)`, and `Result([int],string)` values folded into the checksum.

**Verified.** `make test` 162/0; `make fixpoint` all green (B≡C + differential now
reproduce `option_eq.hi`); `make fuzz N=500` `FAIL=0`.

### B6 — `inout` → `mut` — DONE

Rename the keyword to `mut`, keeping the `&` call-site marker
(`fn incr(n: mut int)` / `incr(&x)`). Mechanical but wide (~46 files + both
lexers + all docs). One rename pass behind the fixpoint.

**What this change did.** A single repo-wide standalone-word rename (`\binout\b`
→ `mut`; `\b` treats `_` as a word char, so compound identifiers `is_inout`,
`inout_fill`, `_ina_` are untouched). It is a hard rename — `inout` is no longer
accepted (it now reads as an unknown type: *"unknown type 'inout'; did you mean
'int'?"*).

- **Collision fixed first:** hierc0's worklist scanner used a local variable
  literally named `mut` (`wl_scan_expr`/`wl_scan_body`); renamed it to `muts`
  before `mut` became a keyword (a hard keyword in `hierc` would otherwise reject
  the parameter name).
- **Both lexers:** `hierc` keyword table (`src/hierc.c:164`, `strcmp(s,"mut")`; the
  internal `TK_INOUT`/`is_inout`/`.is_inout` names are kept — invisible to users)
  and hierc0's soft-keyword check (`compiler/hierc0.hi`, `text == "mut"`).
- **All `.hi`** (tests/examples/corelib/bench/tools + hierc0's own signatures),
  **fuzz generators**, and **all docs** (article-corrected: "an inout" → "a mut").
  `tests/maps.out` golden re-recorded (a printed label changed `inout`→`mut`).
- **Editors:** vscode TextMate grammar updated (consumed directly). The zed
  tree-sitter `grammar.js` is updated, but its committed generated `src/`
  (`parser.c`/`grammar.json`/`node-types.json`) was **not** regenerated — no
  tree-sitter CLI in this env; flagged in `editors/zed/README.md` to run
  `tree-sitter generate`.

**Verified.** `make test` 162/0; `make fixpoint` all green (B≡C holds across the
rename); `make fuzz` `FAIL=0`; `make tools-check` (hierfmt/lsp handle `mut`).

### B4 — tab indentation — DONE

`src/hierc.c` (and hierc0's lexer) currently reject tabs. Allow tabs *or* spaces
for indentation, rejecting only a *mix* within one line's leading whitespace (the
single ambiguous case). Both lexers; indentation drives block structure, so test
carefully.

**What this change did.** Each leading-whitespace char counts as one indent unit
(tab = 1, space = 1), so a consistently-indented file — all tabs *or* all spaces —
nests correctly; the indent stack compares depths, not display widths. A line
whose leading whitespace contains *both* a tab and a space is rejected (the one
ambiguous case).

- **hierc** (`src/hierc.c`): the leading-whitespace loop now consumes `' '` and
  `'\t'`, tracking whether each was seen and erroring on a mix; tabs were also
  added to the inter-token whitespace skip (parity — hierc0 already skipped them).
- **hierc0** (`compiler/hierc0.hi`): the bol indent loop consumes spaces *and*
  tabs with the same mix check. (Before this, a leading tab was silently treated
  as zero indentation — a latent divergence from hierc, which errored.)
- Tests: `tests/tab_indent.hi` (+golden) — a tab-indented program with 3-level
  nesting, multi-level dedent, and blank/comment lines whose tab indent must not
  touch the stack; `tests/reject/tab_mix.hi` (space-then-tab) and
  `tests/reject/tab_mix2.hi` (tab-then-space) — both compilers must reject.

**Verified.** `make test` (incl. the new golden + the two rejects, both compilers);
`make fixpoint` all green (B≡C + differential reproduces the tab-indented program);
`make fuzz` `FAIL=0`; `make tools-check` ok (hierfmt re-derives indentation from the
tab-indented source, idempotent + semantics-preserving).

### B3 — package privacy — DONE

A top-level `_name` is package-private: the resolver rejects qualified
cross-package access to it. Additive resolver rule, non-breaking. Both compilers.

**What this change did.** A qualified reference (`pkg.x`) always names an
imported — hence foreign — package, so the rule is simply: *a qualified access
whose bare name starts with `_` is rejected*. Same-package use (always
unqualified) is unaffected; so is every public (non-underscore) symbol.

- **hierc** (`src/hierc.c`): a `check_pkg_private(qualifier, name, line)` helper
  (gated on `is_imported_pkg`) called at all four qualified-access sites —
  qualified type, qualified call, qualified payload-less variant value, and a
  match arm's qualified variant.
- **hierc0** (`compiler/hierc0.hi`): the check lives in `mangle_dotted` (which
  every dotted call/type/match-arm flows through) and in the `mangle_expr`
  variant-value site; cross-package is detected precisely as
  `resolve_pkg(qual) ++ "__" != m.prefix`.
- Verified non-breaking: no top-level `_`-prefixed symbol and no `pkg._name`
  access exists anywhere in the tree (corelib, examples, tests, the compiler
  itself), so the self-host's rt/main split never trips it.
- Tests: `tests/pkg/privacy/` (+golden) — a package whose public `doubled`/
  `triple` call a private `_scale` internally (same-package access works; the
  public API is reachable across the boundary); `tests/reject/pkg/privacy_cross/`
  — an importer that calls `secretlib._scale` and must be rejected by both
  compilers. The reject needed a new harness loop (`tests/reject/pkg/<name>/`):
  the entry's whole-directory package merge has to be isolated from the
  single-file rejects, which share one directory.

**Verified.** `make test` (incl. the new pkg golden + the package reject, both
compilers); `make fixpoint` all green (B≡C; the rt/main split is `_`-free);
`make fuzz` `FAIL=0`; `make tools-check` ok.

### B5 — remove `map_*`, keep `m[k]` — DONE

Breaking API change. The replacement surface lands first (additive), then the
`map_*` builtins are removed and call sites migrated.

**Replacement surface (additive):**

- **read a composite value** — *already worked*: `m[k]` reads scalars AND
  composites by copy (closed by the earlier m[k]-parity work; the old
  "composites are place-only" note was stale). Verified.
- **membership** `k in m` (replaces `map_has`) — **DONE** (B5.1, commit
  `4997089`): a first-class comparison-precedence operator, both compilers.
- **deletion** `delete m[k]` (replaces `map_del`) — **DONE** (B5.2, commit
  `4628a8a`): a contextual-keyword statement desugaring to `m = map_del(m, k)`,
  reusing the existing in-place/rebuild lowering.
- `keys(m)` / `len(m)` — kept.
- **non-zero default** — **decided: keep `map_get(m, k, d)` as-is.** The plan
  considered renaming it to `get(m, k, d)`, but `get` collides with five
  existing user functions — `corelib/http` `get(url)`, `corelib/csv`
  `get(rows,r,c)`, `corelib/json` `get(j,key)`, the json example, and
  `tools/lsp` — so reserving `get` as a builtin is too intrusive. `map_get` is
  therefore the one surviving `map_*` function.

**Migration + removal — DONE** (migrate `208d9eb`; removal + docs this commit):

- **Migrated** ~178 call sites (`m = map_set(m,k,V)` → `m[k] = V`,
  `map_has(m,k)` → `k in m`, `m = map_del(m,k)` → `delete m[k]`; `map_get`
  kept) across corelib/examples/tests/bench, the self-hosted compiler (its own
  internal map calls), and the fuzzer, via a balanced-paren transform plus
  idiom-aware rewrites for three edge cases: a borrowed-param mutation needs a
  copy first; `m[k]=` does not ground a pending map type the way `map_set` did
  (use a typed empty-map decl); a non-rebind `less := map_del(...)` becomes
  copy-then-delete.
- **Removed** the `map_set` / `map_has` / `map_del` builtins: a user-typed call
  is rejected at parse with a pointer to the new syntax (both compilers). The
  internal lowerings stay — `delete` desugars through `map_del`, the `m[k]` read
  through `map_get` — so the sugar keeps working. Reject tests
  `tests/reject/map_{set,has,del}_removed.hi`; the discarded-pure-result warning
  and the map docs (README, learning-guide, map-mutation, map-values, inference)
  were updated off the removed names.

**Verified.** `make test` 172/0 (incl. the 3 reject tests); `make fixpoint` all
green (B≡C — the self-host compiles itself after migrating its own map calls and
with the new user-call rejection in place); `make corelib` green; `make fuzz`
skip=0 FAIL=0; `make tools-check` ok.

### A2 — Odin-style generics — DESIGN DONE; implementation staged

The largest item and a reversal of a "firm" decision, so it starts with a
written design (`docs/generics.md`): `$T`-style parameters, scope
(functions first, structs later), and how monomorphization composes with the
implicit-arena model (the container monomorphization machinery already exists).
Then staged implementation in both compilers behind the fixpoint. The
"decided against" passages in the existing docs get updated when it lands.

**Design — DONE** (`docs/generics.md`). `$T` type parameters (Odin-style),
introduced at first occurrence and inferred from argument types by a
one-directional structural match (no Hindley-Milner). Generic *functions* infer
their parameters from arguments; generic *structs* take explicit type args, the
same surface as the built-in `Option(int)` / `Result(int, string)`.
Monomorphized over the *existing* container machinery — the interning +
per-type emission + mangling the compiler already runs for `Option`/`Result`/
`[T]`/maps — so it reverses the "the monomorphization registry … does not exist"
ground for "no generics", and is memory-model-neutral (each instantiation is
concrete value-semantic code *before* the signature-directed escape analysis, so
nothing generic survives to analyze). The doc records the reversal argument, the
two-compiler determinism rules, the non-goals, and a 3-stage plan.

**Implementation — staged, both compilers, behind the fixpoint:**
- **Stage 1 — generic functions — DONE** (both compilers). `fn f(x: $T) -> T`,
  argument-inferred, every `$`-parameter required in an argument; an instantiation
  registry feeding the existing per-type emission; constraints checked at
  instantiation; UFCS generic "methods" fall out free. hierc uses a `T_TYPARAM`
  interned type + a call-site instantiate hook; hierc0 uses a `monomorphize_program`
  pre-pass (env-threaded walk → infer → intern instance sharing the template body →
  rewrite calls → drop templates), with the instance mangle matching hierc
  byte-for-byte. Test `tests/generics.hi`; all gates green (test 173/0, fixpoint
  B==C + differential, fuzz, corelib, tools-check, conc, ffi).
- **Stage 2a — generic structs (construction) — DONE** (both compilers).
  `struct Box($T)` / `Pair($A, $B)`; construction *infers* the type args from the
  field values (`Box(5)`→`Box__int`) and monomorphizes one concrete `StructDef`
  with substituted field types, reusing all downstream struct machinery. hierc
  keeps the template out of codegen; hierc0 interns it in `monomorphize_program`.
  Test `tests/generic_structs.hi`; gates green (test 174/0, fixpoint, etc.).
- **Stage 2b — generic structs (type-position) — TODO:** `Box(int)` as an
  explicit-type-args annotation in param/return/field/var positions. Rejected in
  both compilers until it lands together.
- **Stage 3** — multiple/nested params, `where` constraints (a fixed
  compiler-known predicate set), and an explicit call-site type arg for the
  non-inferable case.

When implementation lands, the "decided against" passages
(`docs/arrays-structs.md §7/§9`, `docs/ideas.md`, `CONTRIBUTING.md`, README) are
rewritten and the design doc flips from *design* to *shipped*.

## Sequence

Per the chosen order: A1 → A3 → B6 → B4 → B3 → B5 (all done) → **A2** (the last
item: design doc done — `docs/generics.md`; staged implementation remains). Each
is its own commit, fully green before the next.
