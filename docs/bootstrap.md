# Bootstrapping Hier: a self-hosting roadmap

## North star

Rewrite the Hier compiler in Hier. A self-hosting compiler is the definitive
dogfood for Hier's thesis ("good for compilers and compiler passes"): the
compiler is itself a non-trivial compiler, so the value-semantics +
implicit-arena model gets validated on exactly the workload it claims — on a
real, large program, not a micro-benchmark.

This is a multi-stage campaign, and the value is **front-loaded**. By Stage 1
we have the first real compiler written in Hier and a concrete list of the
language's expressiveness gaps. By Stage 3 we have a feature-complete
alternative front-end. Stage 4 is the dramatic fixpoint where Hier compiles
itself and the C compiler can be retired. Stopping at any stage ≥ 1 is still a
standalone win.

## What we are and aren't rewriting

- The current compiler is **`src/hierc.c` — 3,814 lines of C** (lexer, parser,
  type checker, C code generator).
- The **677-line runtime (`runtime/hier_rt.c`) is shared, not rewritten.**
  Generated C embeds the runtime verbatim; the Hier compiler emits the same
  embed. Self-hosting is about the ~3,800 lines of compiler *logic*.
- The AST is already expressible: Hier's recursive `enum` is exactly what the C
  compiler models with `ExprKind`/`StmtKind` tagged unions. The core data model
  is native.

## Guiding principles

1. **Differential validation is the spine.** The existing `tests/*.hi` + golden
   `*.out` suite is the oracle. A Hier-written compiler is "correct for program
   P" iff `cc(hier_hierc(P))` produces P's golden output. We never trust the new
   compiler on its own — every stage is gated by the existing
   differential + sanitizer harness.
2. **Source-to-source, stdin → stdout.** `hier_hierc < src.hi > out.c`. This
   sidesteps the two hardest missing pieces — file I/O and command-line args —
   entirely. Stage 0 adds the one builtin needed to read all of stdin.
3. **Every stage ships.** Each stage is independently valuable and committed.
   Stage 1 (a subset compiler) is publishable on its own.
4. **Freeze the language during the push.** The C and Hier compilers must agree.
   Pin the Hier compiler to a language snapshot; don't grow the *language*
   mid-bootstrap except for what the bootstrap itself requires.
5. **Explicit state, no globals.** Hier has no globals; the C compiler's global
   tables (cvar stack, type tables, counters, parser position) become an
   explicit `Ctx` struct threaded by `inout`/return. This is the single biggest
   structural difference and a known cost — design `Ctx` early.

## Validation strategy

- **Program-output oracle (primary):** for each test program P,
  `diff <(cc(hier_hierc(P)) | run) tests/P.out`. Reuses the entire golden suite.
- **Byte-identical emitted C (aspirational):** if the Hier compiler emits
  deterministic temp names / formatting matching the C compiler, emitted C can
  be diffed directly — a stronger oracle. Aim for it, don't depend on it
  (equivalent-but-different C is acceptable).
- **Sanitizers come for free:** the Hier compiler, built by the C compiler, is
  just another Hier program — it runs under ASan/UBSan on every test input, so
  its own memory model is continuously checked.
- **Self-build fixpoint (Stage 4):** the gold standard — see Stage 4.

## Language maturity gaps (grounded audit)

**Have** (verified): recursive enums (AST), N-way `match`, arrays + `push`,
string-keyed maps (symbol tables), structs, tuples, `Option`/`Result` +
`or_return` (errors), mutual recursion / forward references (verified —
`src/hierc.c` registers all proc signatures up front), string ops (`s[i] → int`
i.e. ord, `substr`, `split`, `find`, `+`, `len`, in-place append).

**Hard blockers (Stage 0):**

- **`read_all() -> string`** (slurp stdin). `input()` reads one line and
  *cannot distinguish EOF from a blank line* (both return `""`), so it cannot
  read multi-line source. Required to read the source at all.
- **`chr(int) -> string`** (byte → 1-char string). Codegen must emit arbitrary
  bytes / escapes; `s[i]` gives ord but there is no inverse.
- **Error termination.** No `exit`/`die` builtin. Either thread
  `Result(_, error)` everywhere (supported, verbose) or add a `die(msg)`
  builtin — decide in Stage 0.

**Soft / workarounds:**

- ~~No `while`~~ — **a `while` loop already exists**, spelled `for <cond>:`
  (the condition-form `for`; `for i < n:` loops while the condition holds). The
  earlier audit was wrong: the construct was there, just not under the `while`
  keyword. Verified and locked by `tests/while_loop.hi` (incl. a string scan,
  the lexer pattern). No new feature needed.
- **`/` is float division**; no int `//` or `%`. Compilers need little
  arithmetic; add if a pass requires it.
- **`str(bool)` unsupported** (`str` takes int/float only). Trivial add if
  needed.

**Scaling concerns** (a self-hosting compiler is a *large* program). Fixed-size
tables in the C compiler cap what it can compile and the Hier compiler may
approach them: `g_enums[64]`, `g_structs[128]`, `g_cv[1024]` (live vars per
function), `indent_stack[256]`, arr/opt/res type tables `[256]`. These are
one-line constants — raise them in Stage 4 when self-compiling. Watch
string-builder throughput on the full self-compile (the in-place append
optimization should cover it).

## The stages

### Stage 0 — Enable the language to express a compiler

**Objective:** add the minimum so a Hier program can be a source-to-source
compiler.

**Scope:**
- Add `read_all() -> string` (slurp all of stdin): runtime + builtin + checker +
  test/golden.
- Add `chr(int) -> string`: runtime + builtin + test/golden.
- Decide and document the error convention (`Result`-threading vs a `die(msg)`
  builtin).
- Decide whether to add `while` now (cheap — node exists) or defer.
- Verify with tests: deep recursion on large inputs; `s[i]` ord ↔ `chr`
  round-trip.

**Exit criteria:** a Hier program round-trips a multi-line file through
stdin → stdout byte-identically; ord/`chr` round-trip tested; every addition has
a golden; `make test` + `make bench` green.

**Size:** small — a few tiny, well-guarded builtins in the established pattern.
Pure fundamentals. **Risk: low.**

### Stage 1 — Subset compiler in Hier (the milestone)

**Objective:** the first real compiler written in Hier — compiles a documented
*subset* of Hier to C.

**Subset (proposed):** `fn main()` and `fn f(args) -> T`; `int`/`bool`; `:=`
decls and `=` assigns; `+ - *` and comparisons; `if`/`elif`/`else`; `return`;
`print`; `str`; integer literals; calls. (No strings/arrays/structs/enums yet.)

**Scope:** `compiler/hierc0.hi` — lexer over the `read_all`'d source,
recursive-descent parser → AST enums, a minimal checker, C codegen emitting the
shared-runtime embed. Source → source via stdin/stdout.

**Validation:** a handful of subset programs; `cc(hierc0(P)) | run == golden`.
Wire a `make bootstrap-test` target.

**Exit criteria:** every subset test program compiles via `hierc0` to C that
builds and produces the golden output; committed with its harness.

**Size:** several hundred lines of Hier. **This is "first compiler in Hier" —
the publishable win.** **Risk: medium** — it surfaces the real ergonomics of
writing a stateful program in Hier (the `Ctx` threading). That discovery *is*
the value.

### Stage 2 — Grow the subset toward real Hier

**Objective:** cover enough of the language to compile most existing programs.

**Scope (incremental, each its own commit):** function params & all scalar types
→ strings & string builtins → arrays + `push` + `for`-range → structs → enums +
`match` → `Option`/`Result` + `or_return` → tuples → maps. Build out the `Ctx`
(type tables, cvar tracking) as features demand it.

**Validation:** run the existing `tests/*.hi` and `examples/*.hi` through the
Hier compiler; track the pass-rate against the goldens, climbing toward 100% of
the non-self programs.

**Exit criteria:** `hier_hierc` compiles every current `examples/*.hi` with
golden-identical output.

**Size:** large — the bulk of compiler logic; multi-session. **Risk: medium** —
mostly mechanical once Stage 1's patterns are set; caps/perf may bite on bigger
inputs.

### Stage 3 — Feature-complete front-end

**Objective:** `hier_hierc` handles *every* feature the C compiler does —
including the hard codegen: the arena placement, the FBIP reuse optimizations,
projections, slices, newtypes, `inout`.

**Scope:** close to 100% parity, including the memory-model codegen (arena
threading, return-slot, move-on-last-use, match-arm borrow, construction moves,
transient placement), exhaustiveness checking, and error diagnostics. The Hier
compiler must emit the *same quality* C, or the eventual self-compile regresses
performance.

**Validation:** the **entire** `make test` suite passes through `hier_hierc`
with byte-identical program output; ideally byte-identical emitted C vs `hierc`
on a deterministic subset.

**Exit criteria:** 100% of `tests/*.hi` + `examples/*.hi` golden-identical
through the Hier compiler. It is now a complete, drop-in alternative front-end
(but does not yet compile itself).

**Size:** large; the optimization codegen is the hardest part. **Risk: high** —
the memory-model codegen is the subtlest code in the compiler; reproducing it
faithfully in Hier is the real test of both the language and our understanding.

### Stage 4 — Fixpoint bootstrap (self-hosting)

**Objective:** Hier compiles its own compiler; retire the C compiler.

**The 3-stage fixpoint:**
1. `hierc_c` = the C compiler (cc-built). **A** = `hierc_c` compiling
   `hier_hierc.hi` → executable A.
2. **B** = A compiling `hier_hierc.hi` → executable B.
3. **C** = B compiling `hier_hierc.hi` → executable C.
4. **Assert B and C are byte-identical** — the fixpoint. A was built by the C
   compiler; B and C were both built by a Hier-built compiler, so B ≡ C proves
   the Hier compiler reproduces itself exactly.

**Prerequisites:** Stage 3 complete; raise the C compiler's fixed-size caps so
the large `hier_hierc.hi` compiles; ensure self-compile time/memory is
acceptable (the prong-B arena findings — compact nodes, block-retaining
`arena_reset` — may finally matter here, on a real workload that demands them).

**Exit criteria:** `make bootstrap` performs the 3-stage build and asserts
B ≡ C; the C compiler is archived (kept for reference/diffing, off the critical
path).

**Size:** integration + scaling fixes on top of Stage 3. **Risk: high, but
mostly discovered by this point** — Stage 3 already exercised every feature;
Stage 4 is making the compiler swallow its own large source and reach the
fixpoint.

## Risks & off-ramps

- **Effort:** a multi-session campaign. *Off-ramp:* Stage 1 alone proves the
  thesis and is publishable; every stage ≥ 1 is a standalone win. Reaching
  Stage 4 is not required to have succeeded.
- **State-threading ergonomics:** if threading `Ctx` everywhere proves painful,
  that is *itself* a finding (the language may want a lightweight
  mutable-context affordance) — surface it, don't paper over it.
- **Scale/perf on self-compile:** caps and arena throughput. *Off-ramp:* raise
  caps (trivial); accept a slower self-compile; let real need drive the perf
  work — which is the right time to do it.
- **C/Hier divergence:** language changes during the push break parity.
  *Mitigation:* freeze the language; the Hier compiler targets a pinned
  snapshot.

## Status

- [x] **Stage 0** — enabling builtins: `read_all` + `chr` (commit 96444f6),
      `die` error convention (135b4ff); `while` already exists as `for <cond>:`
      (tests/while_loop.hi). The language can now express a source-to-source
      compiler.
- [x] **Stage 1** — subset compiler `compiler/hierc0.hi`. Lexer
      (incl. INDENT/DEDENT) + recursive-descent parser → AST enums + C codegen,
      validated differentially by `make bootstrap`. Compiles `fn main():` whose
      indented body has `:=`/`=`/`print(str(EXPR))` and `if`/`elif`/`else` over
      integer arithmetic (`+ - *`, parens, vars) and comparisons (`== < >`) —
      output matches the C compiler. Dogfooding already paid off: it surfaced a
      real C-compiler bug (recursive enum with an array-of-itself payload emitted
      its type after the array typedef that used it — fixed, commit d8b3934,
      `tests/recursive_enum_array.hi`). User functions, typed int params, calls,
      and `return` now work too (commit 429d6d1) — `compiler/tests/functions.hi`
      compiles a recursive fib and nested calls, matching the C compiler.
      Strings landed too (commit 69280d6) via context-directed parsing —
      `print(...)` parses a string expression (literals + `str(EXPR)` joined by
      `+`), so no type inference is needed; hierc0 emits a tiny self-contained
      string runtime (sc/i2s). And the `for COND:` while loop (commit d8cad4a).
      **Stage 1 is essentially done**: hierc0 (~400 lines of Hier) compiles a
      genuinely usable subset — functions, recursion, if/elif/else, while loops,
      integer arithmetic + comparisons, and string output — with six fixtures
      (arith, vars, control, functions, strings, loops) passing `make bootstrap`
      differentially against the C compiler. Deferred to Stage 2: the counting
      `for i in range()` form, string variables, and a real type system.
- [ ] **Stage 2** — grow to cover `examples/*.hi`. In progress:
      - **2A** (commit eb2fdef): the counting `for i in range(...)` form.
      - **2B**: string-typed variables, parameters, and return types — the
        leap from untyped (`long` everything) to a real int/str distinction.
        hierc0 now threads a type environment (two parallel `names`/`types`
        arrays + a `[Sig]` table of function return types) and emits
        **type-directed** C: `str` → `char*`/`sc(...)`, `int` → `long`/`+`.
        The syntactic int-vs-string codegen split (`gen_iexpr`/`gen_sexpr`) is
        gone — one `gen_expr`+`type_of` infers context. Block scoping is via
        by-value env arrays (inner decls don't leak). Unary minus added
        (`-E` → `(0 - E)`). Fixtures `strvars.hi`/`strfn.hi` pass `make
        bootstrap`; `examples/accumulate.hi` now matches the C compiler
        end-to-end. State-threading ergonomics finding: `inout [T]` works, but
        a borrowed (by-value) array param must be **copied** before it can be
        passed as `inout` — the compiler enforces this ("copy it first"), so
        `gen_block` copies its env arrays locally. `input()` deferred
        (stdin-dependent, untestable in the differential harness).
      - Next gaps for `examples/*.hi`: bare call-statements (`countdown(5)` —
        `demo.hi`), then structs, arrays+`push`, enums+`match`.
- [ ] **Stage 3** — feature-complete front-end (all `tests/*.hi`)
- [ ] **Stage 4** — fixpoint bootstrap (B ≡ C), retire the C compiler
