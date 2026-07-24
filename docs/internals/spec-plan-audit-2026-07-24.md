# Audit — the §6 "must pin down" punch-list, item by item (2026-07-24)

Subject: the **39 numbered items** in `spec-plan.md` §6 (groups A–G, `:281-428`).
Not the five-item header punch-list at `spec-plan.md:12-34` — that one was already
declared closed and is a different list.

Method: for each item, look for a **normative statement in `docs/spec/`** (28
files, ch. 1–33 + appendices A–H). A verdict of CLOSED cites `file:line`. The
list itself was not trusted as evidence; `docs/reference/` was not accepted as
normative (it is explicitly subordinate — `appendix-h-differences.md:5-7`, "On
every such point **this specification governs**").

## Summary

| Verdict | Count | Items |
|---|---|---|
| **CLOSED — pinned in spec** | 36 | 1–8, 10–12, 14–31, 33–39 |
| **CLOSED — deliberately unspecified** | 2 | 9, 13 |
| **CLOSED — obsolete / premise reversed** | 1 | 32 |
| **PARTIAL — pinned only in part** | 0 | — |
| **OPEN** | **0** | — |

**OPEN item numbers: none.**

Every item is now a clean close. **#12** (general place-evaluation order) was the
last PARTIAL; it was closed on 2026-07-24 by probing both compilers — see the row.

Three items were resolved *against* the wording the punch-list assumed (#22, #28,
#32). They are closed, but the closing rule is not the rule the item predicted;
anyone reading §6 alone would come away with the wrong language semantics. Flagged
individually below.

---

## A. Grammar & lexical

The whole group is closed by one structural move: Appendix A is the collected
grammar and the tree-sitter grammar was declared **non-normative** rather than
reconciled — `00-conventions.md:161-165` ("The tree-sitter grammar under
`editors/zed/` is a **non-normative** editor highlighter… it MUST NOT be treated
as a grammar of record"), and `01-lexical.md:298-319` §3.10 enumerates each
divergence, "all resolved in favor of this specification". So items 1–7 are not
merely "closed" — each specific divergence is written down in §3.10 as a numbered
list. That is stronger than the item asked for.

| # | Item | Verdict | Evidence |
|---|---|---|---|
| 1 | `while` in tree-sitter but not the language | CLOSED — pinned | `01-lexical.md:306` — "it lists `while` as a keyword — there is no `while` in the language" |
| 2 | `char`/`void` not spellable type keywords | CLOSED — pinned | `01-lexical.md:307` — "neither is spellable as a type"; corroborated `03-types.md:74` ("there is no `char` type keyword") |
| 3 | `import`/`package`/`extern`/`soa` contextual, `handle` reserved | CLOSED — pinned | `01-lexical.md:308-309` — "they are contextual identifiers (§3.7) — and omits the reserved word `handle`" |
| 4 | number regex misses exponents / leading-dot | CLOSED — pinned | `01-lexical.md:310`; the real rule at `01-lexical.md:216-220` |
| 5 | `::` dead token; `...` real; `{`/`}` not tokens | CLOSED — pinned | `01-lexical.md:155` — "\`::\` \| reserved token, **currently unused** by the grammar" (provenance `:174`, "`::` is lexed at `:402` but no grammar…"); `01-lexical.md:311-312` — "braces are not tokens and `...` is a real operator" |
| 6 | builtin list partial, includes removed `map_get`/`map_set` | CLOSED — pinned | `01-lexical.md:313-314` — "includes the removed names `map_get`/`map_set` (which the language rejects at parse time)" |
| 7 | tree-sitter models no indentation → cannot be grammar of record | CLOSED — pinned | `01-lexical.md:315-319` — "it models no `INDENT`/`DEDENT`/`NEWLINE` (§3.4), so it cannot represent block structure… the tree-sitter grammar has no normative force"; plus `00-conventions.md:161-165` |
| 8 | integer literals decimal-only, no underscores, no suffix | CLOSED — pinned | `01-lexical.md:192-194` — "An integer literal is a run of one or more decimal digits. There is **no** hexadecimal, octal, or binary form, **no** digit-group separator (`_`), and **no** type suffix (such as `u32` or `L`)"; fixture row `appendix-e-conformance.md:38` |

## B. Evaluation order & scoping

| # | Item | Verdict | Evidence |
|---|---|---|---|
| 9 | argument evaluation order | CLOSED — deliberately unspecified | `09-expressions.md:87-91` — "**Argument and operand evaluation order is *unspecified*** (*probed*)… MUST NOT be relied on"; register row `appendix-f-impl-defined.md:14`; rationale `:24-36` |
| 10 | `match` subject evaluated exactly once | CLOSED — pinned | `09-expressions.md:79-80` — "**`match` subject — evaluated exactly once** (*probed*), before any arm is tested"; fixture `appendix-e-conformance.md:114` (`tests/match_subject_once`) |
| 11 | compound-assign single evaluation | CLOSED — pinned | `09-expressions.md:82-83` — "a **side-effecting call inside the place is evaluated once** (*probed*); a pure index sub-expression may be evaluated twice"; fixture `appendix-e:114` (`tests/compound_index_eval`) |
| 12 | general place-evaluation order (receiver vs. index vs. RHS for `p.x[i] = e`) | CLOSED — pinned (2026-07-24) | *Was PARTIAL:* only index-before-RHS was stated (`09-expressions.md:84-86`), leaving the **receiver** leg unaddressed by both §13.4 and `appendix-f:14` (which reaches only "a call's arguments or a binary operator's operands"). *Resolved by probe, not by wording:* the receiver leg is **unobservable by construction** — a place is rooted at a **variable** (`09-expressions.md:15-17`), so a call can never be a place receiver. Both compilers reject `p().x = e()` (tychoc: "cannot assign to a field of a temporary"; tychoc0: parse error) and `p()[i()] = e()` (tychoc: "can only index-assign an array or map variable or field"). A place spine is therefore side-effect-free, and the only side-effecting legs are its **index / subscript-argument** sub-expressions. Probed on both compilers over nested indices (`grid[f()][g()] = e()` → `f g e`), field-then-index, index-then-field (`bs[f()].v[g()] = e()` → `f g e`), map keys, a user-subscript argument (`gr.edge(g()).weight = e()` → `g e`), and the compound-assign forms — **byte-identical output on tychoc and tychoc0**. The general rule is now normative at `09-expressions.md:87-95` ("**The whole place is evaluated before the RHS, left-to-right**… evaluated in **source order**… all of them before the RHS"), with `appendix-e-conformance.md:115` and the fixture `tests/place_eval_order.ty`. No place leg is left unaddressed. |
| 13 | exact scope→arena set | CLOSED — deliberately unspecified (mechanism), normative consequence stated | `07-memory-model.md:97-99` — "The arena *mechanism* (which scope resets versus frees, block-level scratch arenas, per-statement temporaries) is an implementation realization and is **not observable** beyond the guarantees in §10.3"; the guarantees at `:120-131` ("**No dangling**… **No leak at scope exit**"). This is exactly the disposition the item requested ("declare the observable consequence; the arena mechanics are an implementation realization"). |
| 14 | nested-place escape target (`outer.field[i] = e`, `m[k] = e`) | CLOSED — pinned | `07-memory-model.md:114-118` — "**up** — `return e` (to the caller), and `outer = e` / `push(outer, v)` / **any store through a place whose root is an outer variable** (to that variable's storage)… Every destination is decidable at the write site" |
| 15 | `inout` exclusivity through aliasing places | CLOSED — pinned | `07-memory-model.md:169-175` — "two `inout` arguments of one call MUST NOT share a **root variable**. The check is by root variable, conservatively (may-overlap): both `&a[i]` and `&a[j]` — and `&a.x` with `&a.y` — are rejected because they root at the same `a`". Note this is the **spec** stating the rule, independent of the compiler fix; the slice-overlap sibling is at `12-aggregates.md` §16. Fixtures `appendix-e-conformance.md:95` (`reject/inout_alias`, `reject/inout_byval_alias`, `reject/slice_inout_alias`). |

## C. Numerics, floats, conversions

| # | Item | Verdict | Evidence |
|---|---|---|---|
| 16 | `int` = 64-bit exact, not C `long` | CLOSED — pinned | `03-types.md:40-45` — "`int` is a **64-bit two's-complement signed integer**… a conforming implementation MUST realize `int` at exactly 64 bits through a **fixed-width 64-bit lowering**… never a type whose width varies by platform such as C `long`"; `appendix-f-impl-defined.md:56-58` (§F.3 not-impl-defined) and `:66-90` (LP64/LLP64/ILP32 conformance note); fixture rows `appendix-e-conformance.md:49-50` incl. the `make ilp32` lane |
| 17 | float semantics (IEEE-754, NaN, signed zero) | CLOSED — pinned | `03-types.md:54-62` — "`float` is an **IEEE-754 binary64**… Division never traps: `0.0/0.0` is `NaN`… `NaN` is unordered — `NaN == NaN` is `false`"; `f32` binary32 at `:136`; the *textual* NaN/inf form is impl-defined, `appendix-f:45` |
| 18 | shift ≥ width / negative | CLOSED — pinned | `09-expressions.md:59-62` — "yields `0` (every bit is shifted out…)… a **negative** count… **aborts** at runtime… A negative *constant* count is rejected at compile time"; removed from the unspecified register, `appendix-f:19-21` |
| 19 | `to_int`/`to_float`/`to_uN` out-of-range | CLOSED — pinned (now *defined*, not unspecified) | `06-conversions.md:69-77` §8.5 — "a **`to_int` of a `float`/`f32` that is `NaN` or outside the signed 64-bit range aborts**… The sized integer/float conversions (`to_u8` … `to_i64`, `to_f32`) are **total**"; `appendix-f:19-22` records the removal; fixture `appendix-e:85` (`tests/abort/to_int_oob`). Supersedes §6a's "RESOLVED as unspecified". |
| 20 | FFI sized-int round-trip | CLOSED — pinned | `14-ffi.md:51-56` — "widening back with `to_int` **sign-extends** the signed types… and **zero-extends** the unsigned… `to_i32(-1)` → `-1`, `to_u8(-1)` → `255`, `to_i8(200)` → `-56`" |
| 21 | `range` with step 0 | CLOSED — pinned (now *defined*, and stricter than §6a) | `10-statements.md:58-60` — "A **zero step** never advances the counter, so it is a program error: a literal `0` step is **rejected at compile time**, and a step that evaluates to `0` at run time **aborts** (`tycho: range step is zero`)". §6a said "zero iterations"; the spec now rejects/aborts instead. |
| 22 | `char ± int` byte domain | CLOSED — pinned (**premise reversed vs. §6a**) | `03-types.md:75-76` — "`char ± int` has type `char`, and the result **wraps to a byte** (`0..255`, like `u8`) so the value never escapes the type's range"; `appendix-h-differences.md:24` H4 confirms the language was *changed* to wrap ("tightening campaign; `tests/char_byte`"). §6a's probe result (`'a' + 300` → 397, no masking) is stale. |

## D. Concurrency ordering & memory model

| # | Item | Verdict | Evidence |
|---|---|---|---|
| 23 | channel MPMC message ordering | CLOSED — pinned | `13-concurrency.md:118-125` — "a **single** sender's values are received in the order it sent them (FIFO). Among **concurrent** senders, the relative order… is whichever order those sends linearized — a race the language does not otherwise order. With multiple receivers, each value is delivered to exactly one receiver, in ticket order." |
| 24 | `select` arm fairness/priority | CLOSED — pinned | `13-concurrency.md:141-142` — "Ready arms are tried in **listed (lexical) order**, and the first ready arm is taken; `select` is **not fair**" |
| 25 | happens-before axioms | CLOSED — pinned | `13-concurrency.md:31-42` — three stated axioms: "**`spawn` → body**", "**`send` → `recv`**" ("a release store paired with an acquire load on the ring cell"), "**task → `wait`**" |
| 26 | `wait` re-entrancy from a non-spawner thread | CLOSED — pinned (**by construction**) | `13-concurrency.md:70-74` — "A `Task` handle cannot be copied, reassigned, stored in a container, captured by a closure, or passed as an argument (it is affine and non-storable). It therefore can never reach a thread other than the one that created it… waiting a task from another thread is **not expressible in the language**." |

## E. Type identity & generics edge cases

| # | Item | Verdict | Evidence |
|---|---|---|---|
| 27 | `empty$(T)` is not a builtin | CLOSED — pinned | `05-generics.md:106-109` — "There is **no** `empty$(T)` builtin: an `empty()` returning `[$T]` is an ordinary user-written generic, and `empty$(int)` is just `name$(…)` applied to it"; restated `16-builtins.md:197-199`; drift row `appendix-h:25` (H5) |
| 28 | function-value equality | CLOSED — pinned (**resolved differently than the item predicted**) | `03-types.md:274-281` — "**Function values are not comparable**… applying `==`/`!=` directly to a function value is a **compile error**". Identity comparison survives only *inside* an aggregate: "a function field within it compares by identity". So the item's framing ("equality is identity, not structural") is only half right; the direct case is a hard error. Fixture `appendix-e:57` (`reject/fn_eq`). |
| 29 | structural vs. nominal interning | CLOSED — pinned | `03-types.md:25-34` §5.1 — "**Nominal** — `struct`, `enum`, `newtype`, and `handle`… **Structural** — arrays, fixed-size arrays, tuples, maps, `Option`, `Result`, `soa`, function types, and the channel/task handle types… This distinction is normative" |
| 30 | `defaultable` excludes newtypes | CLOSED — pinned | `05-generics.md:53-58` — "`defaultable(T)` \| **exactly** `int`, `float`, `bool`, `string`"… "`defaultable` does **not** [consult the underlying type]: it is satisfied only by the four bare scalar types, so `defaultable` fails for a newtype even over a defaultable base — and therefore `zero$(X)` fails"; fixture `appendix-e:72` (`reject/zero_bad_type`) |
| 31 | newtype underlying set | CLOSED — pinned | `03-types.md:249-253` — "`U` MUST be one of: `int`, `float`, `string`, `bool`, an array type, a map type, or a struct type. It MUST NOT be an enum, a tuple, a sized numeric (`u32`/`u64`/`f32`), `char`, `bytes`, `ptr`, an `Option`/`Result`, a function type, a handle, or another newtype." Both the positive and the negative set, as the item asked. |
| 32 | `str(char)` is intentionally an error | **CLOSED — obsolete, premise reversed** | The language now *defines* it: `03-types.md:77-78` — "`str(char)` yields the one-byte **glyph** string (so a `char` interpolates in an f-string)"; `06-conversions.md:37` lists `char` in `str`'s accepted set. The deliberate asymmetry the spec now calls out is the *opposite* one: `03-types.md:290-292` — "`bool` is comparable and `str`-able but is not ordered. (`char` is comparable, ordered, and `str`-able…)". Item as written no longer describes the language. |

## F. Doc↔implementation drift

| # | Item | Verdict | Evidence |
|---|---|---|---|
| 33 | int-keyed map value restriction | CLOSED — pinned | `03-types.md:191` — "type `V` is **unrestricted** (any type)"; `12-aggregates.md:330` — "The legal **key types** `K` and the unrestricted **value type** `V`"; the stale diagnostic was deleted from source — `appendix-h:22` (H2) |
| 34 | f-string hole types | CLOSED — pinned | `01-lexical.md:284-285` — "Because the desugaring wraps each hole in `str(…)`, a hole expression MUST be of a type accepted by `str`"; `str`'s set (incl. `u32`/`u64`/`f32`, sized ints, `char`) at `06-conversions.md:37` + `:47-49`; drift row `appendix-h:23` (H3) |
| 35 | `builtins.md` incomplete catalog | CLOSED — pinned | `16-builtins.md:5` — "This chapter is the complete, normative [catalog]"; the reference page was back-ported, `appendix-h:26` (H6). Ch. 29 is now the catalog of record; `appendix-d-builtins.md:3` points at it. |
| 36 | no `assert`/`abort`/`panic` builtin | CLOSED — pinned | `16-builtins.md:267` — "There is **no** `assert`, `panic`, or `abort` builtin — no such name is [registered]"; provenance `:280-281` cites `register_builtins src/tychoc.c:3818-3849`; `die` fixture `appendix-e:178` |
| 37 | 6 undocumented corelib packages | CLOSED — pinned | All six are now normative sections of ch. 32/33: `18-library.md:254` §32.24 `net`, `:265` §32.25 `bignum`, `:274` §32.26 `decimal`, `:313` §33.3 `compress`, `:321` §33.4 `image`, `:330` §33.5 `tls`. Reference-doc half back-ported, `appendix-h:27` (H7). |

## G. Consequences of the two scope decisions

| # | Item | Verdict | Evidence |
|---|---|---|---|
| 38 | language↔corelib interface contract | CLOSED — pinned | `18-library.md:22-40` §31.1 the allocation contract — "it **allocates its results into the caller's arena** and returns independent values, never aliasing the caller's inputs… corelib introduces no hidden global state, no shared mutable buffers, and no lifetime escaping the caller's arena"; the one exception (C-owned opaque `ptr` handles) at `:36-40`; realization kinds §31.3 `:63`; `deps`/shim mechanics `15-program.md:296-316` §28.6 |
| 39 | conformance tiers (core vs. extended) | CLOSED — pinned | `00-conventions.md:52-75` §1.3; `18-library.md:42-61` §31.2 — "A conforming implementation **MUST** provide every core-tier package… An implementation **MAY** omit the extended tier and still conform at the core tier. A program that imports an absent extended package **MUST** be diagnosed before producing an executable"; mechanical tier test at `:59-61`; `15-program.md:316` |

---

## Significant findings

1. **#12 — CLOSED 2026-07-24 by probe.** As audited, §13.4 pinned only the
   assignment-place *index* left-to-right, and the **receiver** leg (a
   side-effecting `p()` in `p().x[i] = e`) fell through both §13.4 and
   `appendix-f:14`'s wording ("a call's arguments or a binary operator's
   operands"). The probe showed the premise was narrower than assumed: **a call
   is never a legal place receiver.** Both compilers reject `p().x = e()` and
   `p()[i()] = e()`, so a place spine is rooted at a variable and carries no side
   effect to order. The remaining legs — index and subscript-argument
   sub-expressions — are evaluated left-to-right in source order, all before the
   RHS, identically on tychoc and tychoc0. Pinned at `09-expressions.md:87-95`;
   fixture `tests/place_eval_order.ty`; row `appendix-e-conformance.md:115`.
   Appendix F did not need widening: nothing was left unspecified.

2. **Three items closed with the opposite rule to what §6 assumed** — the
   punch-list text is now actively misleading if read alone.
   - **#22** `char ± int`: §6a probed "no byte-masking, `'a' + 300` → 397". The
     language was subsequently *changed*; `03-types.md:75-76` now requires
     wrap-to-byte (`0..255`), and `appendix-h:24` records H4 as "**Correct**".
   - **#19** out-of-range `to_int`: §6a resolved it as *unspecified*;
     `06-conversions.md:69-77` now makes it a **defined abort**, and
     `appendix-f:19-22` explicitly removes it from the register.
   - **#21** `range` step 0: §6a resolved "zero iterations"; `10-statements.md:58-60`
     now **rejects** a literal `0` at compile time and **aborts** at run time.
   - (**#32** likewise: the item asserted `str(char)` is intentionally an error;
     it is now a defined one-byte glyph.)

3. **Group A closed structurally rather than by reconciliation.** The plan's
   stated resolution for #7 was "extract from `src/tychoc.c`; then either fix
   tree-sitter to match or mark it explicitly non-normative in Ch 3." The second
   branch was taken and taken thoroughly: `01-lexical.md:298-319` §3.10 lists all
   seven divergences by name and closes with "the tree-sitter grammar has no
   normative force", backed by `00-conventions.md:161-165`. Items 1–7 are each
   individually written into that section, which is a stronger outcome than
   "resolved somewhere".

4. **#15 (spec) and the header's item 3 (compiler) are genuinely separate and
   both are done.** The spec states the root-variable may-overlap rule at
   `07-memory-model.md:169-175` including the `&a[i]`/`&a[j]` and `&a.x`/`&a.y`
   cases the item asked about; the `tychoc0` fail-open fix is a distinct fact
   recorded in the same section at `:177-180` and locked by
   `tests/reject/inout_alias.ty`. The spec would state the rule even if the
   compiler had not been fixed.

## Cross-checks run

- `make spec-check` — Appendix E citations resolve.
- `make check-links` — relative links in the changed docs resolve.
- `appendix-e-conformance.md` carries 81 clause→fixture rows; every item above
  that names a fixture was matched against a real row.
