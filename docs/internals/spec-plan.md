# Tycho 1.0 — formal language specification: build plan

> Status: **complete first draft** (2026-07-12). Tracks ROADMAP §1.8
> ("Formal language specification"). This is the working plan for writing the
> spec — its structure, methodology, the reconciliation punch-list, the
> conformance gate, and the phased sequence. The spec itself lives under
> `docs/spec/` (28 files, ~4.2k lines): all chapters 1–33 and appendices A–H are
> drafted.
>
> **Drafting phases 0–5 done; the release-readiness punch-list below is now fully
> CLOSED (last item cleared 2026-07-20).** Kept as a record of what the list was:
> 1. ~~Resolve the remaining `Editor's note` corners~~ — **DONE.** Concurrency
>    ordering (#23 channel delivery = send-linearization/ticket order; #24 select
>    = listed-order, not fair; #25 happens-before via the cell `seq`
>    release/acquire; #26 cross-thread wait unexpressible — affine handle) pinned
>    from `runtime/tycho_rt.c`; FFI sized-int round-trip (#20) pinned by a shim
>    probe (extern-only spellings narrow-in / sign-or-zero-extend-out; first-class
>    `u32`/`u64`/`f32` take a value of that type). Also **source-audited all four
>    delegated chapters** (12/15/16/18) against `src/tychoc.c` — fixed ~15
>    findings incl. two WRONG claims (`x := []`/`x := None` are pending, not
>    errors) and a real error in my own FFI chapter (u32/u64 are first-class, not
>    extern-only). Verified all internal anchor links + `§`-labels resolve (fixed
>    4 broken anchors, 4 mislabeled `[§13]`→`[§20]`).
> 2. ~~Build the `make spec-check` gate and populate the Appendix E coverage
>    matrix (Phase 5)~~ — **DONE.** `make spec-check` (`scripts/spec_check.sh`) is a
>    Makefile target wired into `make ci`; Appendix E holds 81 conformance rows
>    (§ + `tests/` references).
> 3. ~~Fix the tychoc0 `inout`-exclusivity fail-open~~ — **DONE** (§6a; locked by
>    `tests/reject/inout_alias.ty`).
> 4. ~~Back-port the Appendix H drifts to `docs/reference/*`~~ — **DONE.** H1
>    leading-underscore package privacy now stated at `reference/packages.md:22-24`
>    (commit `cf51e09`); the wording drifts back-ported too.
> 5. ~~Flatten the collected grammar into Appendix A~~ — **DONE**
>    (`docs/spec/appendix-a-grammar.md`, all productions collected).

## 0. Decisions locked (the four that shape everything)

| Decision | Choice | Consequence |
|---|---|---|
| Deliverable | Plan **+ begin drafting** the mechanical chapters | `docs/spec/` scaffolded; Lexical + Grammar drafted from `src/tychoc.c`. |
| Scope | **Language + corelib** | One spec. Parts I–XI define the language; Part XII is the normative library reference for the 31 corelib packages. |
| Semantics basis | **Abstract & exact** | `int` = 64-bit two's-complement, `u32/u64` exactly 32/64-bit, `f32`/`float` IEEE-754 binary32/binary64 — independent of the C backend. Near-zero implementation-defined behavior. |
| "1.0" meaning | **Spec is the freeze + date versioning** | The 1.0 spec *is* the language freeze; adopt the ROADMAP date scheme (`Tycho 2027`). Conformance is stated against a named, dated version. |

## 1. What the spec is (and is not)

**Is:** a single normative document that defines Tycho precisely enough that a
third party could build a conforming implementation from scratch and produce
byte-identical output — grammar productions for every construct, a defined
semantics for every statement and expression, the arena/lifetime model as
stated rules, and the conversion/overflow/failure behavior of every scalar.

**Is not:** a replacement for the 13 example-driven reference pages under
`docs/reference/`. Those stay as the *tutorial* surface. The spec is the
*contract*. Where the reference says "here's how you use maps," the spec says
"a `[K:V]` literal MUST have all keys of one hashable type; a read of an absent
key yields the value type's zero and MUST NOT insert."

**Relationship to existing docs:** the spec *distills and tightens* the
reference + `docs/memory-model.md` + `docs/thesis.md`, and *supersedes* them on
any point of conflict (Appendix H logs every such reconciliation).

## 2. Why this spec is unusually tractable (the leverage)

Most language specs define behavior in a vacuum and hope implementations
follow. Tycho already has a **mechanically-checkable behavioral contract**:

- **Two implementations that agree byte-for-byte** — `tychoc` (C reference) and
  `tychoc0` (self-hosted). `make fixpoint` asserts `B == C` byte-identical.
- **A differential fuzzer** (`make fuzz`, 500 seeds) holding both to identical
  accept/reject and identical output on random valid programs.
- **274+ golden fixtures** (`tests/`, incl. `tests/reject/` must-fail).
- **Accept/reject parity lanes** (`typeparity` 4608/4608, `eqparity`,
  `unaryparity`, `parforparity`) that close the "fixpoint is output-only" hole.

So the spec is **not** an act of invention — it is **distillation +
reconciliation**, with a ready-made conformance oracle. Two corollaries:

1. Every normative clause can be *verified against both compilers*: run it,
   diff the two, cite the fixture or the `src/tychoc.c` line.
2. Where the two compilers **implicitly agree on something no doc states**
   (evaluation order, arena-per-arm, channel ordering — see the punch-list),
   the spec's job is to *discover that agreement by reading both* and elevate it
   to a normative rule — or, if they diverge, surface a latent bug. This is the
   real work, and it is the highest-value output of the whole exercise.

This lowers the risk ROADMAP flagged as "high effort." The size is still a
book; the *risk per clause* is low because the oracle already exists.

## 3. Normative conventions (fixed for the whole spec)

- **Requirement keywords:** RFC 2119 / 8174 — MUST, MUST NOT, SHOULD, SHOULD
  NOT, MAY. Plus two defined terms used sparingly and always in an explicit
  register (Appendix F): **unspecified** (a conforming program must not rely on
  it; implementations may differ) and **implementation-defined** (an
  implementation must document its choice).
- **Grammar notation:** W3C-style EBNF (`::=`, `|`, `?`, `*`, `+`, grouping
  `()`, terminals in double quotes, char-classes `[...]`). Because Tycho is
  indentation-sensitive, `INDENT`, `DEDENT`, and `NEWLINE` are **terminal
  tokens produced by the lexer** (the Python-grammar approach). The lexical
  grammar (Ch 3) is separate from the phrase grammar (Ch 4).
- **Source of record:** `src/tychoc.c` (the reference transpiler) is
  authoritative for grammar and behavior. The tree-sitter grammar
  (`editors/zed/grammars/tycho/grammar.js`) is a **non-normative highlighter**
  and is known to disagree in 7 places (punch-list group A) — it does not model
  indentation at all. The spec is extracted from the C parser, not tree-sitter.
- **Abstract-exact policy:** scalar widths and behavior are defined
  independently of the C target (decision §0). The C lowering is described as
  *one conforming realization*, not the definition. The single register of
  implementation-defined behavior is kept explicit and minimal (Appendix F).
- **Examples are executable:** every code block in the spec MUST compile and run
  on **both** compilers and produce the shown output — same discipline the
  reference pages already hold to, enforced by a new `make spec-check` gate
  (§8).

## 4. Deliverable layout

Multi-file under `docs/spec/`, mirroring the reference's one-topic-per-page
structure (a single 400-page file is unreviewable). Files are numbered so the
reading order is the normative order.

```
docs/spec/
  README.md            # spec map + status (the index)
  00-conventions.md    # Ch 1–2: scope, conformance, versioning, notation
  01-lexical.md        # Ch 3: source text, tokens, indentation   [DRAFTING]
  02-grammar.md        # Ch 4: the collected phrase grammar         [DRAFTING]
  03-types.md          # Ch 5
  04-inference.md      # Ch 6
  05-generics.md       # Ch 7
  06-conversions.md    # Ch 8
  07-memory-model.md   # Ch 9–11  (value semantics, arenas, inout)
  08-declarations.md   # Ch 12
  09-expressions.md    # Ch 13
  10-statements.md     # Ch 14
  11-functions.md      # Ch 15
  12-aggregates.md     # Ch 16–19 (arrays, structs, maps, enums/match)
  13-concurrency.md    # Ch 20–23
  14-ffi.md            # Ch 24–26
  15-program.md        # Ch 27–28 (program structure, packages)
  16-builtins.md       # Ch 29
  17-runtime.md        # Ch 30 (defined failure behavior)
  18-library/          # Part XII (corelib) — Ch 31–33, one file per package
  appendix-*.md        # A–H
```

## 5. Specification outline (chapter map)

Each entry: **coverage** · *primary sources* · confidence (H/M/L — how much is
already pinned vs. needs discovery).

### Part I — Foundations
1. **Scope, conformance & versioning** — what a conforming implementation is;
   the two-impl oracle as the conformance model; **conformance tiers** (core =
   pure-Tycho + libc-only; extended = `deps`/pkg-config packages that MAY be
   absent); date-based versioning (`Tycho 2027`). *STATUS.md, CONTRIBUTING.md.* H
2. **Notation** — EBNF conventions, requirement keywords, the
   implementation-defined register. *this plan §3.* H

### Part II — Lexical structure & grammar
3. **Source text & lexical structure** — encoding; logical lines; the
   indentation model (INDENT/DEDENT, depth-not-width, tab/space mixing is an
   error); the token set; **keywords vs contextual identifiers**; operators +
   precedence/associativity; literals (decimal-only ints, float forms, char,
   string, f-string, bool, null); comments. *lexer `src/tychoc.c:107-445`.* H
4. **Grammar** — the collected phrase grammar: top-level declarations,
   statements, expressions, and the type grammar. *parser `src/tychoc.c`.* H

### Part III — Type system
5. **Types** — scalars with **exact widths**; `bytes`; composites (arrays,
   fixed arrays, slices, tuples, structs, maps, enums, `Option`, `Result`, soa);
   newtypes; function types; typed handles; the **structural-vs-nominal identity
   model** per constructor. *reference/types,arrays-slices,structs-tuples,maps,
   enums-options.* H
6. **Type inference** — bidirectional local (synthesize vs. check-against-want),
   `T_PENDING` grounding, "no unification / no type variables." *checker
   `src/tychoc.c`.* M
7. **Generics** — binding-based monomorphization; `$T`; the **closed** predicate
   set (`numeric/comparable/has_str/hashable/defaultable`) + type-set `where`;
   const generics `[$N]T`; variadic generics `...$T`; UFCS dispatch; `zero$(T)`
   and the `name$(Types…)` explicit-type-arg form. *reference/generics.* M
8. **Conversions & casts** — the explicit `to_*`/`str` builtins; **literal
   adaptation only, no value widening**; hard-error matrix. *checker.* M

### Part IV — Memory & object model (the heart; hardest chapters)
9. **Value semantics & the copy invariant** — deep copy on cross-arena move;
   structural `==` as the mirror; the uniqueness guarantee; the
   **unobservable-optimization policy** (move-on-last-use, in-place append,
   return-slot move, recycle: declared observationally-transparent by fiat).
   *thesis §2–4, memory-model.* M
10. **Arenas & lifetimes** — the scope→arena mapping; the escape rule (down =
    arg, up = return/outer-assign); destination-passing; per-statement/loop-body
    arenas. *memory-model, runtime/tycho_rt.c.* M
11. **`inout`** — copy-in/copy-out borrow; the exclusivity rule; heap `inout`
    (push/growth/field mutation through the borrow) and the hidden
    owning-arena parameter. *reference/basics, memory-model.* M

### Part V — Expressions, statements, declarations
12. **Declarations, bindings & scoping** — `:=` / typed `x:T=` / `=` /
    place-assign / `const`; shadowing; name resolution; package name mangling. H
13. **Expressions** — operators; precedence; **place expressions**;
    short-circuit `and`/`or`; **expression-valued `if`/`match`** (tail position,
    branch unification, must-have-`else`); lambdas & closures (deep-copy
    capture, re-homing on return); f-strings; **evaluation order (pinned
    normatively — punch-list B).** M
14. **Statements & control flow** — `if`/`elif`/`else`; `match` + exhaustiveness;
    the three `for` shapes (while / counting / foreach); `break`/`continue`;
    `return`; `or_return`; `delete`; `die`. H
15. **Functions** — parameters; `inout`/`sink` conventions; variadics;
    first-class function values; UFCS methods; user-defined `subscript`
    projections. M

### Part VI — Aggregates in depth (folded into `12-aggregates.md`)
16. **Arrays, fixed arrays, slices** — growth, bounds, borrow-vs-copy, slice
    zero-copy rules. H
17. **Structs, tuples, soa** — positional construction, recursion-through-
    container rule, tuple 2–8 arity, soa. H
18. **Maps & subscripts** — legal key types, place semantics, absent-key read =
    zero, `m.get`, `delete`; user-defined yielding `subscript`. H
19. **Enums, `Option`, `Result`, `match`** — variant globality, exhaustiveness,
    bare-constructor grounding, `or_return`. M

### Part VII — Concurrency
20. **The concurrency model** — deep-copy thread boundary; **the ordering/
    memory-model axioms** (happens-before for spawn→body, send→recv, task→wait —
    punch-list D). M/L
21. **`spawn`/`Task`/`wait`** — affine + structured; implicit join at every
    scope exit; the spawn cap (default 1024). H
22. **`parallel for`** — chunked fan-out; **reduction determinism** (int exact
    across thread counts; float MAY reassociate); other-write = compile error. H
23. **Channels & `select`** — bounded lock-free MPMC ring; `recv → Option`, drain
    semantics; **message-ordering guarantee + select fairness (punch-list D).** M

### Part VIII — FFI
24. **`extern` & the C boundary** — crossable types; **sized-int ABI
    (narrow-in/widen-out, observable round-trip — punch-list C)**; deep-copy-at-
    boundary; linking/cc-line. H
25. **Typed handles** — RAII scope-exit free; affine; `close(h)`;
    borrow-on-pass; fail-closed storage bans. H
26. **FFI & concurrency** — the value-race-freedom guarantee stops at the
    boundary; the per-thread shim pattern. H

### Part IX — Program & packages
27. **Program structure** — exactly one `fn main()`; compilation unit = whole
    reachable import graph merged to **one AST → one C file → one binary** (no
    separate compilation). H
28. **Packages & modules** — Odin-style directory packages; `import`; `core:`
    resolution + `TYCHO_CORELIB`; **no privacy**; name mangling; `deps`/shim
    auto-discovery. H

### Part X — Builtins
29. **Builtin functions** — the **complete** dispatch-derived catalog (not the
    incomplete `builtins.md`): I/O, conversions, strings, arrays, maps, type
    builtins, concurrency, fs/time, libm; **magic vs. registered-Sig**; the
    language↔library line. *`register_builtins` + magic cases in `src/tychoc.c`.* H

### Part XI — Runtime behavior
30. **Defined runtime behavior & failure** — the **abort / wrap / clamp /
    unspecified register**: signed-overflow wrap, div/mod-by-zero abort, bounds
    abort, `substr` clamp, OOM, spawn-cap, channel misuse; the fail-closed
    principle; the float/shift/conversion corners (punch-list C). H

### Part XII — Standard library (corelib) — normative, but tiered
31. **The library contract** — every corelib function allocates into the
    caller's arena; the conformance tiers; pure-Tycho vs. libc-shim vs.
    `deps`/pkg-config. H
32. **Core packages** — one section each: math, fmath, char, strings, path,
    arrays, iter, sort, rand, time, datetime, json, csv, base64, hex, url, uuid,
    hash, md5, sha256, io, os, regex, net, bignum, decimal. *docs/corelib.md +
    the 6 undocumented packages.* M
33. **Extended packages** (`deps`-gated) — http (libcurl), crypto (libcrypto),
    compress (zlib), image (libpng), tls (openssl). M

### Appendices
A. Collected grammar (full EBNF). · B. Keyword & contextual-identifier tables. ·
C. Operator precedence table. · D. Builtin index. · E. Conformance test map
(clause → fixture). · F. Implementation-defined & unspecified register (explicit,
minimal). · G. Glossary. · H. Differences-from-the-reference log (drift
reconciliation).

## 6. The "must pin down" punch-list (the real work)

The surveys found ~30 corners the current docs leave informal and ~10
doc↔implementation drifts. **This list is the spec's actual labor** — each item
is resolved by *reading both compilers to find what they actually do* (and if
they diverge, filing a bug). Grouped; each carries its resolution owner.

### A. Grammar & lexical — extract from the C parser; reconcile tree-sitter
1. `while` appears in tree-sitter but **does not exist** — loops are `for` only.
2. `char` / `void` listed as types in tree-sitter but are **not spellable** type
   keywords (no `: char` annotation form exists).
3. tree-sitter treats `import`/`package`/`extern`/`soa` as reserved keywords;
   they are **contextual identifiers**. It omits the real keyword `handle`.
4. tree-sitter `number` regex misses exponents and leading-dot floats.
5. `::` is lexed but **unused** (dead token); `...` is a real operator
   (variadic/spread); `{`/`}` are **not tokens** (only inside f-strings).
6. tree-sitter's builtin list is partial and includes **removed** `map_get`/
   `map_set` (now a hard parse error).
7. tree-sitter models **no indentation** (by design) → cannot be the grammar of
   record. **Resolution:** extract from `src/tychoc.c`; then either fix
   tree-sitter to match or mark it explicitly non-normative in Ch 3.
8. **Integer literal grammar is decimal-only** — no hex/octal/binary, no
   underscores, no numeric suffix. `0u64`-style suffixes do not exist at source
   level (that `U`/`ULL` is codegen-side). Pin explicitly.

### B. Evaluation order & scoping — the biggest genuine gap (nothing is stated)
9. **Argument evaluation order** of `f(a,b,c)` — unspecified today. Decide
   (recommend: left-to-right, MUST) by reading both codegens.
10. **`match` subject evaluated exactly once** — implied by the desugar, never
    stated. Pin.
11. **Compound-assign single-evaluation** — `a[i] += e` must evaluate `i` (and
    the array place) once. Pin.
12. **General place-evaluation order** — receiver vs. index vs. RHS for
    `p.x[i] = e`. Pin.
13. **Exact scope→arena set** — the docs name function / if / else /
    loop-iteration / per-statement arenas but never close the set: do `match`
    arms, `elif` arms, expression-`if`/`match` arms, and bare indented blocks own
    an arena? Is an untaken arm's arena ever created? What is the inter-arm free
    ordering? (Declare the observable consequence; the arena mechanics are an
    implementation realization but the *reachability of freed storage* is
    normative.)
14. **Nested-place escape target** — the arena-selection rule for
    `outer.field[i] = e` / `m[k] = e` when `outer` lives several scopes up. Docs
    give only the single-variable cases.
15. **`inout` exclusivity through aliasing places** — is `f(&a[i], &a[j])` with
    `i == j`, or `&a` together with `&a[i]`, rejected? Docs state the rule only
    for "the same variable."

### C. Numerics, floats, conversions — sharpened by the abstract-exact decision
16. **`int` = 64-bit exact vs. C `long`.** The abstract-exact choice makes 64-bit
    `int` a *requirement on implementations*. The reference compiler lowers `int`
    to C `long`, which is **32-bit on LLP64 (Windows) and ILP32**. The spec MUST
    either (a) require a fixed-width 64-bit lowering (`long long`/`int64_t`), or
    (b) scope conformance to LP64 targets. Recommend (a) + note the current
    codegen satisfies it on LP64 only. **This is the single sharpest consequence
    of decision §0.**
17. **Float semantics** — declare IEEE-754 binary64/binary32 conformance:
    NaN/inf/signed-zero behavior, `==` on floats is bitwise (`0.0/0.0`, NaN
    ordering, `NaN == NaN`). Currently unspecified.
18. **Shift amount ≥ bit-width or negative** (`<<`/`>>`) — no runtime guard is
    visible; C UB unless codegen masks. Pin (define or reject or mask).
19. **`to_int`/`to_float`/`to_u32…` out-of-range** conversion behavior — pin
    (truncate/saturate/wrap/reinterpret) per pair.
20. **FFI sized-int round-trip** — does `int → u32 param → int return` preserve
    value / sign-extend? "C's defined conversion" must become a Tycho-observable
    rule.
21. **`range` with step 0** — behavior unstated.
22. **`char ± int` byte-domain** — docs say it "stays within a byte" but the
    checker only fixes the *type*; the runtime wrap of `'a' + 300` needs a rule.

### D. Concurrency ordering & memory model — informal today
23. **Channel MPMC message ordering** — the Vyukov ring is FIFO per ticket, but
    no per-sender / total-order guarantee is stated for multiple producers/
    consumers. Pin the guarantee.
24. **`select` arm fairness/priority** — tried in listed order, or fair?
    (Implementation scans in listed order via `try_recv`.) Pin.
25. **Happens-before axioms** — what a deep copy publishes across spawn→body,
    send→recv, task→wait. Assert as ordering rules, not prose.
26. **`wait` re-entrancy** — waiting a task from a thread other than the spawner.
    Not addressed.

### E. Type identity & generics edge cases
27. **`empty$(T)` is not a builtin** — only `zero$(T)` is special-cased;
    `name$(Types…)` is the generic explicit-type-argument call form. Reconcile
    `docs/generics.md`'s conflation; the spec defines only `zero$` + `name$(...)`.
28. **Function-value equality is identity, not structural** — the one
    non-structural `==` in an otherwise fully value-semantic language. Call out.
29. **Structural vs. nominal interning** — tuples/arrays/maps/options/results/
    functions/soa/task/chan are structurally interned; structs/enums/newtypes/
    handles are nominal. State the identity model per constructor.
30. **`defaultable` excludes newtypes** (uses `t ==`, not `base_of`, unlike every
    other predicate) → `zero$(newtype)` fails even over a defaultable base. State.
31. **Newtype underlying set** — restricted to int/float/string/bool/array/map/
    struct; **excludes** enum/tuple/sized-numerics/char/bytes/nested-newtype. Pin
    precisely (docs give only positive examples).
32. **`str(char)` is intentionally an error** though `char` is comparable/
    orderable — state the asymmetry deliberately.

### F. Doc↔implementation drift to reconcile (Appendix H)
33. **Int-keyed map value restriction** — the literal-map error text says "int/
    float values only," but `map_of` actually allows **any** value type. Pin the
    real rule (V unrestricted) and fix the stale message.
34. **f-string hole types** — docs say int/float/bool/string, but the `str(...)`
    desugar also accepts u32/u64/f32. Reconcile.
35. **`builtins.md` is an incomplete catalog** — missing `eprint`, `is_null`,
    `to_ptr`, `to_i32`, `to_u32/64`, `to_f32`, `to_under`, `keys`. Ch 29 is the
    complete dispatch-derived set.
36. **No `assert`/`abort`/`panic` builtin** — `die` is the only user abort;
    internal aborts are not callable. State precisely.
37. **6 undocumented corelib packages** — bignum, compress, decimal, image, net,
    tls exist in the tree but not in `docs/corelib.md`. Part XII must add them.

### G. Consequences of the two big scope decisions
38. **Corelib in scope** → the spec must define the **language↔corelib interface
    contract** normatively (arena-allocation-into-caller, `deps`/shim mechanics,
    the tier boundary), not just list functions.
39. **Conformance tiers** — `deps`/pkg-config packages (http, crypto, compress,
    image, tls) form an **extended tier** an implementation MAY omit and the test
    harness already skips when the C lib is absent; pure-Tycho + libc-only is the
    **core tier**. Ch 1/31 must formalize this so "conforming" is well-defined
    without libcurl/openssl.

## 6a. Resolved by differential probing (2026-07-12)

Probed on both compilers (`tychoc` + self-hosted `compiler/tychoc0`); each
result below **agreed** on both unless noted:

- **#10 `match` subject — evaluated exactly once.** RESOLVED → normative rule.
- **#11 compound-assign index — a side-effecting index *call* is evaluated
  once** (hoisted). RESOLVED. (A pure index sub-expression may still be
  evaluated twice; stated in §13.)
- **#17 float semantics — IEEE-754, non-trapping.** `0.0/0.0` → NaN (no trap),
  `1.0/0.0` → `inf`, `-1.0/0.0` → `-inf`, `NaN == NaN` → `false`. RESOLVED →
  `float`/`f32` are IEEE-754 binary64/binary32; division never traps; NaN is
  unordered. (The *textual* rendering of NaN/inf by `str`, e.g. `-nan`, derives
  from the C library and is **implementation-defined** — Appendix F.)
- **#21 `range` step 0 — zero iterations** (`range(0,3,0)` ran the body 0
  times). RESOLVED → a zero step yields an empty loop.
- **#22 `char ± int` — no byte-masking.** `'a' + 300` yields a `char`-typed
  value holding **397**, not `397 mod 256`. RESOLVED → char arithmetic is
  int arithmetic that keeps the `char` type; the value is NOT reduced to
  `0..255`. (Reconcile `types.md`'s "stays within a byte" wording — Appendix H.)
- **#9 argument evaluation order — UNSPECIFIED.** Observed right-to-left, but
  only because both transpilers emit the *same* C call expression and the same
  `cc` chooses that order; Tycho does not sequence arguments itself, so the
  order is inherited from the C target and is **not** a language guarantee.
  RESOLVED as *unspecified* (Appendix F), with a noted future option to pin it
  by emitting sequenced temporaries. Same reasoning applies to general
  place/side-effect ordering within one expression.
- **#18 shift amount ≥ width — UNSPECIFIED.** `1 << 64`, `1 << 100` both gave
  `0`, but this is C undefined behavior in the emitted code (no guard/mask).
  RESOLVED as *unspecified*: a shift count MUST be in `0..width−1`; otherwise
  the result is unspecified. (Candidate hardening: mask or reject — a design
  decision, not ratified.)
- **#19 out-of-range `to_int(float)` — UNSPECIFIED.** `to_int(1e30)` produced a
  nonsensical, non-representable value (C UB from the float→long cast).
  RESOLVED as *unspecified*: a conforming program MUST NOT rely on an
  out-of-range narrowing conversion. In-range `to_int(float)` truncates toward
  zero.

### Discovered divergence — FIXED (2026-07-12)

- **`inout` two-parameter exclusivity: tychoc0 fail-open — FIXED.**
  `src/tychoc.c:5019` rejects passing one variable to two `inout` parameters of a
  call (`swap2(&x, &x)`, and the array-rooted `addto(&a[i], &a[i])`) with
  *"overlapping mutable access"*. The self-hosted `compiler/tychoc0.ty` had **no
  such check** and accepted both — a fail-open the `fuzz-reject`/`typeparity`
  corpora did not cover. **Fix:** added `eaddr_root_name` + an exclusivity
  double-loop to `check_call_args` in `compiler/tychoc0.ty` (mirrors
  `src/tychoc.c:5006-5021`, reusing the existing `place_root_name` root walk).
  Both compilers now reject the same cases with the same message and still accept
  valid two-different-variable calls. Verified: `make fixpoint` B==C
  byte-identical (29916 lines C), `make test` 298/0, locked by the new
  `tests/reject/inout_alias.ty` (differential must-fail). Emits no codegen (a
  compile-time reject only), so the fixpoint stays byte-identical.

- **Sibling gap — slice-vs-`inout` overlap: tychoc0 fail-open — FIXED.** Audited
  after the above (the reference has a second overlap check right beside it).
  `src/tychoc.c:5022-5038` rejects passing a *slice* of a variable and an `inout`
  of the same variable to one call (`f(a[0:2], &a)` — the `inout` may reallocate
  the buffer the slice views), and `tychoc0` had no such check (accepted it).
  **Fix:** added `place_root_name_slice` + `slice_root` helpers and a
  slice-overlap loop to the same `check_call_args` (mirrors `src/tychoc.c:5022-5038`).
  Both compilers now reject `f(a[0:2], &a)` identically and still accept a slice
  + `inout` of *different* variables. Verified: `make fixpoint` B==C
  byte-identical (29982 lines C), `make test` 300/0, locked by
  `tests/reject/slice_inout_alias.ty`. The rule is already stated normatively in
  the spec at `docs/spec/12-aggregates.md` §16; both compilers now back it.

## 7. Methodology — how each clause gets written and backed

For every normative sentence:

1. **Start from the reference/design-note text** (already example-driven and
   dual-compiler-checked) and tighten to grammar-level precision.
2. **Back it with one of** (mirrors CONTRIBUTING's discipline): a named golden
   fixture in `tests/`, or a source trace citing `src/tychoc.c:line` **and** the
   `compiler/tychoc0.ty` counterpart, or a **new conformance fixture** written to
   pin a previously-untested corner.
3. **For punch-list corners:** write a probe program, run it on both compilers,
   diff. If they agree → that agreement becomes the rule (+ the probe becomes a
   fixture). If they diverge → file a bug; the spec cannot ratify a divergence.
4. **Grammar:** extract productions from the C parser (§3 source-of-record), not
   tree-sitter; reconcile the 7 tree-sitter drifts (group A) in Ch 3.
5. **Drift:** every doc↔impl reconciliation (group F) is logged in Appendix H so
   the reference pages can be corrected in the same pass.

RULE-0/3/8 verification blocks are mandatory for any punch-list resolution that
changes an observable behavior claim.

## 8. Conformance strategy — the spec ships with its own gate

- **`make spec-check`** (new): asserts every fenced code example in `docs/spec/`
  compiles and runs on **both** compilers with the shown output — the same
  mechanism the reference pages already satisfy, pointed at the spec tree.
- **Conformance suite:** a coverage matrix mapping **every normative clause →
  ≥1 fixture** (existing golden, reject fixture, or new probe). Appendix E is the
  living map. A clause with no fixture is flagged, like an untested branch.
- **The two-compiler oracle is the conformance definition:** an implementation
  conforms (core tier) iff it accepts/rejects and outputs identically to the
  reference across the conformance suite. Extended tier adds the `deps` packages.
- **Spec-sync note:** a short "last synced" audit at the top of `docs/spec/
  README.md` (like STATUS.md), re-run before any dated release, asserting the
  spec still matches both compilers.

## 9. Phasing (each phase gated: examples green on both compilers before "done")

- **Phase 0 — scaffolding** *(this turn)*: `docs/spec/` tree; `00-conventions.md`
  (notation, keywords, conformance tiers, versioning, abstract-exact policy).
- **Phase 1 — Lexical + Grammar** *(this turn, drafting)*: `01-lexical.md`,
  `02-grammar.md`, extracted and verified from `src/tychoc.c`. Highest
  confidence, mostly mechanical. Reconcile group A.
- **Phase 2 — distillation chapters**: types, inference, generics, conversions,
  declarations, expressions, statements, functions, aggregates. Mostly tightening
  existing docs; resolve groups E and F as encountered.
- **Phase 3 — the hard chapters**: memory & object model, evaluation order,
  concurrency, FFI. Resolve groups B, C, D via the probe-both-compilers loop.
  This is where the spec does design work.
- **Phase 4 — program, packages, builtins, runtime behavior**: Parts IX–XI.
- **Phase 5 — corelib (Part XII) + conformance**: the library reference, the
  `make spec-check` gate, Appendix E coverage matrix, the six undocumented
  packages, the final spec-vs-both-compilers audit.

## 10. Effort & priority (honest)

- **Size:** a book — ~33 chapters + 8 appendices. ROADMAP rates it **high
  effort**; the abstract-exact + corelib-in-scope decisions *increase* size
  (Part XII adds 31 package sections; abstract-exact forces the numerics/FFI
  corners to be pinned rather than deferred).
- **Risk:** *lower than a typical spec* because of the two-implementation oracle
  (§2). The uncertainty is concentrated in the ~30 punch-list corners, each
  individually cheap to resolve (write a probe, diff two compilers).
- **Priority:** ROADMAP §1.8 says **medium — only if a versioned release is the
  goal.** Decision §0 makes a versioned release the goal, so it is now on the
  critical path for `Tycho 2027`. Sequence Phases 0–1 now (mechanical, durable),
  then gate Phases 2–5 on continued release intent.

## 11. Residual decisions to make *during* writing (not blockers)

- Exact EBNF dialect punctuation (W3C `::=` chosen; confirm on first grammar
  review).
- Whether the unobservable optimizations are stated as "observationally
  transparent by fiat" (recommended) or with their static preconditions spelled
  out normatively.
- `int`-width conformance (punch-list 16): require 64-bit lowering vs. scope to
  LP64. Recommend require-64-bit.
- Whether the `deps` extended tier is *normative-but-optional* or *informative*.
  Recommend normative-but-optional (an implementation MAY omit it and still
  conform at the core tier).
