# Closed 1.0 conditions, August 2026

Maintainer material, moved out of [`ROADMAP.md`](../../ROADMAP.md) on 2026-09-02.
Two relative link targets are rewritten, because the text moved two directories
down; nothing else is edited. These are conditions §2 through §6 and the
order-of-work list exactly as they were written and closed, kept because each one
records what was measured and what was refused, not merely that it finished.

A roadmap is direction. These are history, and they were 351 of ROADMAP's 652
lines, which put "what remains for 1.0" 460 lines below the heading that asks it.
What is still open — §1 and §7 — stayed there.

### 2. ~~The daily papercuts are gone~~ — **CLOSED 2026-08-15**

**Re-probed at HEAD**, 254 commits after the 2026-08-09/08-14 passes below, in
one program: `pass` and the operator line continuation together give 6, both
typed-empty spellings accept a `push`, a newtype-of-array supports `push`/`len`/
indexing (2, 7), `or_return` returns 41 through `fn main() -> Result(void,
string)`, and a package-level `len` still draws the collision warning that names
`package main`'s remedy. A table probed 254 commits ago is a claim, not a state;
this is the state.

These are ergonomic, not soundness — which is exactly why they must be fixed
*before* a freeze rather than after, since fixing them later is a breaking
change. All were recorded by the project itself, in the early plan learnings
(rotated away; recoverable under the `docs-archive` tag) and in FRICTION:

- ~~a package-level `len` **shadows the builtin** inside its own package~~ —
  **closed 2026-08-09**, see the probe table below
- ~~no `defer`~~ — **refused 2026-08-10** (see the probe table); ~~and no bare
  `pass`/no-op statement~~, **closed 2026-08-10**
- ~~**no expression line continuation**~~ — **closed 2026-08-09**
- ~~consts do not cross package boundaries, so levels ship as functions~~ —
  **closed 2026-08-10**: `pkg.NAME` reads an exported const, folded at the use
  site. No export keyword; the leading-underscore rule already there does the
  privacy. One position remains open and is marked `gap:` in the source — a
  qualified const as a fixed-array length, which needs sizes resolved after
  parsing
- ~~`or_return` requires a `Result`-returning function, which `main()` is not~~ —
  **closed 2026-08-10**: `fn main() -> Result(void, string)` is a second legal shape
- ~~a newtype-of-array blocks `push`~~ — **closed 2026-08-10**, and it was worse
  than `push`: every collection operation refused it
- ~~an aggregate capturing a still-live binding warns until you write the copy by
  hand (`arg := hostile`)~~ — **closed 2026-08-10**: that hand-written copy never
  removed a copy, it only silenced the warning (measured); the message now offers
  only the remedy that works
- ~~the typed empty is `[]string`, which nobody guesses first~~ — **not a
  language gap** (see the probe table); the DIAGNOSTIC for the wrong guess was
  fixed 2026-08-10, so `[string]` now names both working forms instead of
  saying "expected an expression"

Each is small. Together they are what "the language is unpleasant on day two"
is made of.

**Re-probed against the compiler on 2026-08-09** rather than trusted from the
phase-1 notes, because two entries turned out to be wrong:

| Papercut | State |
|---|---|
| package-level `len` shadows the builtin | **closed 2026-08-09** — still legal (§3.7 permits it on purpose), but the compiler now warns at the declaration and names the consequence, so the wrong answer is no longer silent. Fixture `tests/warn/shadow_builtin.ty`. |
| no bare `pass` | **closed 2026-08-10** — `pass` is the no-op statement. CONTEXTUAL, not reserved: significant only as a whole statement, because `pass` was already a variable name in `corelib/test/testing` and `tools/prunner` and reserving it would have broken the runner that scores `make test-fast`. Fixtures `tests/pass_stmt.ty`, `tests/reject/pass_as_value.ty`. |
| no expression line continuation | **closed 2026-08-09** — a line ending on an operator joins the next, when that line is indented deeper. The deeper-indent condition is what keeps a truncated line a truncated line: `tests/diag/caret_expr.ty` caught the naive version degrading its own diagnostic. Fixture `tests/line_continuation.ty`. |
| no `defer` | **refused 2026-08-10, documented.** Not a papercut: this language has three cleanup mechanisms already and `defer` would be a fourth. Memory is arena-freed at scope exit; a `handle` runs its declared `free:` at scope exit (RAII, affine, §25); a channel/task handle is affine with an implicit join. `core:io` is path-based and opens nothing. The tree's `close(` calls are overwhelmingly CHANNEL closes, not resource cleanup. Already found once and filed anyway — `docs/internals/plan-tycho-vm-DONE.md:27-32`, whose own verdict was "nothing needs it; it was filed because the probe surprised me, which is not a reason." |
| `or_return` from `main()` | **closed 2026-08-10** — `fn main() -> Result(void, string):` is legal, so `or_return` works at the entry point. `Err(msg)` prints `msg` bare to stderr and exits 1; `Ok` exits 0. The error type is `string` because the message is what gets printed, and the ok payload is `void` so the exit status stays unambiguous. Fixtures `tests/main_result.ty`, `tests/abort/main_result_err.ty` and three rejects |
| newtype-of-array blocks `push` | **closed 2026-08-10** — and the row understated it. A probe found `len`, indexing, slicing, `push`, `pop`, `for … in`, index-assign and every map builtin ALL refusing a newtype-of-collection: it was write-only unless you went through `to_under`. Operations now see through the newtype; distinctness at assignment, parameter passing and comparison is unchanged and pinned by `tests/reject/newtype_agg_still_distinct.ty`. Fixture `tests/newtype_agg_ops.ty` |
| aggregate capturing a live binding warns | **closed 2026-08-10 — the warning was right, its advice was not.** Measured on the emitted C: the suggested `y := s` silences the warning and emits the SAME two copies, while making it the value's last use emits one. The no-op remedy is gone from the message; the one that works is stated plainly. Golden `tests/warn/copy_live.err` |
| ~~typed empty is `[]string`~~ | **not a defect** — both `xs : [string] = []` and `xs := []string` compile. It is a learning-curve item, not a language gap. |
| consts across package boundaries | **re-probed 2026-08-14 — closed.** `pkg.NAME` reads across the boundary for all four base types in every value position, including arithmetic and comparison: a four-const package read from another gave `4 lim 1.5 true` and `arith: 8 cmp: true`. The one remaining position is the documented `gap:` beside `size_is_const` — a fixed-array LENGTH (`[lim.MAX]int`), which needs the size at parse time when the imported package may not be parsed yet. Still refused, deliberately, but it used to fall through to a type parse and say `unknown type 'lim'`, blaming the package rather than the position; it now names the limitation and the two ways out. `[pkg.Type]` stays a dynamic array of an imported type — the discriminator is the type token after `]`, and `[store.Col]`/`[money.Account]` are everywhere. `tests/reject/fixarr_qualified_const.ty` |

Priority inside the list was `len` shadowing, because it was the only one that
produced a wrong answer instead of an error message. It is closed; the rest are
worked top to bottom by cost.

### 3. ~~The expressiveness gaps close, or become documented refusals~~ — **CLOSED 2026-08-15**

**Re-probed at HEAD** in the same program as §2: `sort.sort_by` with a named
comparator sorts (1), `is` answers on both `Option` and `Result` (true, true), a
`where numeric(T), defaultable(T)` generic folds with `zero$(T)` (6), and
`Result(void, string)` is the entry point's own return type. The rows below are
the history of how each closed.

**Re-probed against the tree on 2026-08-11** with one compiled program per row,
not inherited from the 2026-08-09 pass. Six of the seven rows had gone stale:
five closed by commits that landed after the check, and the sixth was recorded
against the wrong mechanism.

| Gap | State |
|---|---|
| `Result(void, E)` not expressible | **closed 2026-08-09** — `d868083` (`T_VOID` → `T_UNBOUND`) then `760bb83` (the spelling). Probe: `fn touch(x: int) -> Result(void, string)` returning `Ok()`/`Err("neg")` compiles and runs. The stated limit still holds exactly — `Result(Option(void), E)`, `fn g(x: void)` and `v : void = 0` are each refused with *"'void' is a type only as a Result's ok payload"*. See below |
| no comparator-taking sort | **closed 2026-08-11** — `e40f32d` added `corelib/sort/sort.ty@sort_by`, `fn sort_by(xs: [$T], cmp: fn($T, $T) -> int) -> [$T]`. Probe: sorting a `[Emp]` by its `name` field through a comparator gave `amy,zoe`, and `sort.asc` on `["pear","apple","fig"]` gave `[apple, fig, pear]` — the row's own complaint, sorting by a string key, needs no invented int in either form |
| an enum cannot be tested for its variant without binding a payload | **closed 2026-08-11** — `308b6d6` (`is` for user enums) and `6d275ca` (`is` for Option/Result). Probe: `s is Circle`/`s is Square`/`s is Dot` on a payload-carrying enum gave `true false false`, and `o is Some`/`r is Ok`/`r is Err` all answer, including on a `Result(void, E)`. The 2026-08-09 note called this "mitigated, stylistic"; `is` closed it structurally instead |
| two error types cannot share an `or_return` chain | **closed — and the row named the wrong mechanism.** `d806a4d` triaged it as WRONG MECHANISM: `corelib/result/result.ty@map_err` already bridged, and `6bc0a29` added `map_err_with` (`corelib/result/result.ty@map_err_with`) for the cause-preserving case. Probe: one `fn chain(k) -> Result(int, string)` whose first leg is a `Result(string, IoErr)` bridged by `result.map_err_with(read_it(k), ioerr_to_str) or_return` and whose second leg is a native `Result(int, string)` — both legs propagate through the same chain, the IoErr path surfacing as `missing key bad`. There was never a language gap here, only a missing combinator |
| `core:iter` unusable for a fallible pipeline stage | **closed 2026-08-11** — `6d498cf` added `corelib/iter/iter.ty:12@try_map` and `:27@try_filter`; `3bdfabc` flipped predicates to `bool`. Probe: `try_map` over `["1","2","3"]` gave `[1, 2, 3]` and over `["1","-2","3"]` short-circuited to `not positive: -2`; `try_filter` kept `[2, 3, 4]` and propagated `overflow guard` |
| `core:decimal` has no `div` | **closed** — `a8c761c`, confirmed by `b4d28e3`. `corelib/decimal/decimal.ty@div` is `fn div(a: Decimal, b: Decimal, scale: int, mode: int) -> Result(Decimal, DivErr)`. Probe: `div(10, 3, 4, 0)` gave `3.3333` |
| `[string]` cannot cross the FFI | **CLOSED 2026-08-11 — lifted for the PARAMETER direction, refused for the return.** `src/tychoc.c@ffi_arg_arr_ptr_ctype` answers `"const char *const *"` for `T_ARRAY_STRING`; a `[string]` argument now emits `(const char *const *)xs.data, xs.len`, the same `(ptr,len)` convention `[int]`/`[float]` use, **borrowed for the call** (unenforceable — stated in `docs/spec/14-ffi.md` §24.1). The return gate stayed `src/tychoc.c@ffi_arr_ptr_ctype`, which never answers for `T_ARRAY_STRING`, so `-> [string]` still dies (`tests/reject/extern_ret_arr_string.ty`). Fixture: `tests/ffi/main.ty@ffi_sfold`, non-empty and empty. **`core:os` was left alone here** — adopted afterwards by `9d63198`, which passes argv as a `[string]` and drops the builder handle |

One correction carried forward from 2026-08-09, and one new one.

`Result(void, E)` was recorded as "not expressible", which overstated it: a
one-field `Unit` struct worked with no compiler change, so the gap was
boilerplate rather than a wall. **CLOSED 2026-08-09**, in two commits.

```tycho
fn touch(x: int) -> Result(void, string):
    if x < 0:
        return Err("neg")
    return Ok()
```

The blocker named here was **`T_VOID` being overloaded**: it was both the
no-return type and the sentinel for *unbound generic type parameter*, so
binding a payload parameter to void would have read as "not yet bound". The
recommendation below — re-sentinel to `T_UNBOUND` — was right, and it was
**necessary but not sufficient**, which the table's "Buys" column had wrong.
`void` was not in the type grammar at all: `parse_type_inner` had no `void`
branch, so the three `== T_VOID` guards inside it were defensive-unreachable
and the collision was purely latent. Re-sentinelling cleared the trap; the
spelling still had to be built on top of it.

What shipped, with the surface chosen by the owner: `void` is spellable in
exactly one position, a `Result`'s ok payload, and it is a type with **no
values** — constructed by a zero-argument `Ok()`, matched by a bare `Ok:` arm,
never bindable. The permission is one level deep, so `Result(Option(void), E)`
is still refused. The `or_return` STATEMENT form (`f() or_return`, previously
rejected as a bare expression with no effect) came with it and is not optional:
with no value to bind there is no `x := f() or_return` to write, so without it
the feature would have had no usable propagation form. Spec:
[§5.3.6](../spec/03-types.md#536-enums-option-result).

The four options as they were tabled, for the record:

| Option | Cost | Buys |
|---|---|---|
| **Re-sentinel `T_VOID` → `T_UNBOUND`** *(taken)* | 6 sites, all reached from the single `new_binds()` — the estimate of ~8 was close | it cleared the latent trap. It did NOT buy "the expected spelling": that was the type grammar, the `Ok()` constructor, the bare `Ok:` arm, the `char okv` placeholder in the emitted C, and the `or_return` statement form — a second commit |
| New `T_UNIT` type | full compiler surface — codegen, eq, copy, `str` | not taken: same result, more surface |
| Ship `Unit` in corelib | tiny | not taken: removes the boilerplate, leaves the wart visible |
| Document the workaround | none | not taken |

An earlier note in this file called this "a spelling decision as much as a
type-system one", inferred from the compiler using the word "void" in its own
diagnostics. That was wrong — diagnostic text is not evidence about the type
system — and the sentinel collision is why.

#### A gap the `void` work left behind — **closed**

This probe found that `core:result`'s combinators could not be instantiated at
`Result(void, E)`: `is_ok` was a `match` whose `Ok(v)` arm had no value to bind
when the ok payload was `void`. **Both halves are fixed and the claim no longer
describes the tree.** `is_ok` is `return r is Ok`
(`corelib/result/result.ty@is_ok`) and `is_err` is `return r is Err`
(`corelib/result/result.ty@is_err`) — no `match`, so nothing to fail to bind. A
`touch` returning `Result(void, string)` now compiles through both:
`result.is_ok(touch(1))` and `result.is_err(touch(-1))` each answer `true`.

The second defect this probe filed — the diagnostic **naming the wrong file**,
sending a reader to line 71 of an eleven-line `main.ty` — is also fixed:
`cf42e2f` made a generic instantiation failure name the corelib file it is
really in, and `4a7cca0` added the instantiating call site as a `note:`, so the
message now names both the corelib line and the caller's own line.

#### Sizing `[string]` across the FFI — both answers, with costs

**Resolved 2026-08-11: lifting was chosen, and the sizing below held** — with one
correction found while implementing it. `ffi_arr_ptr_ctype` is also the *return*
gate, so widening it in place would have made `-> [string]` compile; the branch
went into a new param-only `ffi_arg_arr_ptr_ctype` instead. The rest of this
section is kept as the record of the two prices that were weighed.

**Lifting it is small — smaller than the row's "open by design" implies.** The
blocker is not representation. `runtime/tycho_rt.c@TychoArrStr` is
`typedef struct { char **data; tycho_int len; tycho_int cap; } TychoArrStr;`, and
`runtime/tycho_rt.c:1261@NUL` records that a Tycho string is NUL-terminated and its
pointer is a valid C `char *` with the header hidden behind it. So a `[string]`'s
`.data` **is already a `char **` of ordinary C strings** — no marshalling, no
copy, no ownership transfer. The change is one branch in
`src/tychoc.c@ffi_arr_ptr_ctype` returning `"const char *const *"` for
`T_ARRAY_STRING`; both call-emit sites (`src/tychoc.c@_xa` — the direct-call and
out-param-return emitters) are already generic over that function's result and would
emit `(const char *const *)xs.data, xs.len` unchanged. Comparable to `308b6d6`
(`is`): one resolver branch, one codegen path already written, a fixture.

Three things that must be decided with it, not after:

1. **`(ptr, len)`, not a NULL-terminated argv.** It would follow the
   `[int]`/`[float]` convention already at `src/tychoc.c:10359@arrp`. A callee wanting
   `execv` semantics appends its own `NULL`. Promising argv-shape instead means
   the emitter allocates a terminated copy, and that is a different, larger change.
2. **Borrow for the call; the callee must not retain.** Same contract
   `[int]`/`[float]` carry today. Nobody frees, because nothing was allocated.
3. **The return direction stays refused.** `[string]` *out* of C has no length
   header to reconstruct — the same reason `src/tychoc.c:4609-4610` bans a
   `char **` out-param. Lifting the parameter direction does not lift this one,
   and saying so is part of the change.

**Writing the refusal down is cheaper and buys less.** The shape is §4 and §5
above, both closed on 2026-08-10 as stated limits: a paragraph in
`docs/spec/14-ffi.md` and the `os` entry in `docs/reference/corelib.md`, naming the
permitted set and the reason, plus a pointer to the builder-handle pattern that
already works — `core:os` then declared `osx_argv_new` / `osx_argv_push` /
`osx_argv_free` / `osx_exec`, four shims and a push loop (retired 2026-08-11
once the lifting landed; see `corelib/os/os.ty`). Cost: one doc commit
and the two doc gates. What it does not buy: every future extern taking a string
vector pays those four shims again.

The asymmetry worth noticing is that the refusal was defensible when the cost of
lifting was unknown. It is now measured, and it is one branch.

"Documented refusal" is a legitimate answer for any row. An undecided row is
not.

### 4. ~~`core:net` gets a readiness call, or the cap becomes a stated limit~~ — **CLOSED 2026-08-10, as a stated limit**

`corelib/net/net_shim.c` has 12 exports and not one is `poll`/`select`/`epoll`
or `O_NONBLOCK`, so a server's worker count is a hard ceiling on concurrent
connections. That refusal was defensible and stands; what was not defensible
was leaving it implicit. It is now written where someone reaches for the
package — `docs/reference/corelib.md`'s `net` entry says N workers serve N
connections and one slow client occupies one worker for its whole request.

Readiness polling stays un-built: ~283 lines across 4 files plus a redesign of
`core:httpd`'s blocking read surface, and adding it later is **additive, not
breaking**, so it does not gate the freeze.

### 5. ~~Windows is at parity or its differences are non-goals~~ — **CLOSED 2026-08-10, as a documented limit**

`make ci` is green there. The one measured behavioural difference — a thread
parked in `recv` is not released by the shutdown handler as it is on Linux, so a
Windows server winds down within its idle timeout rather than within a
millisecond — is a **documented platform limit**, not a bug to fix. Nothing is
lost or corrupted; the wind-down is slower and only that. It is stated in the
README's platform notes, and the measurement stays in
[SECURITY.md](../../SECURITY.md).

### 6. ~~A release actually ships, and the support policy is exercised once~~ — **CLOSED 2026-08-15**

Both halves are now real. **A release shipped**: v0.7.0, prerelease, Linux and
mingw tarballs with checksums, verified before and after publishing (see below).
**The support policy was exercised**: `sort.by_key` carries a `# deprecated:`
marker, every call site draws a warning naming `sort_by` *and* the removal
version, and `CHANGELOG.md` records it — re-probed at HEAD, the warning still
fires with its replacement text.

**The deprecation half is DONE, 2026-08-10.** `sort.by_key` is deprecated
through the full path — a `deprecated:` notice in its doc comment, a
`CHANGELOG.md` entry naming `sort_by` as the replacement, a compiler warning at
every call site, and removal scheduled for 1.0. Step three stopped being
special-cased on the way: a `# deprecated: <text>` line directly above a `fn`
marks it, so the next deprecation costs one comment
(`tests/warn/deprecated.ty`).

v0.7.0 was published on 2026-08-15, alongside v0.5.0 and v0.6.0. Release state
is `gh release list`, never this file. The version
constant is now `0.7.0` (`src/tychoc.c@TYCHO_VERSION`), and this section
described `v0.5.0` until 2026-08-14.

**Both tarballs were REBUILT from HEAD on 2026-08-14 and verified.** They had
gone stale in the way that matters: the previous
`dist/tycho-v0.6.0-linux-x86_64.tar.gz` verified against its `.sha256` and
reported `0.6.0`, yet its `tychoc` **accepted all four** of
`tests/reject/generic_ret_handle.ty`, `generic_struct_field_chan.ty`,
`generic_sink_affine.ty` and `generic_bounded_field_degraded.ty`. Two of those
are run-time memory errors — a double free and an aliased channel — so it would
have shipped known memory-safety bugs under a version whose CHANGELOG says they
are fixed. A checksum proves an artifact is intact, never that it is current.

**Rebuilt and re-verified again on 2026-08-15**, and the reason is the same one
this section already records: four commits landed after the 08-14 build, two of
them in `corelib/crypto/`. The tarball on disk verified against its `.sha256`,
reported `0.6.0`, and **shipped a `crypto_shim.c` that left the plaintext in freed
heap and decoded key hex in non-constant time** — both fixed in the tree, neither
in the artifact. Publishing it would have shipped two known crypto defects under a
version whose CHANGELOG says they are fixed. **A stale artifact is the default
state of this directory, not an accident**: every commit re-creates it.

`make release-check` now **rebuilds the Windows archive too**, or says loudly that
it did not. It previously rebuilt only the host archive and still printed
"byte-identical archives", which is how the mingw tarball sat a day behind the
linux one without anything noticing.

State now, each figure measured rather than assumed:

- `make release-check` reports **byte-identical archives** over two builds, and
  rebuilds the mingw archive in the same run.
- The whole **`tests/reject/` corpus — is refused by the extracted
  `tychoc`**, not just the six named above. That number means nothing on its own,
  so a POSITIVE CONTROL runs beside it: a program importing `core:sort` and
  `core:crypto` compiles and runs through the same shipped binary, printing
  `[apple, fig, pear]` and a true AEAD round trip. Without it, "0 accepted" is
  what a compiler that cannot start also reports. **That control is what found the
  cross-file deprecation defect** (`3a1b57e5`) — it was not looking for it.
- Under wine the extracted `tychoc.exe` reports `0.6.0`, refuses all **51**
  `affine_*`/`generic_*` fixtures, and emits C for the positive control. The first
  attempt at this reported "0 wrongly accepted" while every invocation was in fact
  `command not found` — zsh does not word-split an unquoted command string, so the
  sweep was vacuous and the positive control is the only reason that was caught.
- The extracted native `tychoc` reports `0.6.0`, **refuses all six** of the
  affine/generics reject fixtures, and still finds `corelib` beside itself with
  no `TYCHO_CORELIB`, compiling and running a `core:sort` program.
- The `mingw64` cross-build exists again at 0.6.0 (0.5.0 had one, 0.6.0 did not).
  Under wine its `tychoc.exe` reports `0.6.0` and refuses the same fixtures.

**Shipped 2026-08-15 as 0.7.0, not 0.6.0.** v0.6.0 had been tagged and released
on 2026-08-11 and HEAD was 254 commits past it, with breaking changes in
`[Unreleased]` — a copied `handle` and a copied channel, both live memory errors,
stop compiling. Publishing the newer artifacts under the older tag would have put
binaries on a release whose tag names different source, which is the failure this
section already records twice. So the version moved instead.

Verified before publishing, in this order: `make release-check` (byte-identical
over two builds, both platforms), the whole `tests/reject/` corpus through the
**extracted** compiler (333, 0 wrongly accepted) with a positive control that had
to compile and run, the mingw archive under wine (0.7.0, 51 fixtures, 0
accepted), and — after publishing — the tarball downloaded back from the release
page and re-checked. `make test` (719), `asan-self` (737), `fuzz/run.py 120`,
`fuzz-shims` and `ilp32` all green at the released tree.

This section's own rule held again on the way: the version grep used to bump
0.6→0.7 matched `Tycho 0.6` and missed `Tycho is 0.6`, so five files still
announced the old version after the release was cut. A checksum proves an
artifact intact and a grep proves only what its pattern can match. **Re-verify before publishing** — extract the tarball and run those reject
fixtures through the shipped compiler. That check is what caught this, and a
checksum would not have.


### Order of work

Set 2026-08-09, cheapest-and-safest first, so each step is verifiable before the
next depends on it. Compiler changes are ordered late within each group because
every one costs a citation re-anchor (~50-110 anchors move) and a full `make ci`.

1. ~~`len` shadowing~~ — **closed**, `ec4914f`. The only silent-wrong-answer item.
2. ~~expression line continuation~~ — **closed**, `3c2ea6d`.
3. ~~`Result(void, E)`~~ — **closed**, `d868083` (re-sentinel) and the commit
   after it (the spelling). The re-anchor cost the table above warned about was
   real: the first commit was written line-count-neutral and moved zero
   citations; the second moved 87 files' worth.
4. ~~`core:sort` comparator~~ — **closed**, `sort.sort_by(xs, cmp)`. The gate
   prediction held exactly: corelib only, no citation moved, `make corelib` and
   `make corelib-examples` were the whole verification.
5. ~~`pass`~~ — **closed 2026-08-10**, and it was NOT a real keyword: two files
   already used the name, so it is contextual like `const`/`delete`. Cost was the
   predicted surface minus the lexer: parser, spec, `appendix-b-keywords`, the
   grammar appendix, `tycho-lsp`, `editors/` (both grammars, zed regenerated),
   fixtures, goldens. `tychofmt` needed nothing — `pass` is one token on a line.
   ~~Then `defer`~~ — **refused 2026-08-10, and the refusal is the point.** This
   page called it "the largest item on this page" and priced the full keyword
   surface. That was scoping a feature nobody had asked what it was for. An
   arena-scoped language with RAII handles has no work for it: memory is
   arena-freed at scope exit, a `handle` runs its declared `free:` at scope exit,
   and channel/task handles are affine with an implicit join. **This was already
   found once** and filed anyway — `docs/internals/plan-tycho-vm-DONE.md:27-32`
   ends "nothing needs it; it was filed because the probe surprised me, which is
   not a reason." Leaving the row open is what made it get re-scoped; it is a
   documented refusal now.
6. ~~Then the remaining 1.0 conditions: `core:net` readiness (§4), the Windows
   parked-`recv` decision (§5), and shipping a release plus one exercised
   deprecation (§6).~~ — **all three closed.** §4 and §5 on 2026-08-10 as stated
   limits; §6 on 2026-08-15, shipping **0.7.0** rather than the 0.5.0 this line
   named when it was written.

**This list is finished.** Every item on it is closed, and §2 and §3 were
re-probed at HEAD on 2026-08-15 rather than inherited from their tables — 254
commits had landed since the last pass, and a table probed that long ago is a
claim about the past.
