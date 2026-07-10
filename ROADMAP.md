# Tycho — improvement roadmap

> Companion to [STATUS.md](STATUS.md). STATUS says what's *shipped* and what's a
> *decided* non-goal. This says where the language could still move. It is
> deliberately opinionated and it *does* reopen a few STATUS non-goals — because
> that was the ask. Each such item is flagged **[reopens a STATUS non-goal]** and
> argued both ways, not smuggled in.
>
> Organizing principle: Tycho is feature-complete *for its thesis*
> ([docs/thesis.md](docs/thesis.md)). So "improvement" splits three ways —
> **(1) fits the thesis** (build freely), **(2) worth reopening** (the decision
> was defensible but not airtight), **(3) fights the thesis** (would make it a
> different language — listed for completeness, with the verdict). Sorted by that,
> not by size.

---

## Tier 1 — Fits the thesis cleanly (build freely)

These are pure additions. None touch value semantics, arenas, or the escape rule.
Ranked by value.

### 1.1 Sized / unsigned integer types — *this is the real "byte type" ask*
**State today:** the only integer is `int` = 64-bit signed (`types.md:18`). A byte
scalar already exists as `char` (0–255, `types.md:22`) and binary buffers as
`bytes`. Missing: `u8 u16 u32 u64 i8 i16 i32`, i.e. **unsigned and sized**.
`float` is likewise f64-only — no `f32`.

**Why it matters (highest-leverage item on this list):**
- **FFI correctness.** Right now `extern` cannot faithfully type a C `uint32_t`,
  `size_t`, `int16_t`, or `float`. Everything funnels through 64-bit signed `int`.
  That's a silent-truncation / sign-mismatch hazard at the exact trust boundary
  Rule 5 says to be paranoid about. Sized ints fix FFI more than any FFI-specific
  feature would (see 3.3).
- **corelib crypto/hashing/binary I/O.** SipHash, SHA-256, MD5, base64, CSV, and
  the arena hash all currently express byte/word math by masking a signed 64-bit
  `int` by hand. Native `u32`/`u64` with defined wrap makes that code correct by
  construction instead of correct by discipline.
- **Binary formats** (file headers, network protocols, `bytes` parsing) want
  fixed-width fields.

**Thesis fit:** total — scalars don't allocate, don't alias, don't escape. Zero
interaction with the arena model. The only real design work is the conversion/
promotion rules (explicit casts vs implicit widening) and keeping the "byte ≠
number" discipline that `char` already enforces (`types.md:44`).

**"Byte type" verdict:** you already have the byte *scalar* (`char`) and the byte
*buffer* (`bytes`). What you're missing is the **sized-integer family**. Ship that,
not a redundant `byte` alias.

**Effort:** medium. Type-checker + codegen + cast rules + corelib migration.
**Priority: high.**

**Status — substantially shipped (commit `3870125`).** Two slices are done:

1. **First-class `u32`/`u64`/`f32`.** Full in-language types: arithmetic, bitwise,
   shifts, div/mod, comparison, `[]u32` arrays, printing, and conversions
   (`to_u32`/`to_u64`/`to_f32`/`to_int`/`to_float`). `u32`/`u64` wrap at their C
   width (`unsigned int` / `unsigned long long`); `f32` is single precision. Both
   transpilers agree, ASan-clean, locked by `tests/sized_ints` and — for the u32
   path end-to-end — the NIST vectors in `corelib/test/sha256`, which this type
   rewrote to be correct *by construction* rather than by hand-masking.
2. **FFI-boundary `u8`…`i64`.** Valid in an `extern` signature (by-value param or
   return): the emitted C prototype uses the real fixed-width type so a call
   matches the C ABI instead of funneling through `long`. `tests/ffi` case (9).

**Remaining — small and demand-gated, not blocking:**
- *corelib crypto cleanup — DONE.* `corelib/md5` and `corelib/hash` (crc32, djb2,
  sdbm, fnv1a) now run their word math in `u32` (native wrap, `~`, `>> <<`)
  instead of `% 4294967296` / `4294967295 - x`; output is byte-identical to the
  goldens. `corelib/sha256` was already migrated.
- *Parity gap — FIXED.* tychoc accepted `u32 == <int literal>` and
  `f32 </== <numeric literal>` (it adapts literals at `src/tychoc.c:4374`) while
  tychoc0's `==`/`!=` and ordering branches omitted that adaptation. Root cause:
  the adaptation predicate was duplicated across three branches and drifted;
  `compiler/tychoc0.ty` now computes it once (`num_lit_ok`) shared by all three.
  `fuzz/run_typeparity.py` gained u32/u64/f32 to the exhaustive matrix (it had
  only covered int/float/char/string/bool, which is why the gap stayed latent) —
  now 4608/4608 cases agree, and `make fixpoint` stays byte-identical.
- *Literal suffixes* (`123u32`) — **YAGNI**; typed bindings (`x: u32 = 123`)
  already cover the need.
- *Signed/small int family first-class* (`u8`/`i8`/`i16`/`i32`/`i64` outside an
  `extern`) — **YAGNI**; the FFI boundary covers the correctness motive and no
  in-language program has needed them. Add on first real demand.

### 1.2 `const` bindings and compile-time constants — **shipped**
`const NAME = <literal>` works at module top-level AND inside function bodies, for
int/float/string/bool/char literals. A const is an immutable named literal folded
at each use site (no runtime object — mirrors the nullary-enum-variant fold in
both compilers, so emitted C stays byte-identical). Reassignment and non-literal
RHS are compile errors; forward reference and shadowing work. Locked by
`tests/const_toplevel` + `tests/const_local` goldens and three
`tests/reject/const_*` fixtures (both compilers).

Negative-literal consts (`const MIN = -100`, `const T = -3.14`) are supported —
both compilers fold `-<numeric literal>` into a single negative literal at
const-parse time (locked by `tests/const_negative`).

Const expressions are folded at compile time by a shared `const_fold` in both
compilers: integer arithmetic/bitwise/unary (`const MB = 1024 * 1024`,
`const MASK = 1 << 8`, `const N = ~0`) and backward references to earlier
top-level consts (`const MB = KB * 1024`). Locked by `tests/const_expr` +
`tests/reject/const_expr_{divzero,float,localref}` (both compilers). This is the
prerequisite folder for const generics (1.6).

**Deferred by design:** float arithmetic in a const RHS stays a compile error
(only negative float literals fold — computing e.g. `1.0/2.0` byte-identically
across tychoc's numeric-`ival` literals and tychoc0's source-string literals is a
formatting-parity risk, low value); and a *local* const cannot reference another
const (the self-hosted compiler has no sibling-const table at local-parse time) —
both are symmetric limitations rejected by both compilers.

### 1.3 Compiler diagnostics — **substantially shipped**
The humane-error asks are all implemented in tychoc (the reference compiler):
`<file>:<line>: error:` with a source-line snippet, a `^` caret on parse errors,
"did you mean 'X'?" for unknown variable/type/assignment/procedure, type
mismatch showing both sides (`arithmetic requires two ints or two floats (got
int, string)`, `argument 1 of 'f' is string, expected int`, `declared type int
but value is string`), the failed `where`-predicate by name (`'add' instantiated
with T = string, which does not satisfy numeric(T)`), and arg-count/field/index
errors. Locked as goldens in `tests/diag/*.err` (tychoc only — tychoc0's
bootstrap diagnostics are deliberately simpler).

**Did-you-mean extended to struct fields — shipped.** `p.yy` on a `Point{x,y}`
now emits `struct Point has no field 'yy'; did you mean 'y'?`, reusing the existing
Levenshtein `suggest_*`/`dym_pick` machinery (`suggest_field`, `src/tychoc.c:850`;
wired at the field-miss site `:4041`). Only offers a suggestion within the same
edit-distance threshold as the other did-you-means, so a genuinely-unrelated name
(`p.zzzzzz`) still gets the plain message. tychoc only; locked by
`tests/diag/dym_field.err`.

**Remaining — minor, demand-gated:** carets on *semantic* (not just parse) errors
[needs a column on the offending AST node]; a fall-off-the-end "not all paths
return" lint (currently a `-> int` body with no return silently yields 0 —
defined, not UB; the lint would touch both compilers + reject-parity). None blocking.
**Effort:** the residual is small. **Priority: low** (the compounding wins landed).

### 1.4 corelib gaps
CONTRIBUTING lists "corelib gaps" as useful work. Suggested method: pick a target
task list (a small HTTP service, a log parser, a build tool) and build it against
corelib; every reach for something missing is a ticket. Likely gaps to audit
against the 25 shipped packages: TCP/UDP sockets (only `http` today), TLS,
compression (gzip/zlib), buffered I/O, a bignum/decimal type, richer `datetime`
(timezones), process/subprocess, environment/args helpers. **Priority: medium**,
demand-driven — don't build ahead of a real program that needs it.

### 1.5 Tooling maturity
- **LSP completeness** — hover-types, go-to-def, find-refs, rename, completion
  are all **shipped** (`tools/lsp.ty`, compiler-backed via `tychoc --symbols`).
  Follow-ups: rename/references now reach identifiers inside f-string holes
  (**shipped** — `find_occurrences` descends `{...}`); cross-package completion +
  hover on imported members (`strings.trim`) also **shipped** — the LSP resolves
  `import "core:X"` by running `--symbols` on the file in its real directory
  (package-aware; needs `TYCHO_CORELIB` in the server env). Still open —
  semanticTokens (additive, demand-gated; signatureHelp + workspace-symbol shipped — below), and
  package-aware *diagnostics* (today the buffer compiles single-file, so a
  package file's diagnostics are empty rather than wrong — a safe gap).
  **signatureHelp — shipped:** typing inside a call shows the callee's signature
  with the active parameter highlighted. Resolves a local call via the symbol
  index (`sym_describe`) and a `pkg.member` call via the project index
  (`pkg_fn_sig`); the parameter list is sliced depth-aware from the signature
  string so a tuple-typed param (`t: (int, int)`) counts as one. `call_sig_query`
  back-scans the current line for the innermost unclosed `(` and the active-arg
  index (`tools/lsp.ty`). Known limit: cursor inside a paren/tuple *literal* arg
  finds that group's `(` (no callee) → no help. Gated by the `sighelp` assertion
  in `scripts/tools_check.sh`.
  **workspace/symbol — shipped:** `Cmd+T` fuzzy symbol search over *all* open
  buffers (top-level fn/struct/enum), case-insensitive substring on the name,
  empty query returns everything; each result's location carries its own buffer
  uri. Reuses documentSymbol's SymbolInformation emit across the parallel
  uris/syms arrays (`handle_workspace_symbol`, `tools/lsp.ty`). Gated by the
  `wsym` assertion in `scripts/tools_check.sh`. Still open — semanticTokens
  (editors already highlight via TextMate grammars — low marginal value).
- **Debugging story** — **shipped.** `tychoc -g` emits `#line N "src.ty"`
  directives (single-file builds) and compiles with `-O0 -g`, so `gdb`/`lldb`
  step the `.ty` source via DWARF. Default builds emit no `#line` (byte-identical
  fixpoint/corelib preserved); tychoc-only, opt-in. Package builds are skipped
  (merged files lose per-node identity) — a documented limit. See
  [docs/debugging.md](docs/debugging.md); locked by the `line-info` check in
  `scripts/tools_check.sh`.
- **Editor reach** beyond VS Code/Zed if anyone asks (Neovim via the LSP is
  near-free). Still open, demand-gated.

**Priority: medium.**

### 1.6 Const generics (array-length parameters)
Depends on 1.2. Lets `[N]T` fixed-size arrays and generic code over a size be
expressed. Fits monomorphization exactly (each `N` is a binding, like each type
`$T`). Only pursue if a real need appears — it's the kind of feature that's easy to
add speculatively and then carry forever. **Priority: low, YAGNI-gated.**

### 1.7 Early `close(h)` on typed handles — **shipped**
Handles auto-free at scope exit; `close(h)` runs the destructor early and NULLs the
handle, and the scope-exit free is null-guarded so the C `free_fn` runs exactly once
even though both paths reach it (`src/tychoc.c:4310`/`:7533`, `compiler/tychoc0.ty:5214`/`:6825`).
A *use* after close passes NULL to C rather than a dangling pointer. Both compilers;
locked by the `tests/ffi` `use_res_close` case (freed exactly once, ASan-clean) and
`tests/reject/close_handle_nonvar`. See `docs/internals/typed-handles-design.md:71`.

---

## Tier 2 — Worth reopening (the ask items; decision was defensible, not airtight)

### 2.1 The ternary — reframed as **expression-valued `if`/`match`** — **shipped**
`if` and `match` are now value-producing in **tail position**: the whole RHS of a `:=`, a typed
`x : T =`, a plain `x =` / place assignment, or a `return`. Each branch/arm is a single
expression; a value `if` requires an `else`; a value `match` stays exhaustive; all branches must
unify to one type (which the binding infers). It desugars to the exact declare-then-assign-in-
each-arm C the docs already showed by hand — the non-decl positions rewrite each tail to an
`S_RETURN`/`S_ASSIGN`/place-set at parse time (reusing resolve + codegen wholesale); only `:=`
carries the control node and infers the type by unifying tails. No new arena mechanism (the
value lands in the destination arena exactly like a return). Both compilers, byte-identical
fixpoint; locked by `tests/if_expr` + `tests/match_expr` goldens and four
`tests/reject/{if_expr_type_mismatch,if_expr_no_else,match_expr_nonexhaustive,if_expr_multistatement}`
fixtures (both compilers reject). The move-on-last-use read counter, push-fusion, and
bounds-elision walkers were taught to descend into the value-decl's control node (a latent
correctness gap for a variable read only inside a value tail).

**Deferred by design** (ponytail follow-ups, marked in-code): multi-statement value branches;
a diverging arm (`None: return -1` instead of yielding — `block_ends_in_return` is the hook);
and use as a nested sub-expression (`1 + if …`) — the tail-position grammar is what keeps the
indentation-block-as-value unambiguous. The statement form covers all three today.

The rest of this section is the original design argument, kept for the record.

You said the rejection wasn't on sound basis. Here's the honest read, both ways.

**The rejection *is* internally consistent.** Tycho is statement-oriented on
purpose: `match` is a statement, not an expression (`enums-options.md:98`); `if`
likewise. In that world, banning `?:` is *uniformity*, not arbitrariness — no
control flow anywhere produces a value, so the ternary would be the lone
exception. That's a sound basis. It's just an unstated one; STATUS lists the
conclusion (`ternary operator`) without the premise (statement-orientation).

**Where it's weak:** the premise itself is the real decision, and it's the thing
worth reopening — not the ternary. The ternary is a symptom. The question is:

> Should `if` and `match` be **expressions** (Rust/Kotlin style — a block whose
> value is its last expression / its taken arm)?

If **yes**, then `x := if c { a } else { b }` and `x := match v { ... }` subsume
the ternary entirely, are strictly more powerful (n-way, pattern-matching,
multi-statement arms), and *remove* the current boilerplate the docs themselves
show — `enums-options.md:98` literally instructs you to declare-then-assign-in-
each-arm because there's no expression form. That boilerplate is the actual cost
the user feels; `?:` only patches the 2-way case of it.

**Thesis fit: clean.** An expression that yields a value is just an rvalue — it
lands in the destination's arena exactly like a function return already does
(§4a return-slot move). No aliasing, no new escape path. This does *not* fight
value semantics.

**Recommendation:** don't add C's `?:`. If you want the ergonomics, add
**block-valued `if`/`match`** — more uniform, subsumes the ternary, and it's the
sound version of the same wish. If you *don't* want expression-oriented control
flow as a language identity, then keep both rejected, but write the premise into
STATUS so "no ternary" reads as a consequence, not a whim.
**Priority: medium-high** *if* you want the ergonomics; it's a genuine identity call.

### 2.2 Expanding generics — the wall is the constraint set, not the shapes
**State today:** generics are already broad — monomorphized generic
structs/enums/fns, recursive + nested, through containers/channels/Tasks
(STATUS "Shipped"). The constraint mechanism is a **closed, transpiler-known
predicate set**: `numeric(T)`, `comparable(T)`, `has_str(T)` (`generics.md:29–34`),
deliberately not user-extensible (that's the anti-traits stance).

**So "expand generics" means one of two very different things:**
- **(a) More predicates / more reach** — add predicates (`hashable(T)`,
  `defaultable(T)`, `ordered(T)`), variadic generics, generic UFCS method
  resolution, const generics (1.6). All fit the closed-set model. Incremental,
  thesis-safe. **This is the real generics roadmap.** **Priority: medium**,
  demand-driven.
- **(b) User-defined constraints** — letting *users* name new predicates over
  types. That is traits/typeclasses by another name, and it's a hard STATUS
  non-goal. See 3.2. Don't drift into it by accident while doing (a).

The honest framing: generics aren't under-powered, they're *deliberately
un-abstracted*. Growth happens by widening the built-in predicate set, not by
handing the predicate mechanism to users.

### 2.3 FFI improvements — most of the win is 1.1, the rest hits the non-goals [partially reopens]
**State today:** `extern` over scalars/string/bytes/opaque `ptr`/typed handles,
nullable `Option(string)` returns, `mut` out-params (STATUS "FFI"). Declared
non-goals: variadics, callbacks-into-Tycho, struct-by-value, auto-bindgen.

**What improves FFI without touching any non-goal:**
- **Sized integer types (1.1)** — the single biggest FFI correctness win, full
  stop. Do this first; it's already Tier 1.
- **Richer handle/type mapping** — arrays/slices across the boundary, more
  faithful `const`-correctness, `f32`.
- **Linking/build ergonomics** and clearer FFI docs (CONTRIBUTING notes
  "read-once-borrow docs" are thin).

**What would help but reopens a non-goal:**
- **Callbacks into Tycho** [reopens]. This is the real FFI ceiling — `qsort`,
  event loops, and most callback-driven C APIs are unreachable. But it's a non-goal
  for a *thesis* reason, not a whim: a C callback fires on a thread/stack Tycho
  didn't create, with no arena in scope — it breaks the "private arena per call,
  deep-copy at the boundary" invariant the concurrency safety rests on
  (thesis §7). Reopening it means designing an arena-entry shim for foreign call-ins.
  Real design work, not a small feature. **Verdict: hard, and it's load-bearing —
  only if a concrete need justifies the design cost.**
- **struct-by-value / auto-bindgen** — ergonomic, not thesis-threatening, but you
  declared them out to keep FFI's surface small. Reopen only if FFI becomes a
  headline use case (it reads today as a supporting feature, not a pillar).

### 2.4 User-defined projections / yielding subscripts
Not on your original ask list, but it belongs at the *top* of any real roadmap:
it's the **one** feature-work direction CONTRIBUTING explicitly blesses and RFCs
(`limited-references-spike.md`) — zero-copy views that generalize the built-in
`&m[k]`, the single limited-reference idea that fits the arena + deep-copy-boundary
model. If you build one new language feature this cycle, this is the sanctioned
one. **Priority: high among language features**, but scope it to a real need
(CONTRIBUTING marks it low-priority-until-demanded).

---

## Tier 3 — Fights the thesis (verdict: don't build; here's why, so it stays decided)

Listed so the roadmap is complete and the "no" is on record with its reason, per
the project's own honest-limits culture (thesis §5).

| Item | Verdict | Reason |
|---|---|---|
| **Shared-mutable / graph references** (observer graphs, ref-cyclic structs, stored aliases) | **No — defining boundary** | This *is* what value semantics forbids by construction (thesis §5). `mut` already covers the reachable part (call-scoped exclusive borrow). Storable aliasing would make it a different language. Graphs → index pools. |
| **Traits / typeclasses** (user-defined constraints) | **No — but note it's the ceiling on 2.2b** | Removes the closed-predicate-set simplicity. The whole generics-constraint story is built on *not* having this. |
| **GC / refcounting / COW** | **No — thesis-defining** | The entire claim is "no GC, arenas made implicit by value semantics." Adding any of these dissolves the point. |
| **Hindley-Milner / global inference** | **No** | "Every expression typed at its own site, no unification" is a stated design property (`types.md:112`). Bidirectional local inference is the deliberate choice. |
| **Package manager** | **No — decided** | Odin-style local packages ship; a *manager* (registry, versioning, network fetch) is scope the PoC doesn't want. |

---

## Cross-cutting axes (not language features)

- **More codegen optimizations.** The two thesis-carrying ones shipped (return-slot
  move §4a, in-place append §4b). Next candidates that fit: inlining hints,
  small-value stack promotion, SIMD in hot corelib paths. All measurable against
  the existing `bench-guard`. **Priority: opportunistic, evidence-gated** — the
  `bench/*/RESULTS.md` discipline is already the right filter.
- **Alternate backends** (WASM, native/LLVM). Interesting reach, but C-as-target is
  part of the PoC's leverage (portability, DWARF, ASan verification). **Verdict:
  out of scope unless the PoC's goal changes** from "prove the model" to "ship a
  toolchain."
- **Verification surface** is already exceptional (16 gates, differential fuzzing,
  byte-identical self-host). Marginal add: property-based tests in `corelib/test`.
  Low priority — this is the project's strongest area, not its weakest.

---

## If you do three things this cycle

1. **Sized/unsigned integer types (1.1)** — unblocks FFI correctness *and* corelib
   crypto in one stroke; pure thesis-fit.
2. **Compiler diagnostics (1.3)** — cheapest compounding ergonomics win.
3. **Decide the expression-orientation question (2.1)** — the *real* item hiding
   behind "ternary." Block-valued `if`/`match` if yes; write the premise into
   STATUS if no. Either way, resolve it as a principle, not a syntax skirmish.
