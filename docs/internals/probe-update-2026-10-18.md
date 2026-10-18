# Probe: exclusive update callbacks — 2026-10-18

Model: Muse Spark 1.3 Contributor (same family as the maintainer's assistant;
the external-model route was unavailable — the `opencode` API key is expired
and `openai-codex` is rate-limited — so the probe ran as a fresh agent with no
compiler, internals, RFCs, or prior findings in context, in a stripped tree).
Host: Linux x86_64. Compiler: `tychoc 0.8.7`.

Program: `update.ty` (186 lines, single file, no imports; thrown away after
this record). A social graph as one owned value with an `update_person`
confined-update operation (callback receives the node by value, caller writes
back through a `subscript` place), an atomic `merge_people` multi-node op,
and attempted escapes: returning the node, stashing it in an outer variable,
capturing it in a closure, holding two overlapping updates. MUST-use
coverage: `inout` places, update callbacks (named + inline lambda), custom
subscripts (`person`, `friends_of`), a generic `find_first` over `[$T]`,
`Option`/`Result` with `or_return`/`or_else`, file-free deterministic demo.

## What went right

Value semantics does the confining work for free: every escape the probe
could write legally just copies — a returned node, a captured node, and the
callback's parameter are all independent values. The escape table (LOG §3):
stashed copy age=999 vs graph 30, closure snapshot vs outer, overlapping
updates refused statically with the overlapping-mutability diagnostic.
`or_return`/`or_else` chains make the atomic merge pleasant (three lines of
validation, straight-line mutation, byte-identical graph after a failed
merge). Diagnostics name the rule and the fix.

## Findings (adjudicated against `main`, all reproduced)

1. **No `inout` callbacks.** A function with an `inout` parameter cannot be a
   first-class value and lambdas cannot take one (`'bump' has an inout
   parameter, so it can't be a function value`; spec §15.4). The brief's
   in-place-callback shape is inexpressible; the probe reorganized to a pure
   transformer plus caller writeback. Confinement is preserved (arguably
   strengthened), but the borrow-passing shape does not exist. This is the
   probe round's load-bearing representational finding.
2. **Two soundness defects in `tychoc1`'s subscript handling, both fixed as
   FRICTION 205.** An `inout` push through an array-typed projection grew
   into the caller's freed scope (use after free → garbage `[1, 91]` →
   OOM): the call-site arena came from `_ownerof` of the `Addr(Call)`
   wrapper, whose root is unresolvable. Whole-value stores through a
   subscript place emitted a raw C assignment: no copy into the owner's arena
   (the rename printed a previous error string) and no recycle of the
   displaced value (leak). The reference compiler was correct throughout;
   minimized repros are 16 lines and in the FRICTION entry.
3. Array-typed subscript projections are second-class places: `push` and
   element assign through one are refused (`must be an array variable or
   field`) while struct-field writes and `&`-as-`inout`-argument work.
   Documented only by the struct-field example in `subscripts.md`; the
   reference page needs an array-projection row.
4. Lambdas are single expressions (documented); multi-step mutating callbacks
   must be named. Borrowed heap struct parameters mutate a private copy
   silently while borrowed array parameters refuse element writes -- the
   asymmetry is real (copy vs borrow by type) but discovered by accident,
   not from the docs, which say the callee "may read but not mutate."
5. Minor: `[][int]` vs `[]int` empty-nested-literal spelling is shown only
   in passing; generic higher-order syntax differs between `iter.ty`
   (`fn($T) -> $U`) and `generics.md` (bare `T` on use) with no statement
   of which is canonical.

## Verification

Rebuilt clean from a scratch directory under `./tychoc` and `./tychoc1`:
byte-identical output; ASan+UBSan build identical, no findings. Pre-fix, the
self-hosted build printed garbage integers, a corrupted name, then died with
`tycho: out of memory`; post-fix all three agree. `tests/subscript.ty`
gains the array-projection growth and heap-member writeback legs.
