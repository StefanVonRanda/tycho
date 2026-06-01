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
- [x] **Stage 2** — grow to cover `examples/*.hi`. **Complete: all 16
      `examples/*.hi` compile through hierc0 with golden-identical output**
      (15 verified in `make bootstrap`; `hello.hi` verified manually with piped
      stdin — it can't be a harness fixture because the runner gives binaries no
      stdin and `input()` would block). Increments:
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
      - **2C**: bare call-statements (`countdown(5)` — a call invoked for its
        effect, no assignment). New `SExpr(Expr)` stmt; parsed when a leading
        ident is followed by `(`, emitted as `EXPR;`. Fixture `calls.hi`
        passes; `examples/demo.hi` now matches the C compiler end-to-end.
      - **2D**: structs — `struct Name:` decls (parsed at top level into a
        `Program` of structs+funcs), positional construction `Point(1, 2)`
        (emitted as a C compound literal `(Point){1, 2}`), nested field access
        `r.lo.x` (new `.`/`TDot` token, `EField` parsed as a postfix chain),
        field assignment `r.lo.x = 100` (new `SFieldAssign`), and value-copy
        assignment `d := a` (C struct copy). The type system grew beyond
        int/str: a type is now `"int"`/`"str"`/a struct name, and the read-only
        `sigs` thread became a proper `Ctx { sigs, structs }` (the struct table
        types field accesses and distinguishes construction from calls).
        Fixture `structs.hi` passes; `examples/structs.hi` matches the C
        compiler end-to-end.
      - **2E**: `[int]` arrays — literals `[1, 2, 3]`, empty typed `[]int`,
        `push`/`len` builtins, index read/write `xs[i]`, `[int]` as a
        var/param/return type, and value-semantic copy. New `[`/`]` tokens,
        `EIndex`/`EArrLit`/`EArrEmpty` exprs, `parse_type` handles `[T]`.
        Codegen targets a small runtime struct `IntArr {data,len,cap}` with
        `iarr_new`/`iarr_from`/`iarr_copy`/`iarr_push` emitted in the preamble;
        `[1,2,3]` → `iarr_from((long[]){1,2,3}, 3)`, `xs[i]` → `xs.data[i]`,
        `len(xs)` → `xs.len`, `push(xs,v)` → `iarr_push(&xs, v)`. Value
        semantics: a whole-array binding read FROM a place (`ys := xs`,
        `b := a`) is deep-copied (`gen_rhs`/`is_place` → `iarr_copy`); fresh
        arrays (literals, call results) already own their buffer. Memory is
        leaked, not freed — acceptable for now (the C compiler uses arenas;
        correctness first). Fixture `arrays.hi` passes; `examples/arrays.hi`
        and `examples/array_fns.hi` match the C compiler end-to-end.
      - **2F**: enums + `match` (and the `bool`/`true`/`false`/`and`/`or` that
        `examples/optimize.hi` needs). Recursive `enum` decls with payloads;
        construction `Add(l, r)`; `match subj:` with payload-binding arms;
        structural `==` on enum values. C model: an enum value is a pointer to
        a heap node (`EnumName*`) — a flat tagged struct with one field per
        variant payload (`Variant_i`), recursive payloads as pointers.
        Construction → `mk_Variant(...)`; `match` → a tag-dispatch if-chain
        binding payload fields; `==` → a generated recursive `EnumName_eq`.
        Enums are immutable in Hier (matched, never field-assigned), so
        pointer-sharing is observationally identical to value semantics — no
        deep copy needed. `bool` maps to `long` 0/1; `and`/`or` → `&&`/`||`
        (new precedence layers below comparison). `Ctx` gained an `enums`
        table. Fixture `optimize.hi` (a full constant-folding pass) passes;
        the example matches the C compiler end-to-end.
      - Two language/compiler facts learned and locked in: (1) the C compiler
        resolves type names in source order — no cross-type forward refs — so
        `match` arms are stored as parallel arrays in the `SMatch` payload
        rather than a `MatchArm` struct (which would cycle with `Stmt`); nested
        arrays `[[T]]` and the empty-literal form `[][T]` both work. (2) An
        indented comment as a block's first line emits a stray `NEWLINE` before
        the `INDENT`; block openers now skip blank/comment lines via
        `open_block` (this was a real hierc0 lexer/parser bug, fixed here).
      - **2G**: `inout` parameters (+ the `<= >= !=` comparison operators
        `examples/memo.hi` needs). An `inout T` param is a C pointer; its env
        type is marked with a leading `&` (so the base type still drives all
        type logic). `gen_expr(EVar)` derefs (`(*n)`) when the var is inout, so
        field access, indexing, `push`, and assignment all compose with no
        extra cases (`(*s).sum`, `(*memo).data[n]`, `iarr_push(&(*memo), v)`).
        Call-site `&place` parses to `EAddr` → `&(gen_expr(place))`; since
        `&(*p) == p`, passing an inout param onward (`fib(n-1, &memo)`) Just
        Works. `cty` emits the pointer; `parse_param` reads the `inout`
        modifier. Fixtures `inout.hi` (inout int + pure-value struct) and
        `memo.hi` (shared `inout [int]`, recursive) pass; both examples match
        the C compiler end-to-end.
      - **2H**: generic array element types. The single hardcoded `IntArr`
        runtime is replaced by **monomorphized** `Arr_<T>` families — one
        `new`/`from`/`copy`/`push` set per element type the program uses, with
        a `CTY* data` buffer (deep-copy on assign = value semantics). Element
        types are collected from annotations (param/return/field/payload) plus
        a seed of `int`/`str` (covers every array literal seen in practice);
        families are emitted after enum forward typedefs and before struct
        bodies that embed them. `cty`/`push`/index/literal/`gen_rhs` all key off
        the element type. Element types must be word-sized (int/bool/str/enum-
        pointer); `[struct-by-value]` is still pending. Also added bare
        `return` (void). Fixture `strarrays.hi` (= `collect.hi`: `inout
        [string]`, string push/index, recursive accumulation) passes;
        `collect.hi` matches the C compiler end-to-end. (GCC-14 note: array
        `_from` takes a non-const element pointer — `char**`→`const char**` is
        a hard error under the default `-Wincompatible-pointer-types`.)
      - **2I**: deep struct copy for heap-bearing fields. A per-struct
        `StructName_copy` is generated for any struct that owns heap memory (a
        string, array, or nested heap struct field): it shallow-copies scalars
        then dups each heap field (`sc(x,"")` for strings, `Arr_T_copy` for
        arrays, the nested struct's `_copy` recursively). `gen_rhs` applies it
        when a heap struct is bound from a place (`snap := ctx`, `u := t`),
        mirroring the array rule; pure-scalar structs (Point/Stats) keep C's
        shallow copy (already deep for scalars). Notably `context.hi` and
        `records.hi` already *passed* under shallow copy — neither observes the
        difference (strings immutable; `push` updates the owner struct's own
        length). So a dedicated fixture `heapcopy.hi` exercises it directly: an
        index-write through a value-copied struct's array field (`b.items[0] =
        99` leaves `a.items[0] == 1`) plus nested-struct and array-field
        isolation — verified differentially and by quoted output.
      - **2J**: `substr` builtin (`substr(s,a,b)` → a self-contained C helper)
        and string-aware `len` (`len(str)` → `strlen`, `len(arr)` → `.len`).
        Fixture `accumulate_big.hi` passes (the O(n) in-place append is
        invisible — hierc0's `sc`-based concat is O(n^2) but produces identical
        output; 50000-char build completes fine).
      - **2K**: `split(s, sep) -> [string]` builtin. Because it returns an
        `Arr_str`, its runtime (`hi_split`) is emitted *after* the array
        families (not in the preamble); `str` is always in the seed set so
        `Arr_str` is always present. Fixture `words.hi` passes.
      - **2L**: `[string: int]` maps — empty literal `[]string: int`,
        `map_set`/`map_get`/`keys`/`len`. The internal type is `"{str:int}"`
        (distinct from array `"[...]"`); only this one map type is supported.
        The emitted `Map_str_int` runtime is a **faithful byte-for-byte mirror**
        of the C runtime's open-addressing table (`runtime/hier_rt.c`): FNV-1a
        hash, power-of-2 capacity (min 8), linear probing, rehash past 1/2
        load, deep-copy value semantics, `keys()` in bucket order. Matching the
        layout exactly is what makes `keys()` iteration order byte-identical —
        and that required reproducing the compiler's **accumulator rewrite**:
        `x = map_set(x, k, v)` (uniquely-owned map) becomes an in-place
        `map_str_int_put(&x, …)`, not a copy-then-set, because pure-set
        produces a different final bucket layout (verified empirically — the
        order diverged until the rewrite was added). hierc0 applies the rewrite
        on the syntactic self-assignment pattern (assumes unique ownership; no
        example aliases a map). Fixture `wordcount.hi` passes with identical key
        order. (No tombstones — the subset has no map delete.)
      - **2M**: the remaining string builtins + I/O. `find(s, sub) -> int`
        (`strstr`), string indexing `s[i] -> int` (the byte/ord, vs array
        `.data[i]`), lexicographic string comparison (`<`/`<=`/`==`/… on
        strings → `strcmp(a,b) OP 0`), and `input()` / `chr()` faithfully
        mirroring `runtime/hier_rt.c` (`input` reads a line, `""` on EOF; `chr`
        is a one-byte string). Fixture `strops.hi` (= `examples/strings.hi`:
        find/index/substr/ordering) passes; `examples/hello.hi` matches with
        piped stdin (`echo Ada | … → "what is your name: hello Ada"`).
      - **Stage 2 done.** 23 bootstrap fixtures green; every `examples/*.hi`
        is golden-identical through hierc0. hierc0 is now ~700 lines of Hier
        covering: functions/recursion/`inout`, int/bool/string scalars,
        structs (+deep copy), `[T]` arrays (monomorphized), `{str:int}` maps
        (FNV table, byte-identical key order), enums + `match`, the full
        operator set, and string/array/map builtins.
        Language features still unimplemented (no example exercises them yet):
        `Option`/`Result` + `or_return`, tuples, float, `[struct-by-value]`
        arrays, non-`{str:int}` maps, map delete. These move into Stage 3
        (full feature parity over `tests/*.hi`).
- [ ] **Stage 3** — feature-complete front-end (all `tests/*.hi`). In progress;
      baseline at start was 6/29 `tests/*.hi` golden-identical through hierc0
      (the 5 already-covered scalar/control tests plus, after the first fix,
      `recursive_structs`). Increments:
      - **3A**: aggregate type ordering. Arrays-of-structs (`[P]`) and recursive
        structs (`struct Node { kids: [Node] }`) emitted broken C because the
        monomorphized `Arr_T` referenced an element struct not yet declared, and
        structs weren't forward-declared. Fixed by staging the C emission so
        every type is complete before use: (1) forward-declare all struct/enum
        tags; (2) emit array/map **struct bodies** (element types appear only as
        pointers here); (3) enum eq prototypes; (4) user struct bodies (array
        fields now use the complete `Arr_T`); (5) enum bodies; (6) array/map/str
        **functions** (need element bodies complete for `sizeof`/by-value moves);
        (7) ctors/eq; (8) struct copies; (9) protos/funcs. `gen_arr_family` and
        `gen_maplib` were split into type-def vs function halves, and structs now
        emit a named `struct N {…}` body. `recursive_structs.hi` passes (6/29).
        Still pending for `aggregates.hi`: nested arrays `[[int]]` (needs element-
        type name mangling) and whole-array `==`.
      - **3B**: `float` scalar + `[float]` arrays + `/` + `not` + array `==`.
        Lexer gains `D.D` float literals and `/`; `float` → C `double`; floats
        emit verbatim (round-trip); arithmetic `+ - * /` promotes to float when
        either operand is float (int `/` stays integer division). `str(float)`
        → `f2s` mirroring `runtime/hier_rt.c`'s `%.15g`+`.0` format; `to_float`/
        `to_int` → casts. `not X` parses to `(X == 0)` (a precedence layer below
        `and`). Whole-array `==` generates `Arr_T_eq` (element-wise; emitted only
        for elements with a usable equality — int/float/str/enum). Cleared
        `floats`, `float_arrays`, `logic` and one more — 6 → 10/29. Deferred:
        `float_maps` (a second map type, `[string: float]`).
      - **3C**: struct/enum-element arrays + array-field equality (10 → 11/29).
        `collect_elem_types` now seeds every struct and enum name, so a
        literal-only `[Struct]`/`[Enum]` (which no annotation scan would catch)
        still gets its `Arr_T` family. `eq_field` gained an array case
        (`Arr_T_eq`), fixing `recursive_enum_array` (an enum whose payload is
        `[Tree]`). Unused seeded families are harmless dead statics.
      - **3D**: `die()` + `read_all()` builtins (11 → 13/29). `die(msg)` →
        stderr + `exit(1)`; `read_all()` slurps stdin. `die.hi` is a fixture
        (die never fires); `io_builtins.hi` is sweep-only (reads stdin).
      - **3E**: newtypes (`type X = U`) — zero-cost, resolved to the underlying
        type in codegen (`is_newtype`/`resolve_nt`, a `Ctx.newtypes` table);
        `X(v)` is identity. 13 → 14/29. **Surfaced a real C-compiler bug**
        (dogfooding): a function that takes the heap-bearing `Ctx` by value AND
        returns one of its string *parameters* gets an empty string back — a
        transient-arena reuse bug. `resolve_nt` returns `ty + ""` (a fresh copy)
        to dodge it; the underlying bug remains to be fixed in `src/hierc.c`.
        (Functions returning a *field* of the by-value struct are unaffected.)
      - **3F**: Option/Result + `or_return` (14 → 19/29). Designed around a
        **uniform boxed** representation — `HOption {tag; void* val}` /
        `HResult {tag; void* ok, err}`, single C types, not monomorphized. A
        `Some(x)`/`Ok(v)`/`Err(e)` boxes its payload (`hbox(sizeof(T),(T[]){x})`
        with `T = typeof(arg)`), so **construction never needs context typing**;
        `None` is type-free (`hnone()`). The only T-dependent site is the match
        arm, where T comes from the scrutinee's static type. `match` branches on
        `is_option`/`is_result` to a value-type dispatch reading the box
        (`*(T*)(scrut.val)`). Borrow-vs-copy falls out free: a `Some(xs)` binding
        copies the value-struct (own len, shared buffer), so a `push` into it
        can't reach the scrutinee. Added the `name : Type = expr` typed decl and
        the `or_return` postfix (unwrap Ok / propagate Err via the uniform
        HResult). Also made match-arm payload parens optional (`None:`). Cleared
        `options`, `results`, `or_return`, `optres_borrow` (+ a no-payload enum
        test). Deferred: `option_fields` (needs struct `==`), `option_arrays`
        (needs `[Option(...)]` element-name mangling).
      - **3G**: structural struct `==` (19 → 20/29). A `StructName_eq` is
        generated per struct (AND of per-field equality), with prototypes up
        front for mutual recursion; `eq_field` gained struct / Option / Result
        cases (recursing — e.g. an `Option(Point)` field compares tags then, if
        `Some`, `Point_eq` on the boxed payloads). `Arr_T_eq` now also emits for
        struct elements. Cleared `option_fields` (struct `==` recursing through
        Option fields). `aggregates` is now blocked only by nested arrays.
      - **3H**: composite array element types (20 → 23/29). Array family names
        are now `Arr_<mangle(elem)>` where `mangle` turns any element type into a
        valid C identifier (`[int]`→`arr_int`, `Option(int)`→`opt_int`,
        `Result(T,E)`→`res_..`), applied at every `Arr_` site. Element-type
        collection became recursive (`note_arr_types`) and now also **walks
        function bodies** (`collect_expr`/`collect_stmt`/`collect_block`) to find
        composite element types that appear only in literals (e.g. `grid :=
        [[1,2,3],…]` → `[int]`; `[Some(Point(1,2)), None]` → `Option(Point)`).
        Cleared `aggregates` (nested arrays + `[P]==`), `projections` (nested
        index-set, projected field/array mutation, `&arr[i].x`), `option_arrays`
        (`[Option(int)]`/`[Option(Point)]`).
      Remaining failures (6): `tuples`, `slices`, `float_maps` (2nd map type),
      and parser gaps in `maps`, `match_reuse`, `ctor_move`.
- [ ] **Stage 4** — fixpoint bootstrap (B ≡ C), retire the C compiler
