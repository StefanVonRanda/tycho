# Spec-driven design review — the tightening ledger

> Started 2026-07-13, from the design conversation after the full source-audit of
> the Tycho 1.0 spec. Writing a spec forces a decision on every corner, which is
> the chance to reconcile the language with its own stated identity. This is the
> ledger of what the audit surfaced: what to **change**, what to **decide**, and
> what to **keep**. It began as a decision record with nothing implemented; most
> of the fail-closed cluster has since shipped — see **Status** immediately below.

## Status — audited 2026-07-15 (both compilers re-probed)

Most of this ledger has landed; the original "nothing implemented yet" framing is
superseded. Verified by probing tychoc **and** tychoc0:

- **A1–A4 (fail-closed gaps) — RESOLVED, parity-clean.** `range` step 0 rejects a
  literal and **aborts** on a runtime 0; `chr(n)` **aborts** outside `0..255`
  ("out of byte range 0..255"); a **negative shift aborts** and `≥ width` yields 0
  (spec §5.2.7); an out-of-range `to_int(float)` **aborts** ("float-to-int out of
  range"). Both compilers agree, with descriptive messages.
- **C1 — RESOLVED.** The sized-int family is first-class (`i8`…`i64`, commit
  `f824a2a`); no longer extern-only.
- **C3 — RESOLVED.** `fn == fn` is **rejected** ("functions are not comparable"),
  not compared by identity.
- **C2 — addressed.** Both `x := None` and `x := Ok(1)` **reject with a clear
  "annotate the type" diagnostic** — symmetric and fail-closed.
- The `str(char)` papercut is gone — `str` of a char prints its glyph.

**Still open:**
- **A5 — evaluation order.** *Not* pinned; spec `17-runtime.md:83` documents it as
  target-inherited (not sequenced by Tycho). The sharpest remaining item for the
  two-implementations thesis — the live work.
- **B — the `char`/byte-type fork.** `char + int` still yields a `char` whose value
  can exceed 255 (verified both compilers). A genuine design decision (reopens a
  STATUS discipline), pending a call; `ord(c)` is still unbuilt.

## Where the spec stands (context for the next session)

- `docs/spec/` = a **source-audited draft** (chapters 1–33 + appendices A–H).
  Every chapter was cross-checked against `src/tychoc.c` by an adversarial
  auditor and each finding re-verified against source before applying. All
  internal links/labels/section-refs resolve. Reference docs back-ported to match.
- **Not complete.** Missing for a finished/ratified 1.0: the `make spec-check`
  gate (examples run on both compilers — the spec's own §2.3 promise, unbuilt);
  Appendix A (collected grammar — a pointer stub); Appendix E (conformance
  coverage matrix — empty); full corelib per-function semantics; a *reverse*
  completeness pass (walk the compiler's constructs, confirm each has a home in
  the spec — the audit only checked "is what's written correct," not "is
  everything present"); and the audit used one oracle (`tychoc`), not both.
- **Audit ≠ proof.** The auditors are models reading source; they found real bugs
  that had shipped (`range` step 0, `chr`, `>>`), so they also can't be assumed
  to have caught everything.

## The through-line

Tycho's stated identity is **fail-closed, defined behavior, deterministic** (see
STATUS, spec §30). The audit found ~5 corners that quietly *don't* honor it.
Those are the highest-value tightening targets: closing them makes the language
*more itself*, not different.

## A. Fail-closed gaps — recommend CHANGE (they violate Tycho's own principle)

| # | Corner | Current (verified) | Why it's a gap | Recommendation | Cost |
|---|---|---|---|---|---|
| A1 | `range` step 0 | **Infinite loop** when `start > stop` (`range(3,0,0)` hangs); 0 iterations when `start ≤ stop`. Loop test is `step>0 ? i<stop : i>stop`, no guard. | Bounds abort, div/0 aborts — but a zero step fails *open*. | Reject a **literal** 0 step at compile time; **abort** on a runtime 0 step (like `/0`). | compiler + fixpoint/fuzz reverify |
| A2 | `chr(n)` out of range | **Silently masks** `n & 0xff` (`chr(256)`→NUL, `chr(-1)`→255). No abort. | Array/string index abort on OOB; `chr` doesn't. Same input class, opposite discipline. | Abort on out-of-range `n` (consistency), OR keep but state the type truncates. | compiler + reverify |
| A3 | Shift `<<`/`>>` ≥ width or negative | **C UB** (`1<<64`→0 today, but undefined). No mask/guard. | `-fwrapv` made overflow defined and `/0` traps; leaving shifts UB is inconsistent. | Define (mask), or reject a literal bad count, or abort. | compiler + reverify |
| A4 | Out-of-range `to_int(float)` etc. | **C UB** (garbage). In-range truncates toward zero. | Same fail-open-into-UB as A3. | Define (saturate/wrap — pick one) or abort. | compiler + reverify |
| A5 | Evaluation order | **Unspecified.** Args/operands/places not sequenced by Tycho — byte-identical *only* because both compilers emit the same C to the same `cc`. | **Sharpest one for the thesis.** The whole credibility is "two implementations, byte-identical output"; a third implementation could differ. | **Pin left-to-right** (emit sequenced temporaries). Costs little; closes a real determinism hole. | compiler + reverify |

Note `>>` is **arithmetic on signed `int`, logical on `u32`/`u64`** (verified:
`-8>>1 = -4`). Defensible (matches C), but the same operator changing sign
behavior by operand type is a footgun worth an explicit callout even if kept.

## B. The `char` / byte-type question — DECIDE (genuine design fork)

`char ± int` yields a `char` whose value can be **397** (`'a' + 300`, verified) —
the type says "byte," the value isn't one. The type lies about its own invariant.
Three coherent options; current is arguably the worst (has the cost *and* leaks
the invariant):

- **(a) Mask to a byte** on char arithmetic — enforce the 0..255 invariant.
- **(b) `char + int → int`** — drop the pretense; a byte offset produces a number.
- **(c) Keep** — accept that a `char` isn't really 0..255.

Related papercut: `str(char)` is a **type error** (byte≠number discipline), so
printing a char's value needs `to_u32(c)`, and f-strings can't hold a char.
Recommendation: add an **`ord(c) -> int`** builtin so the discipline keeps its
ergonomics, and pick (a) or (b). (Reopens a STATUS-decided discipline → user call.)

## C. Principled asymmetries — DECIDE (keep-or-tighten judgment calls)

- **C1 — sized-int family is half-in.** `u32/u64/f32` first-class; `u8/u16/i8/
  i16/i32/i64` extern-only ("int to Tycho"). A regular language doesn't split a
  type family by demand. Finish it, or state the split as intentional. ROADMAP
  already argued YAGNI for the rest — the spec just makes the irregularity
  visible. (mostly decide + document)
- **C2 — `x := None` is pending/groundable, `x := Ok(1)` rejected immediately.**
  Principled (one type param vs two), but surprising enough it tripped the
  auditors. Make uniform or document loudly. (decide + document)
- **C3 — function values compare by identity** — the only non-structural `==` in
  a fully value-semantic language. Necessary (structural closure equality is
  ill-defined), but it's the one exception to the cleanest story. Own it
  explicitly. (keep + document)

## D. KEEP — the thesis core is proven and coherent; a tightening pass must not erode it

- Value semantics → implicit hierarchical arenas (the thesis; measured, self-hosted).
- `inout` (call-scoped copy-in/out borrow) and `sink` (owned/consuming).
- The **closed** generic-predicate set — the deliberate refusal to abstract is a
  feature, not a gap (traits are a decided non-goal).
- Structural deep equality mirroring the deep copy.
- The two-compiler byte-identical discipline + differential fuzzing.

## E. How to work this

1. **Cheap (decide + spec/doc):** C1, C2, C3, and the `>>` callout.
2. **Compiler changes (need fixpoint + fuzz + goldens reverify):** all of A, and
   whichever `char` option in B. Land each in **both** compilers (parity), guard
   with a `tests/reject/` or golden fixture, keep the fixpoint byte-identical.
3. **Resolve each by probing both compilers, then user decision** — several
   reopen STATUS-decided items, so they are the user's calls, not to be changed
   unilaterally.

Suggested order: the fail-closed cluster (A) first — most defensible, most "in
character," and it's what a spec-quality language would be expected to nail — then
the `char` fork (B).
