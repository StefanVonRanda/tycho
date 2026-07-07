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

**Status — FFI-boundary slice shipped.** `u8`…`u64`/`i8`…`i64` are now valid in an
`extern` signature (by-value param or return): the emitted C prototype uses the real
fixed-width type (`unsigned int`, `unsigned long long`, …) so a call matches the C ABI
instead of forcing everything through `long`, while the value stays `int` to Tycho (no
arithmetic/printing/map/literal changes, no leak — `x: u32` outside an extern is still an
error). Both transpilers, both agree, ASan-clean, `tests/ffi` case (9). The remaining,
larger slice — sized ints as **first-class in-language** types (u32/u64 arithmetic with
defined wrap for corelib crypto) — is unbuilt; see the scope options in the design
discussion. `f32` also unbuilt.

### 1.2 `const` bindings and compile-time constants
**State today:** no `const` in the reference. Enum variants carry values, but there's
no `const MAX = 1024` / `const VERSION = "..."`. Values that never change are
declared as ordinary `:=` locals or hardcoded.

**Why:** array-size clarity, magic-number elimination, and a prerequisite for
*const generics* (1.6). A `const` is just a name the checker folds — no runtime
object, no allocation.

**Thesis fit:** total (compile-time only).
**Effort:** low–medium. **Priority: medium.**

### 1.3 Compiler diagnostics
CONTRIBUTING already names this as wanted work ("keyword-used-as-variable
message"). The compiler is correct and fails closed; the gap is *humane* errors —
source span + caret, "did you mean", type-mismatch showing both sides, the
`where`-predicate that failed by name. This is the single biggest day-to-day
ergonomics lever and has zero thesis risk.
**Effort:** medium, incremental. **Priority: high** (cheap wins, compounding).

### 1.4 corelib gaps
CONTRIBUTING lists "corelib gaps" as useful work. Suggested method: pick a target
task list (a small HTTP service, a log parser, a build tool) and build it against
corelib; every reach for something missing is a ticket. Likely gaps to audit
against the 25 shipped packages: TCP/UDP sockets (only `http` today), TLS,
compression (gzip/zlib), buffered I/O, a bignum/decimal type, richer `datetime`
(timezones), process/subprocess, environment/args helpers. **Priority: medium**,
demand-driven — don't build ahead of a real program that needs it.

### 1.5 Tooling maturity
- **LSP completeness** — hover-types, go-to-def, find-refs, rename, completion.
  The type info exists in the transpiler; surface it.
- **Debugging story** — you emit C, so DWARF flows through. Document the
  `gdb`/`lldb`-on-generated-C path (or a source-map) so `mut`/arena frames are
  legible. Cheap, high goodwill.
- **Editor reach** beyond VS Code/Zed if anyone asks (Neovim via the LSP is
  near-free).

**Priority: medium.**

### 1.6 Const generics (array-length parameters)
Depends on 1.2. Lets `[N]T` fixed-size arrays and generic code over a size be
expressed. Fits monomorphization exactly (each `N` is a binding, like each type
`$T`). Only pursue if a real need appears — it's the kind of feature that's easy to
add speculatively and then carry forever. **Priority: low, YAGNI-gated.**

### 1.7 Early `close(h)` on typed handles
The one item STATUS already lists as genuinely-open-minor
(`typed-handles-design.md`). Handles auto-free at scope exit; this is just an
early-release optimization. **Priority: low**, but it's the smallest closed loop
on the board — good warm-up.

---

## Tier 2 — Worth reopening (the ask items; decision was defensible, not airtight)

### 2.1 The ternary — reframed as **expression-valued `if`/`match`** [reopens a STATUS non-goal]
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
