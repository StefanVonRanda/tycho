# Rewrite the bootstrap compiler in Tycho (`compiler/`)

## Goal

A second Tycho compiler, written in Tycho, targeting the whole locked language —
not a subset. Seven packages under `compiler/`: `lex parse ast types lower emit
driver`, entry `compiler/main.ty`, `make tychoc1` -> `./tychoc1`.
`runtime/tycho_rt.c` is unchanged and stays C; `tychoc1` emits against the same
runtime ABI. Done when `TYCHOC=./tychoc1 make test` is green at the same count
as `./tychoc`, and the fixpoint holds — `tychoc1` built by tychoc, then
self-built twice, the two emitted `.c` identical.

## Pre-flight

- **Worst case:** `tychoc1` compiles the corpus but emits subtly different C —
  a wrong arena parent, a missed deep-copy elision — so `make test` is green
  while generated programs leak or alias. The fixpoint leg does not catch this;
  only the golden-per-fixture comparison does.
- **Reversibility:** total. `compiler/` is a new directory, `src/tychoc.c` is
  untouched, and `make tychoc1` is a new target. Deleting `compiler/` restores
  the tree exactly.
- **Verified:** `compiler/` is empty and untracked (`git ls-files compiler/`
  returns nothing; `603d8fbd` removed the old one wholesale).
  `src/tychoc.c` is 14,509 lines / 471 static functions.
  The emit target is `runtime/tycho_rt.c` embedded verbatim, then
  `void h_<fn>(Arena *_parent)` with `arena_child` per scope (`Makefile:25`,
  `Makefile:17-21`). **60** `run.sh` files set `TYCHOC=` — the "55" in the old
  plan was stale — and 3 sites call `./tychoc` inline, which an override would
  silently miss: `bench/transpile/run.sh:6`, `tools/tycho-ar/run.sh:266`,
  `tools/tycho-ar/run.sh:267`.
- **Assuming — bootstrap story:** `src/tychoc.c` stays the bootstrap forever;
  `tychoc1` is built by `./tychoc` and no generated `.c` is committed. This is
  the reversible default and does not foreclose committing one later. If the
  release story needs a self-contained clone, this decision flips and Phase 10
  changes.
- **Assuming — runtime source:** `tychoc1` reads `runtime/tycho_rt.c` from disk
  rather than replicating the `$(EMBED)` awk step into a generated header. Risk
  if wrong: `tychoc1` is not relocatable outside the repo. Phase 1 settles it.
- **Scale, stated plainly:** phases 5–8 are each large. This is not a
  single-session task and the plan does not pretend otherwise.

## Phases

- [ ] **Phase 2b-1 — `src/tychoc.c` has the unclosed-bracket recovery block twice**
  - Found while porting item 3. The `decl_kw` recovery appears at
    `src/tychoc.c:527` and again at `src/tychoc.c:552`, character-identical
    apart from a comment. The second is DEAD: the first sets `bracket_depth = 0`,
    so the second's `bracket_depth > 0` guard can never hold. `grep -c 'decl_kw\[\] = '`
    is 2.
  - Scope: `src/tychoc.c` only. Delete one block.
  - Done when: one block remains and the two-unclosed-bracket probe prints the
    same two diagnostics it prints today.
  - Verify: `make test`, and re-anchor citations (`scripts/reanchor_citations.py`)
    — every `src/tychoc.c` edit moves 60-140 refs.

- [ ] **Phase 6c — the four rule families `--typecheck` still misses**
  - Scope: `compiler/types/`, and for item 1 ALSO `compiler/parse/` +
    `compiler/ast/`, which Phase 6b's scope lock excluded.
    1. **Two rules the AST cannot express.** `compiler/parse/parse.ty@_wheres`
       parses a `where T: int | string` type SET and pushes only `T`, discarding
       the members, so `typeset_notin` cannot be scored; and an f-string's
       interpolations are kept as raw text, so `len_scalar` is invisible. Both
       need an AST change first.
    2. **The concurrency rules.** Eleven `tests/conc/reject/` fixtures: what
       `spawn` may be applied to (a named user fn, not a builtin, not a closure,
       no `inout` parameters), `parallel for`'s reduction shape and its
       break/return bans, capturing a task, and `wait`'s argument. Each is a
       statement SHAPE, not a type rule.
    3. **The match-ARM name rules.** `foreign_variant_bare`, `foreign_variant_is`
       and `result_arm_mangled` — a variant of another package's enum must be
       written qualified, and a Result's arms are `Ok`/`Err`/`_`.
    4. **Pending-type (B-3) grounding**, a subsystem of its own:
       `infer_bare_empty`, `infer_use_before_ground`,
       `void_grounds_pending_push`. Plus `generic_recur_grow`, which needs the
       generic template BODY instantiated rather than only its signature, and the
       UFCS method call that leaves 22 programs ungrounded at leg12.
  - Done when: `compiler/run.sh`'s KNOWN list and `compiler/verdict_diff.py`'s
    are both EMPTY, and leg2c reads `rejected=337 missed=0`.
  - Verify: leg2c, leg10 and leg11 unmoved at 0; `leg2b wrongly-rejected=0`;
    each family gets a probe with an accepting twin, as leg13 does.
  - Do NOT run: `make test`, `make ci`. The lane is `make parse-check`.

- [ ] **Phase 8c — `_hashable` is depth-capped at 8 and fails closed** (found in
  Phase 8, out of scope there)
  - `compiler/emit/emit.ty@_hashable_d` stops at depth 8 and returns false,
    because a struct whose field is an array OF ITSELF is a legal recursive
    shape and recursing on it never terminates (it OOM'd two fixtures before the
    cap). The cap is a guess, not a proof: a legitimately hashable struct nested
    deeper than 8 loses its generated hash and the map that keys on it stops
    compiling, with no diagnostic naming the cap.
  - Scope: replace the depth cap with a visited set over struct/tuple ids.
  - Done when: the recursive shapes still terminate and no legal depth is cut.
  - Verify: `sh scripts/tychoc1_check.sh` unmoved, plus a probe nesting a
    hashable struct 12 deep and keying a map on it.

- [ ] **Phase 3c — `scripts/check_goldens.py` carried a stale NO_GOLDEN entry**
  - Found while wiring Phase 3b: `NO_GOLDEN` still excused `compiler/run.sh`
    ("a differential, not a golden") for a file `603d8fbd` deleted wholesale.
    The entry was removed in Phase 3b's commit, because the new lane could not
    go green under it — the gate itself printed "is in NO_GOLDEN but the scan
    found a golden -- remove the entry", which is the gate working.
  - Scope: audit the other 19 `NO_GOLDEN` entries for runners that no longer
    exist. Nothing else in the tree checks that list against `git ls-files`.
  - Verify: `make goldens-check` (~0.08 s) and nothing else.

- [ ] **Phase 5d — `'X' is already defined` says more than `src/tychoc.c` does**
  (found in Phase 5b, out of scope there: 5b's contract was the LINE, not the
  message). Where a top-level name collides, `compiler/types/resolve.ty` appends
  `(<kind> in <file>)` and `src/tychoc.c` does not:

  ```
  tychoc :  tests/reject/dup_struct_enum.ty:3: error: 'X' is already defined
  tychoc1:  tests/reject/dup_struct_enum.ty:3: error: 'X' is already defined (struct in tests/reject/dup_struct_enum.ty)
  ```

  `src/tychoc.c@die_dup_proc` does have a two-file form, but it fires only when
  the other declaration is in a DIFFERENT file of the same package, and it is
  worded differently. Phase 9 pins message text byte for byte and owns this;
  recorded here so it is not rediscovered.
  - Done when: the seven `'%s' is already defined` sites agree with
    `src/tychoc.c` on text as well as line, including the cross-file form.
  - Verify: extend leg8 in `compiler/verdict_diff.py` to compare the MESSAGE for
    the NAME class, not only `file:line`. State the new agree count.
  - Do NOT run: `make test`.

- [ ] **`no 'main' procedure` names the wrong file, in `src/tychoc.c`**
  (found in Phase 5c, out of its scope). The check is
  `src/tychoc.c:9267`, an `fprintf` of `g_srcname` — which parsing left pointing
  at the LAST file of the merge order, not the entry the user named. Measured
  over all 100 files in the tree tychoc refuses this way, 2026-08-23: 99 name
  their package's alphabetically-last sibling. `tools/tycho-sheet/cell/cell.ty`
  is reported as `tools/tycho-sheet/cell/fold.ty`, a file the reader never
  mentioned and which is not missing anything.
  - Done when: the message names the entry file, and `compiler/types/resolve.ty`
    is changed back to `p.entry_path` in the same commit — tychoc1 reproduces the
    wart today, deliberately, because leg8 compares the two.
  - Verify: `make parse-check` (leg8 must stay at 151/151), plus `make test`,
    which owns `tests/reject/no_main.ty`'s golden.
  - Do NOT run: `make ci`.

- [ ] **f-string interpolations are still raw text, so nothing inside one is
  checked** (found in Phase 6a, out of scope there). `ast.FStrLit` keeps the body
  as written and `compiler/types/resolve.ty@_fstring_uses` only scans it for
  identifiers to mark used. `src/tychoc.c:2712` parses each `{...}` into a real
  expression. The visible cost today is one fixture:
  `tests/reject/len_scalar.ty` is `print(f"{len(x)}")` at an int `x`, and it is
  one of the four KNOWN misses in `compiler/run.sh`'s leg2c.
  - Scope: parse each interpolation into `ast.Expr` and hang them off the node.
  - Done when: `len_scalar.ty` leaves `KNOWN_TYPE_MISS` in both
    `compiler/run.sh` and `compiler/verdict_diff.py`.
  - Verify: `make parse-check`. Expect leg3's census to MOVE (the interpolations
    become real nodes) and say by how much; both other censuses should not.
  - Do NOT run: `make test`. Nothing here reaches `src/tychoc.c`.

## Phase: close the compile-speed gap to ./tychoc

Scope: tychoc1 compiles 2.0-4.5x slower than ./tychoc (best-of-three, --emit-c:
compiler/main.ty 327ms vs 90ms, tycho-sheet 45 vs 10, mandelbrot 4 vs 2). The
cheap wins are taken (86dc4329, 822ad009, cb133eb0, 5d25b6d0: 663ms -> 327ms on
the self-compile). What is left is not a hot spot -- callgrind shows no caller
above 3.1% and nothing in the type checker above 0.25%, with ~48% of
instructions in arena_alloc + memcpy.

Two measured causes, both architectural:
  - 1,341,342 temp arenas per tycho-db compile, 1,134,759 of which never
    allocate: one per statement, from the codegen model.
  - Value-semantics AST: constructing a node copies its children, and every
    walker that binds a payload copies the subtree (22,032 Expr deep copies in
    one tycho-db typecheck). tychoc1 also runs ~2x the passes ./tychoc does.

PROGRESS. Two self-host miscompiles are fixed (d313844e, 39593d65 -- a
container literal's elements and a returned slice were still pinned to the
dying scope) and the self-built compiler now passes the whole 752-fixture
corpus. The match-arm payload borrow landed (1730220d). Ratio on
compiler/main.ty is ~3.1x, from 5.0x.

The two-stage build is ON (e4e0abbe) and the blocker is fixed (82aae400):
to_str/to_bool emit their argument unchanged, so their result is that place and
retaining it must copy -- core:io was handing back a pointer into the frame it
had just freed. Shipped compiler 341 ms -> 279 ms.

MEASURE WITH CALLGRIND, NOT THE CLOCK. This machine drifts +/-5% between runs,
which is the size of most of these wins; `valgrind --tool=callgrind` gives a
deterministic instruction count.

WHERE IT STANDS on compiler/main.ty (tychoc 89-91 ms alongside):

    5.0x   at the start of this work
    3.8x   341 ms
    3.1x   279 ms   two-stage bootstrap (e4e0abbe), unblocked by 82aae400
    2.8x   254 ms   concat chains flattened (c7f6ed37)
    2.7x   242 ms   arena bump inlined (cb76f804)
    2.6x   230 ms   same-arena strings and ENUMS shared (f1d14147 and after)
    2.4x   217 ms   push keeps a temporary already in the array's arena
    2.2x   200 ms   destructuring a call result, for-in snapshot, size-class guard
    2.0x   182 ms   arm-mutation test narrowed (05d0a710), after 8fc08efc
    1.95x  172 ms   string equality settles on the lengths (aa5d0909)
    1.87x  166 ms   short string copies move inline (23e2d69e)
    1.84x  164 ms   the short move shared by concat/substr (e1b2809b)
    1.81x  161 ms   the lexer stamps a token's file at creation (94add001)
    1.26x  113 ms   for-in snapshots only a collection the body WRITES (5a8df4e7)
    1.24x  110 ms   the scope-elision test scans once (5d4122c1)
    1.21x  108 ms   the parser compares a token's kind in place (a6ecc1ba)
    1.19x  106 ms   string equality checks one byte first (ad604f7a)
    1.18x  106 ms   the hottest token sites build their Tok inline (cc3a9d03)
    1.18x  105 ms   an ENUM out of an inout parameter returns uncopied (d99bc633)
    1.03x   94 ms   the binary parse levels write through an out parameter (622c4951)
    1.00x   92 ms   primary/postfix/unary and every expr() call site too (6588f1b9)
    0.99x   91 ms   unit and each function built into the slot that holds it (c89ec1bd)
    0.99x   91 ms   the Program written into the caller's own, not returned twice (0aa7fc11)
    0.98x   89 ms   short string equality without memcmp (fae3bfc4)

GOAL MET. 21 interleaved rounds, one session: ./tychoc min 91.0 / median 92.3 /
mean 96.9 ms, ./tychoc1 min 89.0 / median 91.2 / mean 94.8 -- 0.977 / 0.988 /
0.979. All three statistics agree, which the 1.00x row's did not (1.004 / 0.997
/ 0.994) and is why that row is not the finish line. Instructions 850.0e6 ->
789.6e6; tychoc still executes only 681.6e6, so the parity is bought by cheaper
instructions, not fewer.

THE RULE, in its final form. An out parameter pays where ONE tree is built up
and would otherwise be re-homed at every level; it LOSES where many small
values accumulate, because each push then pays an individual retention copy
instead of one bulk copy at the return. Measured both ways: the binary levels
and primary/postfix/unary win, tokenize_named's token array loses (830.8e6 ->
833.6e6), unary as a pass-through is flat, and postfix on top of a
tuple-returning primary is worse. The same rule read the other way is what the
slot-building wins are: push a placeholder, let the callee build in place.

A SHAPE WORTH KNOWING, since three of the wins above are instances of it:
passing a value through a small helper LOSES ITS ARENA IDENTITY and costs a
copy. A literal handed to _tk is a place once it is a parameter; a token read by
_expect is copied whole to look at two fields; `return cs.vcn[i]` duplicates a
field of a caller-owned struct into the caller's own arena. Building or
comparing in place at the call site is what removes them.

The general form of the last one does NOT work, and the reason is worth keeping:
teaching _shares that a place rooted in an INOUT parameter may be returned
without a copy -- its arena is the caller's, which is what _parent names -- is
right about LIFETIME and wrong about MUTATION. It reddens 15 fixtures for ~1%,
and const_local fails with `tycho: out of memory`, not a wrong value: the shared
string is a FIELD of the inout struct that its owner appends to IN PLACE, and
the in-place append grows a buffer the returned value still aliases.
_acc_rooted cannot see that -- it tests the root LOCAL, and here the accumulator
is cs.ccode[i]. Shipped for ENUMS only (d99bc633), which have no in-place write
at all: green everywhere, instructions flat because it fires rarely, 105 ms
against 106 over two runs of eleven each way.

    instructions   2.820e9 -> 1.016e9

WHAT IS LEFT, and why the two clean ways out are both measured losses: half the
remaining tycho_str_copy calls are inside AST deep copies, and those happen at
the RETURN boundary -- the parser handing a node up through its recursion. The
two ways to remove them are promotion (build the local in the caller's arena;
removes ZERO copies here, the rule never fires on this code shape) and adoption
(give the parent the scope's blocks; green and -4% instructions, +12% WALL).
Both are written up below with their numbers.

Also ruled out this round, each measured:
  - inlining arena_free's empty-arena case (1.87M of 2.75M arenas are freed
    empty) -- 1.016e9 -> 1.086e9 and 106 -> 111 ms. The call-site bloat costs
    more than the call.
  - raising the emitted C's optimisation level: it is ALREADY -O3
    (driver.ty@build) while ./tychoc itself is built -O2, so tychoc1 is the more
    aggressively compiled of the two. There is no free win there.
  - a cheaper map hash (siphash13 is 2.9%): rejected rather than measured. The
    seed is randomised per process on purpose and a weaker hash reopens HashDoS
    for 2%.

THE ONE THAT MATTERED: for-in was snapshotting its collection defensively, so
every AST walker in every pass deep-copied the child list AND every node under
it on entry. The snapshot only exists so a body can push to the array it walks;
the same mutation test the match arm already used decides it. -32% on its own.

THE RULE that did most of it: a value already in the destination arena is stored
rather than copied, when nothing can write through it -- strings and enums are
immutable, and a value built by a CALL was built in this arena to begin with,
since the call is handed cs.arena as its _parent. Arrays and maps are excluded
(push and element assignment write in place), and so is a place rooted in a live
string ACCUMULATOR (tests/value_semantics.ty).

STANDING: 1.15-1.18x. The gap is ONE problem, and it is scoped: 1.9M of the
4.0M string copies in a self-compile sit inside AST deep copies, and every one
of those crosses an arena boundary at a RETURN. That is ~17% of the profile,
which is the whole remaining gap. Four routes to it are closed with numbers:

    promotion / escape analysis   11 attempts; DEAD, and not for the reason
                                  everything above says -- see the last one
    scope adoption                green, -4% instructions, +12% WALL
    push-value arena              190 fixtures, then 254
    alias scope to caller's arena 19 fixtures; the accumulator guard made it 35

THE EIGHTH PROMOTION ATTEMPT WORKS, IS GREEN, AND IS STILL A LOSS -- and what
it measures is the useful part. Three gaps had to be closed:
  - a name declared by `l, i := mul(...)` (an MDecl) was never a candidate, and
    that is how every recursive-descent function declares the local it returns;
  - a read inside `l = ast.Binop(ln, op, [l, r])` -- the tree accumulator -- was
    counted as "outside a return", which disqualified exactly the target;
  - or_return pinned its subject to &_scope, so a promoted declaration took its
    Ok payload out of a scope the return then freed (tests/calc died on a
    non-exhaustive match, reading a tag out of released memory). Moving the
    subject to the current arena unconditionally leaks the statement's own
    scratch on the early-return path -- ASan caught one block in
    tests/result_void -- so it moves only for a promoted declaration.

With all three: 752/752, parse-check, corelib green, and

    tycho_str_copy   4,038,215 -> 3,707,039   (-331k, -8%)
    instructions     0.996e9 -> 1.020e9       (+2.4%)

The rule fires and removes copies; the census costs about three times what they
save. Two more attempts to make the census cheap: folding its four walks into
one (0.882e9 -> 0.947e9) and then removing the 4-tuple its recursion returned,
which allocates per node (-> 0.943e9). Neither helps, and together they measure
the thing worth knowing: ONE FULL AST WALK COSTS ABOUT 30M INSTRUCTIONS HERE,
3.4% of a whole compile. Any per-function analysis in emit starts 60M in debt.
That is the number a future attempt has to beat, and it is why the answer is
not a better census. AND THE ASSUMPTION UNDERNEATH ALL EIGHT ATTEMPTS IS WRONG: the AST copies
did NOT move (tycho_copy_E_ast__Expr 1,488,864 -> 1,484,873). The parser's
`return (l, i)` is not where they come from. Tracing the callers again puts the
roots in tycho_arr_K3_copy -- STATEMENT BODY arrays -- which this rule excludes,
because promoting arrays reddened every pkg_* fixture in an earlier attempt.
That is where a ninth attempt should start, and it should confirm the root by
call count BEFORE writing any analysis.

OUT PARAMETERS PAY WHERE A TREE ACCUMULATES, AND NOWHERE ELSE. The binary parse
levels (mul/add/isexp/cmp/notexp/andexp/expr) each returned their node, which
copies it into the caller's arena at EVERY precedence step -- O(depth x size)
for a deep expression. Writing through an `out: inout ast.Expr` builds the tree
where it will live: -3.3%, 95 -> 93 ms (622c4951).

The same transformation applied to levels that do NOT accumulate is a LOSS:
unary is a pass-through (0.850e9 -> 0.851e9) and postfix, which does accumulate
but whose base comes from primary through a shim, is worse still (-> 0.865e9,
93 -> 95 ms). The rule is: convert a level only where the node it returns is
built up across iterations from what the level below produced.

AN ARENA'S MEMORY IS NOT FRAME-SCOPED, and that is worth knowing before anyone
tries to make it so. Compiling compiler/main.ty asks the pool for 842k blocks to
bump 285 MiB -- about 350 bytes per block acquired -- so most arenas take a
64 KiB block, use a few hundred bytes and hand it back. Giving Arena a 256-byte
inline buffer (on the C stack for a scope arena) removes almost all of that
traffic and reddens 3 fixtures with ASan reporting STACK-USE-AFTER-RETURN. The
reason is deliberate: to_under aliases its argument, ./tychoc does too and
tests/newtype_agg records that answer, so a value can legitimately outlive the
frame whose arena holds it. Arena storage must stay off the stack.

THE ELEVENTH ATTEMPT SETTLES IT, AND CORRECTS THE DIAGNOSIS ABOVE. Promote
EVERY top-level local of a function that returns something heap -- no census, no
walk, no analysis cost whatsoever, just a mark. Green everywhere, and:

    instructions   0.879e9 -> 0.942e9   (+7%)
    allocations    7.59M -> 8.84M, bump 285 MiB -> 315 MiB
    wall           95 ms -> 107 ms

Allocations go UP. So the cost was never the census: building locals in the
caller's arena is itself a pessimisation here, because a promoted local's own
assignments then copy INTO _parent and nothing is freed until the caller
returns. Every earlier attempt blamed its walk; the walk was not the problem.
The family is dead.

ALSO MEASURED, both negative:
  - returning a PARAMETER-rooted place without copying, on the theory that the
    per-statement scratch arena is what makes a parameter's lifetime unknowable.
    Removing that arena (a statement's expression evaluated in &_scope) is GREEN
    on its own -- and costs 2.6%, 0.879e9 -> 0.902e9, 95 -> 97 ms: fewer arenas
    but worse locality. And the sharing it was meant to enable still reddens 23
    fixtures, so a parameter's buffer has other ways of being shorter-lived than
    the _parent it returns into (a loop's scratch arena resets, for one).

The FBIP recycling that aliasing would disturb is worth 2.5% on its own
(measured by making arena_recycle a no-op: 0.996e9 -> 1.021e9, 105 -> 109 ms),
so it cannot simply be dropped to make room.

What is left is not a patch. It is a decision about how the compiler assigns
arenas -- whether a callee can be told to build its result where the caller
wants it, rather than building it locally and copying. Everything above is an
attempt to infer that after the fact, and all four inferences are unsound,
too expensive, or both.

WHERE THE TIME IS BY PHASE, which reframes all of the above -- measured with the
compiler's own front-end modes on compiler/main.ty:

    --parse       2 ms   (one file)
    --resolve    78 ms   (load: read + lex + parse EVERY file, then resolve)
    --typecheck  79 ms   (+1)
    --emit-c    105 ms   (+26)

The BACK END IS 26 ms of 105. Everything above was aimed at emit's copies, and
emit is a quarter of the compile; the front end is three times it. A callgrind
of --resolve alone puts tycho_str_copy at 15.1%, tycho_str_eq 6.4%,
arena_alloc_i 5.9%, the Expr copy 5.4%, and -- the one that stands out --
tycho_arr_K17_copy at 5.1%: the whole TOKEN ARRAY, deep-copied once per file
with all four strings of every token, at tokenize_named's return.

That last one looks like the next lever and is NOT: rewriting tokenize_named to
fill an `inout` array moves the copy rather than removing it, because push into
an array owned by another arena copies each element anyway. It only pays if the
token is BUILT in the destination arena -- the push-value-arena change, now
tried TWICE and worse the second time: 190 fixtures the first way, 254 the
second (computing the target's owner before evaluating the value, so a pending
`?A` element type and a nested place both read the wrong arena). Do not try a
third shape of it without first understanding what _ownerof answers for a push
target that is a field, an element, or an inout parameter.

PROFILE NOW (exclusive): tycho_str_copy 13.1%, memcpy 11.7%, arena_alloc_i
7.4%, tycho_copy_E_ast__Expr 7.0%, the Expr-array copy 6.7%, memcmp 5.6%.

WHERE THE REST OF THE COPYING IS, traced through the call graph rather than
guessed: 4.6M of the 7.1M string copies are inside tycho_copy_E_ast__Expr; those
Expr copies come from tycho_arr_K0_copy; and 687k of THOSE come from
tycho_copy_E_ast__Stmt, which comes from tycho_arr_K3_copy -- copying an
[ast.Stmt] BODY. So the chain is: a body array is retained (parse builds `body`
then wraps it in ast.IfS), arrays are mutable so the sharing rule cannot touch
it, and the copy takes every statement, every expression and every string under
it. Eliding that needs the source array to be built in the destination arena,
which is the escape analysis -- measured four times, a loss every time.

ALSO RULED OUT: returning a shared singleton for an EMPTY string copy. Sound --
there are no bytes to alias and every write path reallocates -- but it measured
+1.6% (1.642e9 -> 1.669e9): the branch on every copy costs more than the
allocations it saves, so empty strings are not as common in copies as the AST's
shape suggests. Reverted.

RULED OUT, each measured against the baseline:
  - the full return-only escape analysis (src/tychoc.c@collect_ret_alias) --
    +7.7%. The name census is linear and still costs more than the copies it
    removes, and promoting locals to _parent defers every free.
  - the cheap half of it (promote a local returned BY NAME) -- the SELF-BUILT
    compiler fails 24 fixtures. Something else in this codegen assumes a local
    lives in _scope; the string accumulator's in-place append is the first
    suspect, since it needs its buffer to be the last allocation in its arena.
Both reverted. Do not re-port them without first finding what the promotion
breaks.
  - evaluating push()'s value directly in the ARRAY's arena (instead of the
    scope, then copying) -- 190 fixtures. The narrow version that only drops the
    copy when both are already in &_scope is what shipped.
  - (This one was WRONG and is now shipped as 9fbe9f35: widening the arena fast
    path to test the size CLASS rather than the bucket table's existence was
    dismissed on a wall-clock reading of 221 ms against 217, inside this
    machine's drift. Measured in instructions it removes 3.92M slow-path calls
    and 2%. Do not judge a change of this size by the clock.)
  - the return-only escape analysis, SECOND attempt, this time with a bounded
    census (candidates come from the return statements, only those names are
    counted over the body -- so the +7.7% of the first attempt is gone). It
    reddens 32 fixtures: every pkg_* program plus tests/subscript, and subscript
    fails with a WRONG VALUE (r=11,31,30 against 11,22,30), not a crash. So the
    promotion introduces aliasing somewhere the "every read is inside a return"
    rule believes it has excluded. Worth resuming from that fixture: it is small
    and it is the only non-package one.

    SEVENTH attempt, aimed squarely at why the rule never fires. The parser
    builds its tree with `l = ast.Binop(ln, op, [l, r])`, and that read of l is
    not inside a return, so "every read is inside a return" excludes it. Extended
    the rule to count a read inside an assignment BACK TO THE SAME NAME as
    return-only. It still does not pay -- 0.996e9 -> 1.019e9, +2% -- and it
    reddens tests/calc, whose own parser is that exact shape: the compiled
    program dies with "non-exhaustive match", so the promoted local holds the
    wrong value at run time. Seven attempts, seven negatives.

    SIXTH attempt, and the one that explains all the others. Counting the calls
    it removes: tycho_str_copy 4,398,272 without it and 4,473,320 WITH it;
    tycho_copy_E_ast__Expr 578,719 -> 586,274. It removes NOTHING -- the counts
    go up, by the analysis's own allocations. The rule never fires on this
    codebase, and the shape says why:

        l, i := mul(toks, i)
        l = ast.Binop(ln, op, [l, r])
        return (l, i)

    `l` is read in the Binop construction, which is not inside a return, so
    "every read is inside a return" excludes exactly the pattern the whole idea
    was aimed at. Dropping the census entirely (promote any local a return
    mentions) is still +1.4% AND reddens tests/calc. Six measurements: this line
    is closed, and it is closed on MECHANISM, not on cost.

    FOURTH attempt, after 05d0a710 made every AST walker cheap -- the reason
    the earlier ones were assumed to have failed. Still a loss: 1.765e9 ->
    1.976e9, 182 ms -> 199 ms. Four measurements now say the same thing, and the
    walkers being cheap does not change it. Stop trying this in emit.

    THIRD attempt, restricted to ENUM locals (arrays and maps still copy, which
    is what the pkg_* failures were about): GREEN everywhere -- 752/752,
    parse-check, corelib -- and 17% SLOWER. 1.924e9 -> 2.246e9, 200 ms -> 233 ms.

    So the conclusion is not "the rule is wrong" but "the analysis cannot live
    here". Three variants have now been measured: a full per-name census
    (+7.7%), a bounded one restricted to the names the returns mention (+17%
    with the enum restriction), and move-on-last-use (+8%). Every one of them
    costs more in emit than the copies it removes, because emit runs the walk
    per function on every compile.

    WHERE THE COPYING IS, so the next attempt starts from evidence: 5.5M of the
    8.2M string copies in a self-compile are inside tycho_copy_E_ast__Expr, and
    5.5M of the 5.9M AST copies come from tycho_arr_K0_copy -- an Expr ARRAY
    being deep-copied. That is the parser returning `(node, i)` up through its
    recursion, re-homing the subtree at every level.

    THE WAY OUT, if anyone takes this up: compute the escape set in a pass that
    ALREADY walks the body -- resolve or tcheck -- and hand emit a set to look
    up. Every failure here has been the cost of the walk, never the rule.

## Tried and failed: ALIAS the scope to the caller's arena

The cheapest possible form of adoption: for a function that returns a PLACE --
which is exactly the set whose returns copy -- give it no scope arena at all and
let its body allocate directly in _parent. No blocks to splice, no teardown
walk, and the return copy vanishes because the value is already there. The
qualifying test needs no types: a return whose expression is a place.

19 fixtures. Guarding it further -- never alias a body with an in-place string
accumulator, whose buffer must grow where it sits -- makes it WORSE, 35, so the
accumulator is not the (only) cause. Reverted, and this is the third shape of
"stop copying at the return" to fail after adoption (green but slower) and
promotion (seven attempts). The failures cluster on or_return, map parameters
and value_semantics, which is where to start if anyone tries a fourth.

## Tried and failed: ADOPT the scope instead of copying out of it

What is left of the copying is all at the RETURN boundary. Traced: the AST deep
copies are now almost entirely self-recursive (an Expr copy pulling its child
array, which pulls each child), and the ROOTS are the parser's own returns --
h_parse__mul, __postfix, __isexp -- handing a node up through the grammar. Each
level copies the whole subtree it just built.

Copying is the wrong instrument there. The scope is ABOUT TO DIE and the value
lives in it, so splicing the scope's blocks into the parent is O(1) where the
copy is O(the value), and every pointer stays valid. Sketched as
`arena_adopt(Arena *p, Arena *c)` in the runtime plus an `adopt` flag threaded
through the return's expression emit so tuple elements and payloads skip their
copies too.

BUILT AND GATED GREEN, AND STILL NOT WORTH IT. Two bugs had to be fixed first:

  - arena_reset pools every block after the head, which would hand back memory
    an adopted value still points into. Fixed with a `pinned` flag on HBlock
    that a reset keeps and only arena_free releases.
  - _ownerof answers "&_scope" for anything it cannot find, PARAMETERS included,
    so `return toks` adopted a buffer that belongs to the caller and handed the
    caller an alias of its own array. That is what reddened 14 fixtures, all of
    them slice/value-semantics ones. Fixed by requiring the root to be a local
    of the function being emitted.

With both fixed: 752/752, parse-check, corelib, conc all green, and

    instructions   1.016e9 -> 0.977e9   (-4%)
    allocations    11.1M -> 9.6M, bump 940 MiB -> 329 MiB
    wall           108 ms -> 121 ms     (+12%)

The instruction count improves and the WALL CLOCK gets worse, which is the whole
lesson. TYCHO_ARENA_STATS says where it goes: teardown is 19% of the run. Every
adopting scope hands its parent a whole block that is mostly empty, so the block
chains grow long and arena_free walks them. Adoption trades O(value) copying for
O(blocks) teardown plus the cache cost of a working set that no longer shrinks.

Reverted. Anyone taking it up needs a way to adopt only a scope whose live bytes
are worth a block -- which is a RUNTIME property, not a compile-time one.

## The biggest measured prize, and why it is not shipped

NARROWING THE ARM-MUTATION TEST IS WORTH 21%. `_mut_e` is conservative at ANY
unqualified call taking the binding first (emit.ty, the _mut_e Call arm). That
is exactly the shape of every recursive AST walker -- `_cn_es(k, nm)`,
`_mut_es(k, nm)`, `_names_in(kk, &out)` -- so every walker deep-copies its
payload at every node, and so does every analysis anyone writes on top of one.
Restricting it to the three builtins that really write their receiver
(push/pop/reserve; an `inout` parameter needs `&x` at the call site, which is an
Addr and is caught separately) gives:

    instructions   1.924e9 -> 1.521e9   (-21%)
    wall           200 ms -> 156 ms     -- a ratio of 1.7x, the best seen

SHIPPED as 05d0a710, once the real cause of the two map failures was found and
fixed (8fc08efc: an assignment built its value in &_scope and wrote the pointer
through an inout parameter, handing the caller memory the callee then freed).
The record of how it was found is kept below because the method is the lesson. A SEMANTICALLY INERT perturbation of emit.ty reddens the same two
fixtures:

    add a `curfn: string` field to struct C, set it in _fnbody around the body
    emit, add

        fn _bor_ok(cs: inout C) -> bool:
            return len(cs.curfn) > 0 and strings.starts_with(cs.curfn, "zzz__")

    and gate the two arm-binding copies on `not _bor_ok(&cs) or _mut_b(...)`.
    No function is named zzz__, so _bor_ok is always false and every binding
    copies exactly as it does at HEAD -- and tests/maps FAILS.

Controls, each observed: HEAD passes; the field ALONE passes; the field plus the
assignment passes. It takes the extra function and the two gated conditions --
still behaviourally identical -- to break it. So there is a LATENT,
LAYOUT-SENSITIVE miscompile in the self-built compiler, and the tree is green by
luck rather than by construction.

That reframes everything below it: the borrow narrowing was probably innocent
all along, and the 21% is blocked by this bug rather than by unsoundness.

What is known about the latent bug:
  - Stage 1 (built by ./tychoc) compiles both fixtures cleanly. Only the
    SELF-BUILT stage 2 fails, so the unsound borrow is somewhere in tychoc1's
    own source, not in the fixtures.
  - The symptom is a hoisted temp DECLARATION going missing: the emitted C says
    `h__mk4 undeclared`, so the `pre` accumulator in emit@_hoist_legs lost text
    it had appended. Once it emitted raw heap bytes into the middle of a line.
  - Restricting the borrow to ENUM payloads only does NOT fix it, and neither
    does requiring the match SUBJECT to be a place. Both were tried -- and both
    are explained by the perturbation result above.
  - Ruled out as the mechanism: the in-place string accumulator's length/cap
    shadow going stale across the recursive _hoist_legs call. `pre` is a
    parameter there, and accumulators are locals only, so that path never
    applies.
  - The 21% survives those restrictions largely intact, because copying an enum
    copies its payload arrays too -- tycho_arr_K0_copy is called BY
    tycho_copy_E_ast__Expr.

Next step for whoever picks this up: find which enum-payload binding in emit or
parse is written through. `_hoist_legs` and the compound-index-assign path are
where the corruption surfaces; the write itself is elsewhere.
  - MOVE-ON-LAST-USE itself, written and gated green (752/752, parse-check,
    corelib): a local read exactly once and not from inside a loop hands over
    its buffer instead of copying. It is a NET LOSS of 8% -- 217 ms -> 234 ms --
    and the split is the useful part: with the moves disabled but the census
    still running, 233 ms. So the read census costs ~16 ms and the moves it
    enables save ~1. Two reasons, both worth knowing before anyone tries again:
    the census is a per-function AST walk plus two maps, and the sites it
    reaches (declaration and assignment) are NOT where the copying is. The
    remaining tycho_copy_E_ast__Expr calls are deep copies ACROSS arenas --
    returns, and containers built in another arena -- which no last-use rule can
    elide. Reverted; the diff is recoverable from this commit's parent if the
    census is ever made free.
  - returning an IMMUTABLE value read out of a PARAMETER without copying it --
    12 fixtures. The premise is wrong: a parameter's buffer is not always in an
    arena that outlives the caller's _parent. `f(g())` passes g's result out of
    the caller's per-statement scratch, and f returning it lets the caller store
    into &_scope something that dies with that statement.
  - extending the same-arena sharing to STRUCTS with no array or map field
    (sound -- assigning a struct copies it by value and writing a field replaces
    the pointer) -- measured neutral, 1.9648e9 against 1.9625e9, because the
    struct copies that cost are inside array copies and at returns, neither of
    which the rule reaches. Not kept: a generalisation with no measured win is
    still code.

Done when: the ratio is <= 1.0 on compiler/main.ty and tycho-db.
Verify: best-of-three wall clock, both compilers, same input, --emit-c.
Gates: TYCHOC=./tychoc1 make test (752), make parse-check.
Not this: --native (measured slower, 341ms), -O3 (already used), per-token
field trimming (<1%), the copy_live lint (<1%), a per-loop scratch arena reset
per iteration (3 fixtures leak -- an early return escapes it), and one
per-function scratch reset per statement (307 fixtures: values DO outlive their
statement, and a reset reissues the same block). That last one is the useful
negative: the per-statement arena_new/arena_free is not removable without the
move-on-last-use liveness analysis that decides which values escape.

## Phase: the emitted-code performance gap to ./tychoc

Measured 2026-08-24, after `170f587c` and `67cd912c` made `bench/` honour
`TYCHOC`: benchmarks compiled by `./tychoc1` were run against the same source
compiled by `./tychoc` for the first time. Wall time is 1-4x worse; **peak RSS
is 100-400x worse** — strarr_build 1 MB -> 360 MB, inout_fill 1 MB -> 392 MB,
prongB iter-transform 4 MB -> 1536 MB, latency 4.5 MB -> 1533.7 MB, prongB
binary-trees 13 MB -> 767 MB, maptree 6 MB -> 504 MB. Two workloads are FASTER
under tychoc1: treewalk 38 ms -> 7 ms, prongB json-parse 1405 ms -> 1135 ms.
strarr_build, inout_fill and treewalk were reproduced independently.

**What is NOT established, stated before the ranking:**

- **No pass was isolated by rebuilding `./tychoc` without it.** The item-1
  ranking is inference from the shape of the emitted C, not measurement.
- **Items 6-9 have no measured workload at all** — they are gaps read out of
  `src/tychoc.c`, with no bench row attributable to them.
- **dbquery has no tychoc1 number**: `tychoc1: unknown option '--pkg'`, a CLI
  gap, not a codegen one.
- **The ms figures are best-of-1 on a non-quiesced box.** The RSS figures are
  deterministic and are the ones to trust.

The phases below are ranked by expected size of win. Each names the
`src/tychoc.c` pass and the emit site that stands in for it.

- [ ] **Perf 2 — bounds-check elision for monotone indices**
  - **Its claim is contradicted, 2026-08-30, and it is left open anyway.**
    `sh bench/guard.sh` under `TYCHOC=./tychoc1` reports `arr_pipeline: 2 raw
    .data[i], 0 checked calls (bounds-check elision live)`, so emit does NOT
    always emit the checked accessor. Not closed: that lane proves elision fires
    on ONE scan shape, which is not the same as the reference's monotone-index
    proof, and this phase still has no benchmark isolating it.
  - `src/tychoc.c:9351-9540` proves an index in range, gated at
    `src/tychoc.c:9450`. `compiler/emit/` always emits the checked accessor
    (`compiler/emit/emit.ty:1625`).
  - Evidence: wall time only; no RSS component. Unquantified — no benchmark
    isolates it.

- [ ] **Perf 3 — nullary-variant singleton returned by copy**
  - `src/tychoc.c:9793-9806` returns a shared singleton for a payload-free enum
    variant. `compiler/emit/emit.ty:4256` copies one out.
  - Evidence: allocation count on any Option/Result-heavy loop; no isolated
    measurement.

- [ ] **Perf 4 — move-on-last-use**
  - `src/tychoc.c:9543-9713` hands a buffer over at its last read instead of
    copying. `compiler/emit/emit.ty:1320` (`_copy`) always copies.
  - Evidence: a standalone attempt on tychoc1's OWN compile speed was a net 8%
    LOSS (recorded above), because the census cost more than the moves saved.
    That is a cost measurement of the analysis, not of the win on the emitted
    programs, and it does not settle this item either way.

- [ ] **Perf 5 — construction-argument move**
  - `src/tychoc.c:10029-10050` moves an argument into the aggregate being built.
    `compiler/emit/emit.ty:2437` and `compiler/emit/emit.ty:3084` copy.
  - Evidence: binary-trees (13 MB -> 767 MB) is the shape; not isolated.

- [ ] **Perf 6 — map accumulator rewritten in place**
  - `src/tychoc.c:9928` and `src/tychoc.c:9938` recognise `m[k] = ...` /
    `delete` on the map being folded and write in place. `compiler/emit/` has
    **no** `map_set` fast path at all.
  - Evidence: **none measured.** maptree (6 MB -> 504 MB) is the plausible
    workload; nothing attributes it.

- [ ] **Perf 7 — push-cursor caching**
  - `src/tychoc.c:9643`, `:9671`, `:9698` (`fuse_gather`/`fuse_open`/
    `fuse_close`) hoist the destination cursor out of a push loop.
    `compiler/emit/emit.ty:3166` emits an independent push per iteration.
  - Evidence: **none measured.**

- [ ] **Perf 9 — sink-argument adopt**
  - `src/tychoc.c:9714-9733` adopts a `sink` argument's buffer instead of
    copying it. `is_sink` is parsed (`compiler/ast/ast.ty:30`) and **never read
    by `compiler/emit/`**.
  - Evidence: **none measured.**

- [ ] **Emit parity — the classes measured 2026-08-26, in payoff order**
  - Method: diff every differing fixture, bucket the changed lines. Parity is
    byte-identity PER FIXTURE, so the count moves only when a fixture's LAST
    class clears; the census below is "fixtures containing it", not "fixtures
    it would clear".
  - **Fixed-size `[N]T`** — 86 fixtures. `./tychoc` emits `TychoArrC<n>`
    (`struct { tycho_int v[3]; }`); `compiler/emit/emit.ty` has no fixed-size
    array kind and falls back to the dynamic `TychoArrK<n>`. The single largest
    class by lines.
  - **Accumulator arena** — 81 fixtures. `./tychoc` builds a returned
    accumulator in `_parent` and returns it bare; emit.ty builds it in `&_scope`
    and copies on return (`tests/saccum_typed.ty`, `tests/crlf_adjacent.ty`,
    `tests/if_expr_block.ty` are each ONLY this).
  - **`_set` vs `*_ptr`** — 106 fixtures. `tycho_arr_int_set(&xs, i, v)` vs
    `(*tycho_arr_int_ptr(&xs, i)) = v` (`tests/sink.ty`).
  - **Scratch arena sequence numbers** — 70 fixtures, and **not a class of its
    own.** It was read as a pure renaming; it is a SYMPTOM. Both compilers name
    `_scr<N>` off one shared block counter, so the sequences diverge wherever
    the reference takes a number emit.ty never asks for — the offsets are
    downstream of every other class, and `sole diff` is empty for all 70. The
    remaining shapes, measured 2026-08-26 by comparing the ordered `_scr`
    sequence per fixture: an unequal COUNT (`bool_array`, `int_hex`,
    `variadic` — a loop scratch the reference opens and emit.ty does not), and
    an offset that tracks a missing temp elsewhere in the same function. Fix
    the class that consumes the number; the sequence follows.
  - **A string SLICE is not a place** — 4 fixtures, and the whole diff in each
    (`str_index`, `string_slice`, `float_str_locale`, `slice_once`).
    `compiler/emit/emit.ty@_place` has no `ast.Slice` arm, so `t := s[7:12]` is
    retained with no `tycho_str_copy`. Not `_shares` — that path clears
    `string_nul` alone.
  - NOT a class: the shadow rename, closed 2026-08-26 (commit 25e16469). It
    was 4 fixtures, not 8; two were its sole diff and cleared.
  - NOT a class: the multi-piece self-append `_ap` temps, closed 2026-08-26
    (commit 01c0c363) — the largest single source of `_scr` offsets.
  - NOT a class: the generic-instance NAME, closed 2026-08-26 (commit 8e58f43a).
    The census read it as "instances ./tychoc never makes"; it was the same
    instance set under a serial name.
  - **`_shares` removal is REFUSED on measurement, not on taste.** It buys
    `string_nul` (+1) and costs wall 1.076/1.084/1.081 min/med/mean against
    `./tychoc` — over 1.000 on all three, against 0.994/0.995/0.992 without it —
    and callgrind Ir 847.7e6 -> 925.5e6 (+9.2%). Measured three-way interleaved,
    51 reps, `--emit-c compiler/main.ty`, every run asserted exit 0 and a >2 MB
    `.c`. Do not re-propose it without a replacement for the elision.

## Compile-speed vs ./tychoc: where it stands and what is closed

Measured on a QUIET box (load 1.53, after killing four runaway processes that
had held load at 4-5 all session), fine-grained A/B alternating on every run --
a block-interleaved harness gives contradictory answers when the rotation order
is swapped, and a python subprocess.run wrapper charges ~0.5 ms per compile,
which reads a 1 ms input as 1.04 when it is really 1.20.

  compiler/main.ty 0.673 | tycho-db 1.033 | server 1.057 | tiny 1.081
  tycho-sheet 1.095 | tycho-scheme 1.127 | tycho-vm 1.162 | raytrace 1.199
  GEOMEAN 1.040

The session took the worst case from 2.74 to 1.20 and compiler/main.ty to 0.673.

CORRECTION, measured across ALL 55 real entry points (examples/*.ty,
tools/*/main.ty, server, compiler) rather than the eight ad-hoc inputs above:
tychoc1 is at or under 1.000 on **4 of 55**. Geomean 1.1376, median 1.1600.
The four wins are the LARGEST compiles -- tycho-fetch 0.621, compiler/main.ty
0.684, tycho-ar 0.803, tycho-snap 0.808 -- and the 51 losses run 1.056 to 1.371
(worst: invindex 1.371, tycho-chess 1.360, tycho-grid 1.298).
So the eight-input set was FLATTERING tychoc1, not representative, and the
crossover is a function of compile size: tychoc1 wins only where the variable
work is large enough to amortise its ~1.05 ms of fixed cost per compile.

SUPERSEDED by commit e539f781, which stopped emitting the ~33% of functions
nothing in the unit reaches. Re-measured over the same 55 entry points, min of
2 alternating rounds against ./tychoc on a quiet box: **12 of 55** at or under
1.000, geomean **1.0239**, median **1.0507**, worst 1.339 (was 1.372). The
eight-input geomean is **0.9849**. The size crossover above still holds --
every win is still a large compile -- but a program that imports a package for
two symbols no longer pays for the rest of it.

SUPERSEDED AGAIN by commits 40f19a0 and a307931, which take the same
reachability idea down to the GENERATED helpers -- a type's eq/str/copy/hash
and an array kind's eleven operations were emitted as a family whether or not
anything called one. Re-measured over the same 55 entry points, min of 2
alternating rounds against ./tychoc on a quiet box (load 0.84): **7 of 55** at
or under 1.000, geomean **1.0563**, median **1.0814**, worst 1.295. The two
commits are 0.9939 and 0.9753 against their own rebuilt predecessors.

**This avenue is now exhausted, measured rather than assumed.** Closing
reachability over the emitted C from its non-helper text, the dropped set is
EXACTLY the unreachable set on server, tycho-vm, compiler and raytrace, and
what is left dead is 0.3% of the output on compiler/main.ty and 0.0% on
server -- all of it the map family, whose twelve operations cross-reference
each other. There is no third level to take this to.

### Why the rest is not another optimisation pass

raytrace needs ~40% of tychoc1's instructions gone to reach 1.000 and the
largest single remaining item is 9%. `./tychoc` is SINGLE-PASS and builds no
AST; tychoc1 builds one. That is the gap on small compiles, and it is
architectural.

### Closed by measurement -- do not re-open without new evidence

- **The AST deep-copy family.** Extending escape analysis so values are built in
  the destination arena: -5.0% Ir, WALL-NEUTRAL (geomean 1.0041), because the
  loop scratch is a cache-resident bump allocator and the long-lived arena is
  cold -- LL misses +9.0%, all write-side. Ceiling: deleting EVERY deep copy
  reaches 1.017x tychoc's Ir *before* the cache cost. Patch kept at
  scratchpad/escape-push-promotion.patch.
- **Ir is not the metric.** -5.0% Ir bought 0.4% wall; -1.8% Ir measured wall
  NEGATIVE; static linking is +39% Ir and -8% wall. Decide on wall, always
  against a rebuilt HEAD binary in the same rotation.
- **Build flags**, all measured: current `-O3 --param inline-unit-growth=150
  -static -flto` is best of ten. -Os 2.410, -O2 1.486, plain -O3 1.432, growth
  300/600 no change, max-inline-insns-auto=60 worse, -fipa-pta 1.041,
  `-ffunction-sections -Wl,--gc-sections` 3.7% SLOWER despite -164 KB of text
  (so text size is not the mechanism; cross-TU inlining is),
  -fno-unwind-tables inside noise and costs gdb backtraces.
- **Link models**, all measured against -static: dynamic 1.086, -static-libgcc
  1.081, -static-pie 0.9964 (inside noise, and loses on compiler/main.ty).
  Dynamic has the LOWEST Ir and the WORST minor faults, 133/run vs 105.
- **Other dead ends:** block_into at top level (Ir -0.11%, wall +3.2%);
  self_rebuild_move (fires nowhere); building a push's call-result in the
  destination arena (use-after-free in stage 2 -- the same wall as the
  "reddened 190 fixtures" note in emit.ty); borrowing ld.files[i].unit.pkg
  through a parameter (10k Ir worse); single-hash map insert (tiny -2.8%,
  compiler +3.1%); index loops in _sig_of (7k Ir); the outp.files reorder
  (1.67%); io.copy of the 140 KB runtime (~150 us on ext4 by EVERY method --
  stdio, copy_file_range, sendfile, 1 MB read/write -- so ./tychoc pays it too).
- **Cannot be touched cheaply:** swapping the runtime's siphash moves keys()
  order and re-records goldens tree-wide for BOTH compilers.
  `__register_frame_info` runs before main for 202,296 Ir, 6% of a raytrace
  compile, out of crtbeginT.o; no flag disables it.

### Interning identifiers at lex time -- CLOSED, not attempted, 2026-08-26

The last avenue the previous note called tractable. It was not written; it was
PRICED first, with two probes that cost twenty minutes instead of a whole-tree
refactor of lex/ast/parse/types/emit.

- **The hashing half.** Replacing `tycho_si_hash`'s SipHash-1-3 with an O(1)
  proxy for an int key (`runtime/tycho_rt.c@tycho_si_hash`, probe only, never
  committed) is the upper bound on what keying the symbol tables by an id can
  win: **-4.08% Ir on raytrace, -0.57% WALL geomean** over the eight inputs,
  raytrace itself -0.43%. A first probe using a variable-length `memcpy` moved
  Ir +0.4% and wall +0.48% and proved nothing -- the control that matters here
  is that the Ir actually FELL, since a "cheaper" hash that inlines its cost
  into `mapM0_find` reads as a fix and is not one.
- **The copying half** cannot be probed the same way: `tycho_str_copy` as a
  no-op breaks the stage-1 self-compile, so `make tychoc1` fails outright.
  The AST deep-copy family above is its measured stand-in: -5.0% Ir,
  wall-neutral.

Both of the two largest items interning attacks therefore convert at about
**0.1 wall-% per Ir-%**, and interning's whole theoretical ceiling (str_copy
9.4% + siphash 5.7% + a slice of tokenize_named and mapM0_find, of which only
the identifier-related part is reachable) is ~15% Ir. That is ~1.5% wall at a
ceiling nobody reaches, against a 17k-line refactor. It does not pay.

### Why: most of a small compile is not instructions at all

Ir against wall, measured 2026-08-26 on the same binary:

| input | Ir | wall |
|---|---|---|
| tiny | 412,579 | 1.11 ms |
| raytrace | 3,331,700 | 1.78 ms |
| tycho-vm | 45,500,559 | 8.08 ms |

The fit through the two ends puts **~1.05 ms of fixed cost on every compile** --
exec, the 2.8 MB static binary's page faults, the pre-main FDE registration, the
emitted C and the 140 KB runtime copy -- which is 59% of a raytrace compile and
94% of `tiny`. tychoc1's fixed cost is already within 8% of ./tychoc's (that IS
the 1.081 on `tiny`); the 1.199 on raytrace is 1.46x on the VARIABLE part alone.
Reaching 1.000 there needs ~32% of the variable time gone, which at the measured
conversion is most of the instruction stream. No front-end change buys that.

### The cause, measured directly (cachegrind, examples/invindex.ty, worst ratio 1.371)

|            | ./tychoc | ./tychoc1 | ratio |
|------------|---------|----------|-------|
| I refs     | 1,993,780 | 3,039,560 | 1.52x |
| **D refs** | 694,746 | **1,312,166** | **1.89x** |
| LL misses  | 8,284 | 16,321 | 1.97x |
| minor flt  | 135 | 207 | 1.53x |
| LL miss RATE | 0.3% | 0.4% | same |

The miss RATE is identical: locality is not worse, tychoc1 simply TOUCHES TWICE
THE DATA. That is an AST built with value semantics, and it is the same
quantity the deep-copy work proved cannot be removed profitably.

INSTRUCTION COUNT IS NEARLY MEANINGLESS FOR THIS COMPARISON. Three separate
large Ir reductions produced NO wall win: the escape/promotion pass (-5.0% Ir,
wall 1.0041), the siphash proxy (-4.08% Ir, -0.57% wall), and
-fno-asynchronous-unwind-tables (-19% Ir on an empty compile, wall geomean
1.0030 over six inputs). tychoc1's excess instructions are high-IPC linear work
-- arena copies, and the pre-main FDE scan which is 63% of an EMPTY compile
(201,044 of 319,103 Ir) -- that the prefetcher absorbs. ./tychoc's time is
cache-missing pointer chasing instead: 30% __strcmp_avx2, 10% _int_malloc.

Also measured: tychoc1's process startup is FASTER than ./tychoc's (--version
1262 us vs 1344 us over 200 execs). The whole per-compile difference is setup
work, not exec: an empty-file compile is 1481 us vs 1337 us. Reading the 140 KB
runtime rather than embedding it, as ./tychoc does, is 22 us of that
(read+write 112.8 us vs write-only 91.2 us).

### Shrinking the AST node types -- CLOSED by a sensitivity probe, 2026-08-26

The lever the section above named as the last one short of not building an AST.
It was PRICED before it was written, the way interning was, and it is dead.

`compiler/ast/ast.ty`'s `Expr` union is 88 B against a 32 B median payload and
`Stmt` is 96 B against 40 B, so the premise was that every node drags dead bytes
through cache. The probe tests the SLOPE instead of assuming it: take the C that
stage 1 emits for `compiler/main.ty`, append a padding array to `struct
E_ast__Expr` and `struct E_ast__Stmt`, rebuild on the same cc line, measure.
Growing a node cannot be worse than shrinking it is good.

| every node grown by | D refs | LL misses | LLd write misses |
|---|---|---|---|
| +0 (HEAD, rebuilt) | 1,308,061 | 16,400 | 7,094 |
| +32 B (Expr 120, Stmt 128) | 1,309,459 | 16,410 | 7,094 |
| **+4096 B (Expr 4184, Stmt 4192)** | **1,309,196** | **16,400** | **7,090** |

A 47x node is FREE: +0.09% D refs, zero change in LL misses, and wall geomean
**0.9980** over invindex, raytrace, tiny, tycho-vm, tycho-chess and
compiler/main.ty. The mechanism is that a bump allocator never touches the bytes
nobody assigns, and `[Expr]` lowers to an array of `E_ast__Expr *` -- a push
copies 8 bytes, and `tycho_copy_E_ast__Expr` copies the live variant's fields,
not the union. Node size is not on the data path at all, so removing it removes
nothing. Controls: all three binaries emit byte-identical C for invindex, and a
first 4 KB run that read 73,232 D refs was a cwd mistake that died on `no 'main'
procedure` -- the dying-compile trap, caught and discarded rather than reported.

### The miss gap is half INSTRUCTION fetch, which no front-end change reaches

Splitting the same cachegrind runs on `examples/invindex.ty` -- the number the
section above reported only as a total:

| | ./tychoc | ./tychoc1 | delta | share of gap |
|---|---|---|---|---|
| LLi (instruction) | 3,161 | 6,972 | +3,811 | **47%** |
| LLd read | 1,561 | 2,382 | +821 | 10% |
| LLd write | 3,558 | 6,966 | +3,408 | 42% |

Nearly half the miss gap is code footprint, not data: 2.79 MB of static binary
against 0.56 MB, and a compiler that makes several typed passes over a tree
where `./tychoc` makes one. `-ffunction-sections -Wl,--gc-sections` already
measured 3.7% SLOWER for -164 KB of text, so this is the executed code surface
and not the file size. The write half is `__memcpy` 1,520 misses, an arena array
push 1,077, `tycho_str_copy` 1,005, `tokenize_named` 920, `tycho_copy_E_ast__Expr`
536, `__memset` 514 -- string copying and array growth, every one of which the
deep-copy and interning work above already closed by measurement.

### PGO: measured, real, and not adopted

`-fprofile-generate` over four inputs then `-fprofile-use` on the same cc line:
wall geomean **0.9876** over tiny/raytrace/invindex/tycho-vm/compiler -- tiny
0.965, invindex 0.981, raytrace 0.988, neutral on the two largest. It does NOT
reduce instruction-cache misses (LLi 7,068 vs 6,972 at HEAD), so the win is
branch layout, not the I-cache footprint it was aimed at.
NOT adopted: it needs a two-stage instrumented build and stored .gcda profile
data in the Makefile, and 1.2% does not change the standing when the gap is 14%.
Revisit only if the gap is otherwise closed to within ~2%.

### The lexer at 54 instructions per byte -- 214 per TOKEN, and it pays

54 Ir/byte was the wrong framing: 23,764 tokens from 93,961 bytes is 3.95
bytes/token, so it is **214 Ir per TOKEN**. The scanning loops are ~11 Ir/byte;
the cost is per-token materialisation -- an 8-field value-semantic Tok built on
the stack, deep-copied into the array's arena, at **3.88 tycho_str_copy calls
per token**, two of them copying "".
f0b22716 fixed three defects: Tok.file was copied on all 23,763 non-EOF tokens
though only K_EOF uses it; `raw` went through tycho_str_copy because a VARIABLE
is copied where a literal is emitted bare; op_len did two bounds-checked reads
and an eleven-test chain for the ~70% of operators that are one byte.
Result: 3.88 -> 2.11 str_copy per token, lexer self 5.08e6 -> 4.74e6, program
27.32e6 (-3.44%), **wall geomean 0.9846 over seven inputs**.

MEASUREMENT INCIDENT, worth more than the patch. I first measured this change
at wall 1.0459 -- 4.6% SLOWER -- and wrote it up as a loss. That reading was
taken while the agent that authored it was still running: its `make parse-check`
had reverted the worktree edit and truncated `tychoc1` to 0 bytes mid-run, so I
timed a binary that was being rebuilt underneath me. Re-measured on a quiet box
with both binaries built from committed trees: 0.9846. **Never measure while
another process can touch the worktree or the binary**, and check
`pgrep -fa 'tychoc|make|valgrind'` before timing.
It converted at ~0.4 wall-% per Ir-%, four times the ~0.1 this session's other
Ir cuts managed, because these are call+allocate costs rather than linear copy.
The residue is `text` and `raw` genuinely crossing arenas -- the deep-copy
family already closed. The lexer is only ~6% of a small compile, so there is no
second win of this size in lex.ty.

### A COMPILE CACHE IS REFUSED

Corelib loading is the one lever measured with enough headroom to close the gap:
`_merge` is 16.98e6 of the 28.29e6 resolve phase (60%), ~28% of the whole
compile, re-deriving 11 corelib files totalling 93,961 bytes on EVERY compile.
Caching that would put tychoc1 at roughly 44e6 against ./tychoc's 54.4e6 on
server/main.ty -- a win rather than a 1.13 loss -- and would apply to most of
the 51 entry points that currently lose.
The user has refused it: a cache adds a stale-build failure mode this compiler
does not have, and this is a one-person project that would carry that support
surface forever. Embedding the PARSED corelib at build time is treated as
covered by the same refusal -- it has the same staleness property and would cut
across TYCHO_CORELIB.
So the gap stands. Do not re-propose either without new information.

### Why the crossover exists at all (./tychoc profiled, server/main.ty, 54.41e6 Ir)

  __strcmp_avx2  19,254,418  35.39%   <- its symbol table is LINEAR string compare
  _int_malloc     4,331,240   7.96%
  lex             3,386,363   6.22%
  sig_find        2,725,075   5.01%

./tychoc burns 43% of its compile on symbol lookup and allocation. tychoc1
replaced that with hash maps costing ~1e6, so it SAVES ~18e6 there -- and is
still 7e6 behind in total, which means its AST construction and five-pass
structure cost ~25e6 more than tychoc's single pass.
That IS the crossover, and it is a property of the two designs rather than of
any missing optimisation: ./tychoc's cost grows with SYMBOL COUNT, tychoc1's
with a fixed per-compile AST overhead. So tychoc1 wins the large programs
(compiler/main.ty 0.682, tycho-fetch 0.618, tycho-ar 0.771, tycho-snap 0.783)
and loses the 51 small ones, where there is nothing for its overhead to
amortise against. Its own lexer is now within 1.4x of ./tychoc's after
f0b22716, so the lexer is no longer the difference.

### If this is picked up again

The remaining items on raytrace are all under 10%: str_copy 9.4% (already 13
instructions per call), tokenize_named 6.0%, pre-main FDE 6.0%, siphash13 5.7%,
AST copies 4.8%. Interning is closed (above) and so is every avenue in this
section. What is left is the fixed 1.05 ms and the single-pass shape -- not
building a full AST for a single-file compile -- not another pass over the
current one.



### Emit-time dead-body elimination: independent re-measurement


INDEPENDENT RE-MEASUREMENT, idle box (load 1.79), best of 3 rounds with each
timing sized to ~300 ms: **5 of 55 at or under 1.000, geomean 1.0809, median
1.1046, worst invindex 1.354**. The authoring agent reported 12/55 and geomean
1.0239 from a min-of-3 harness; that does not reproduce. Take the conservative
figures. The DIRECTION is not in doubt -- geomean 1.1262 -> 1.0809 and the
median off 1.15 -- but the change is worth ~4%, not ~9%.


### Emit-time reachability, both levels: what it actually bought

e539f781 (function bodies, 222 -> 153 definitions on server) and 40f19a00 +
a3079317 (generated helper families, per operation) are the largest wins after
static linking. Emitted C shrinks 17% on tycho-vm, 13% on server, 9% on compiler.

MEASURED THREE WAYS, and the spread is the lesson:
 - the authoring agents' own sweeps: 12/55 then 7/55, geomean 1.0239 then 1.0563
 - my independent 55-point sweeps: 5/55 then 6/55, geomean 1.0809 then 1.0833
 - a DIRECT A/B of the two trees, both binaries rebuilt in one rotation, idle
   box: **0.9840 for the helper pass** -- a real 1.6%
Two independent whole-corpus sweeps differ by MORE than the effect being
measured, so only the paired A/B against a rebuilt sibling is trustworthy. Quote
the paired number for a change and the sweep only for the standing.

Standing after both, my sweep on an idle box: **6 of 55 at or under 1.000,
geomean 1.0833, median 1.1075**, worst invindex 1.407, hello 1.388.

Further reachability is EXHAUSTED: after both passes the still-dead set is 0.3%
of compiler/main.ty's output and 0.0% of server's, all of it the map family
whose twelve operations cross-reference each other.


### Two findings from the last probe, neither shipped

**Embedding runtime/tycho_rt.c into tychoc1 the way the Makefile embeds it into
./tychoc is worth ~22 us per compile** (read+write 143 KB is 112.8 us against
91.2 us write-only) and would remove an asymmetry with the reference rather than
add a failure mode. It is NOT reachable through a Tycho string literal: a
150,000-character line of 'a' compiles fine, but the escaped runtime source does
not -- so the blocker is escape handling, not size. A byte-array or chunked
encoding could still work. At ~22 us against gaps of 300-400 us on the small
inputs, it is not worth the build machinery on its own.

**RETRACTED -- there was no diagnostic defect.** I reported that tychoc1 named
a file it was never given (`/tmp/lsp_bad.ty`). It was given: `package main` makes
the DIRECTORY a package, so the compiler loads every .ty in it, and /tmp held a
broken file from an unrelated tool. ./tychoc does exactly the same -- both print
`/tmp/pkgtest/b.ty:4: error: expected an expression` on a package directory with
a broken sibling. Correct behaviour in both. `lex.file_of` returning the last
token's text remains theoretically unsound on an early-exit path, but no input
demonstrates it and the speculative fix was reverted.

**And embedding IS feasible after all.** The escaped runtime -- 147,521
characters -- compiles as a single Tycho string literal; my earlier "blocked by
escape handling" note was measuring the package-loading failure above, not a
literal-size limit. So embedding runtime/tycho_rt.c into tychoc1 the way the
Makefile embeds it into ./tychoc is available, worth ~22 us per compile
(read+write 143 KB is 112.8 us against 91.2 us write-only). It is small against
gaps of 300-400 us, but it is real and it is symmetric with the reference.


### The closing arithmetic: no phase dominates

tools/tycho-vm/main.ty, cumulative Ir by phase (./tychoc totals 34.6e6):
  --parse       6.48e6   16%
  --resolve    21.72e6   +15.2e6, 37%
  --typecheck  31.14e6    +9.4e6, 23%
  --emit-c     41.33e6   +10.2e6, 25%
tychoc1 is 1.19x on instructions here and every phase carries part of it. To
reach wall 1.000 needs roughly 16% of instructions removed ACROSS ALL FOUR, and
this session's Ir cuts converted to wall at 0.1-0.4x, so it is nearer 40% of
instructions in practice.
Merging resolve and typecheck -- the only remaining structural idea that does
not add a second code path -- would save the duplicate traversal, perhaps
2-3e6 (5-7% Ir, so ~1% wall), against a 19% gap. Not worth a high-risk refactor
of two passes with ordering dependencies.

That is the end of the analysis. The gap is not one hot spot; it is an AST-based
five-pass compiler against a single-pass one, and closing it needs a different
front-end shape, which would mean two code paths in a one-person project.


### RETRACTED: tests/strbytes.ty does NOT fail under ASan

Two agents reported it dying under -fsanitize=address,undefined with every
compiler including ./tychoc, and I relayed that three times without running it.
Checked: `TYCHO_CFLAGS="-fsanitize=address,undefined -g" ./tychoc
tests/strbytes.ty` then running it gives **exit 0, zero bytes on stderr, output
matching the golden**. There is no pre-existing memory error here.
Whatever those runs hit was environmental -- most likely the `Permission denied`
and truncated-binary interference documented above, when two sessions were
rebuilding tychoc1 concurrently. A failure seen once during a contended run is
not a defect until it is reproduced on a quiet box.


### OPEN: no lane can redden when tychoc1 stops checking generic template bodies

Found 2026-08-27 as the negative control for f0e2a3b5. A build that drops
FSig.body for GENERIC signatures too accepts an ill-typed template instance --
`fn twice(x: $T) -> $T: return x + x` called at $T=bool, which ./tychoc refuses
-- and `make parse-check` stays ALL GREEN: 1081 files, leg5 and leg10 both 0
disagreements.

Why both legs miss it: the 337 reject fixtures reach leg5 through
compiler/reject_class.tsv as SEMANTIC, and a SEMANTIC rejection is required to
be ACCEPTED by the parser leg (that is what stops "reject everything" scoring
full marks). leg10's whole-tree typecheck comparison does not score them either.

Done when: a fixture exists that reddens for this, with the accept/refuse pair
above as its observed control. Note the pinned counts -- compiler/run.sh,
compiler/verdict_diff.py@EXPECT and the three censuses -- move when a fixture is
added; that has broken parse-check twice before.

### CORRECTION: the -static-pie win in 30029fa5 is ~0.5%, not what that message claims

30029fa5 reports "7 of 90 entry points at parity, now 39" and geomean
1.0854 -> 1.0073. Those two figures came from two sweeps run hours apart and are
not a controlled comparison. Measured head-to-head in one session, same source,
alternating A/B, -static vs -static-pie is geomean **0.9949** over five inputs,
with tiny.ty at 0.9923 (median 1.0256 -- i.e. the median says slightly slower).

The Ir figure in that message IS solid and reproducible: -static-pie removes
201,604 Ir of eh_frame scan on every compile. It does not convert to wall time,
because that scan is a cache-resident linear walk at high IPC. Instruction count
is not time, and this is the second measurement this session where I read a
ratio off runs that were not taken against each other.

Sweep counts against ./tychoc are session-dependent at the few-percent level and
must not be quoted across sessions: 39/90 (geomean 1.0073) early, then 10/90 and
12/90 (1.0577, 1.0614) back-to-back later the same day, same binaries.


### CLOSED 2026-08-30: the recursion TCO fixtures pass under BOTH compilers

Superseded the 2026-08-27 "leave it" entry. tychoc1 was never wrong here: a
self-call in tail position is a loop once the emitted C has nothing after it, so
the frames never exist and the program returns the right answer. ./tychoc emits
arena_free(&_scope) AFTER the call, which blocks the tail call, so its stack
really does overflow. Both behaviours are correct; the lane asserted only one of
them.

tests/recursion/run.sh@progdie now takes the expected answer and accepts either
outcome:
  exit 0        -> stdout must EQUAL that answer
  exit 1..127   -> stdout empty and "stack overflow" on stderr (unchanged)
  exit >= 128   -> FAIL always, a signal is never acceptable

That is STRONGER than what it asserted before, not weaker: it used to accept any
nonzero exit and never checked what a surviving program computed. Nothing in the
compiler changed.

  ./tychoc   3x "program died cleanly, rc=1"
  ./tychoc1  3x "tail call became a loop; correct answer ...", 2000001000001 / 0 / 0

Controls, both observed: a deliberately wrong expected answer fails the tail-call
branch, and a binary dying on SIGSEGV exits 139, which the >= 128 leg refuses.
