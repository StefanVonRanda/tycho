# Roadmap

Tycho is **0.8.5 — pre-1.0**, and this file is a direction, not a promise of
dates. Day-to-day work is tracked in [GitHub
issues](https://github.com/StefanVonRanda/tycho/issues); this file is the
high-level shape. What 1.0 now requires is [below](#what-10-requires).

## Where it is

The language and the 45-package core library are
feature-complete for the thesis they exist to prove. The current strength is the
correctness harness — an adversarial fuzzer, sanitizer lanes over both the compiler and
the programs it emits, and a golden-locked test suite, all green in a local gate
(`make ci`). See [docs/architecture.md](docs/architecture.md) for what each gate proves.

**Production readiness.** Of the seven things §"What 1.0
requires" asks for, §2 through §5 are closed and §6's artifacts are built and
verified for both platforms. What is left is not engineering: **§1** (someone
other than the author writes a real program), **§7** (an external security
review). Those need
another person or the owner, so the tree cannot close them on its own.

The last engineering sweep before that line was the **fail-open audit**: every
corelib parse function and every caller that reads persisted or wire data,
checked for a parser that returns a plausible wrong value instead of an error.
It found a build tool accepting a damaged mtime (a stale output shipped as
current), a database lexer turning an out-of-range literal into `0` (a query
matching the wrong rows), and the money type's only text constructor returning
`0.15` for `"1.5x"`. All three are fixed and gated; the pattern and the
`_checked` convention are written up at FRICTION #4 and #56.

## The self-hosted compiler

`compiler/` is Tycho written in Tycho — 21,498 lines across 13 files in six
packages (`lex`, `parse`, `ast`, `types`, `emit`, `driver`), built as `tychoc1`.
It has been absent from this roadmap, which understated it: **it is not a side
project, it is already the compiler the gates run.** `tests/run.sh`,
`corelib/run.sh`, `examples/corelib/run.sh` and `server/run.sh` every one
default to `TYCHOC="${TYCHOC:-./tychoc1}"`, so `make test`'s 1039 fixtures, the
corelib suite and the server lane are compiled by the self-hosted compiler, not
by `src/tychoc.c`. `src/tychoc.c` is what bootstraps it and what it is scored
against.

**Measured, not asserted:**

- **The bootstrap is two-stage and the fixpoint holds.** `./tychoc` emits
  stage 1; stage 1 emits the shipped `tychoc1`, so a defect in tychoc1's own
  code generation reaches the binary you run rather than hiding a generation
  back. Re-measured 2026-09-11: `tychoc1` compiling `compiler/main.ty` produces
  a binary **byte-identical to itself**. (Three further generations were
  measured byte-identical on 2026-08-30 —
  [architecture](docs/architecture.md#bootstrapping).) **Now gated**, as
  `make fixpoint-check`, `[1e/13]` in `make ci`: it compares the emitted C
  rather than binaries and costs **~2.3s** — the estimate when this was raised
  was "one more compiler build, one to two minutes", and that was wrong by two
  orders of magnitude because the one generation it builds only has to run, so
  it is built `-O0`. Until 2026-09-11 nothing asserted it, which is the worst
  shape for an invariant: a bootstrap that stopped converging would leave every
  other lane green.
- **Two gates carry it.** `make parse-check` scores its front end against
  `./tychoc`'s own verdicts file by file over the whole tree, with an AST census,
  a resolution census and a type census against recorded goldens — an accept/
  reject verdict is blind to a parse that succeeds with the *wrong tree*.
  `make tychoc1-check` substitutes it into the real runners (conc, ffi,
  recursion, entrypoints, corelib, corelib-ex, server) rather than
  reimplementing their judgements.
- **The two compilers are held to agreeing.** Where they diverge it is a defect
  in one of them: FRICTION 87 was a user-facing diagnostic the two worded
  differently, found because a `tests/diag/` golden can only match one, and
  closed by fixing `src/tychoc.c`.

**Decided 2026-09-11: `tychoc1` stays the second opinion, and does not become
the successor.** Two implementations over one corpus is the same differential
instrument as native-vs-ASan, `dis` round-tripping `asm`, and tycho-scheme's two
backends agreeing — and it is the strongest one here, because there is no hosted
CI and no second reader. The parity work is not a tax on that; it *is* the gate.
FRICTION 87 is the evidence: the divergence found a worse diagnostic in the
shipped compiler and fixing it improved what users get, on the first instance of
paying the cost. Demoting `src/tychoc.c` to a bootstrap would retire the oracle
in exchange for not maintaining the thing producing the findings — and would
make 21,498 lines of Tycho, rather than one dependency-free C file, what people
actually run.

What that decision required writing down is the **asymmetry**, now in
[architecture](docs/architecture.md#which-compiler-is-right): `src/tychoc.c` is
normative, `tychoc1` is the oracle, and when they disagree the question is
*which is right* — not *make `tychoc1` match*. That is already how 87 was
resolved (`tychoc1` was right; the reference changed), so this records the
practice rather than inventing one.

## The language surface moved once, deliberately

From 2026-08-22 the keyword set, the builtin set and every corelib signature were
locked by `surface.lock` and gated by `make surface-check`, with **no new language
features before 1.0**. That held until 0.8.5, which broke it on purpose: the layout
and SIMD work (`packed`, `align(N)`, `vector[N]T`, simultaneous assignment,
swizzling) needed surface, and taking it before 1.0 was judged better than not
being able to take it after. The lock still gates — it records what was added
rather than forbidding additions.

The reason is measured, not stylistic: in the ten days to 2026-08-22, 91 commits
touched `docs/spec/` and 69 touched `src/tychoc.c`, 11 of them adding compiler
features. A surface moving that fast cannot be learned, documented, or depended
on, and the thing 1.0 is actually waiting for is programs written by people who
are not the author.

Corelib may still GAIN functions — a gap like a missing `mkdir -p` is worth
filling. It may not lose one or change a signature. Since 2026-09-03 a gained
function must be RECORDED in the same commit: an addition the lock has not seen
fails the gate, because until then it printed a note attached to no verdict and
22 of them accumulated under a permanently green lane.

Taking new surface is deliberate: `python3 scripts/surface_lock.py --record`, and
the diff says exactly what grew.

### Taken since the lock

All three have now shipped. They are kept here as the record of what the freeze
was broken for, and what each cost:

- **Alignment and packed layout.** The `packed` half SHIPPED on 2026-09-04 and
  is the freeze's first deliberate exception: `packed struct` is a declaration
  attribute that gives an aggregate a byte-exact C layout
  ([spec 17.1a](docs/spec/12-aggregates.md#171a-packed-layout)), recorded in
  `surface.lock` in the same commit. It was measured first: 141 sites across 7
  files hand-assemble a binary record one byte at a time today. The `align` half
  SHIPPED on 2026-09-05: `align(N) struct` raises an aggregate's alignment
  without ever lowering it below what the fields require
  ([spec 17.1a-2](docs/spec/12-aggregates.md#171a-2-stated-alignment)). It
  shipped without the caller it was once claimed to have -- vectors turned out
  not to need it -- and the ceiling is proved where alignment ENTERS the type
  system rather than at the 61 arena_alloc emit sites (commit e185caa2).
- **Vectors.** SHIPPED on 2026-09-04, the freeze's second deliberate exception.
  `vector[N]T` is a fixed array whose arithmetic lowers to one machine vector
  operation ([spec 5.3.11](docs/spec/03-types.md#5311-vectornt)); the count is
  generic and power-of-two constrained, which is where Zig, Odin and Rust's
  portable SIMD all landed, and a width the target lacks is split by the C
  compiler below us. `vector` is recorded in `surface.lock` in the same commit.
  The claim that this needed `align` first turned out to be FALSE, and measuring
  it is what settled it: the emitted aggregate is pinned to the 8-byte alignment
  the arena already guarantees, and the arithmetic is still a single instruction
  there. No allocator change was made.
- **Groups.** SHIPPED, the freeze's third deliberate exception. Simultaneous
  assignment writes 2-8 places at once, and the swizzle form permutes 2-8
  components of one value
  ([spec 17.5](docs/spec/12-aggregates.md#175-destructuring)); vector lanes carry
  `.x .y .z .w` / `.r .g .b .a` names on top of it (commit 0c5ff854). It replaces
  the temporaries a swap needed.

`packed`, vectors, `align` and groups are all done, and the queue behind the
lock is empty. Nothing further is planned before 1.0.

Not queued: compile-time execution, and field promotion (`using`). The first is
deferred, the second refused -- a bare field name should say where it came from.

## Near-term

Foundation before feature breadth. In rough priority:

1. **Demand-gated corelib / tooling.** New library and tooling work is built against a
   real program that needs it, never ahead of one — e.g. more image codecs, richer
   date/time, additional networking. Requests belong in the issue tracker.
2. **Keeping the compiler honest.** As new language features land, each is
   adversarially fuzzed and gated against recorded goldens before shipping. This is
   ongoing, not a milestone.
3. **Overloading — open, demand-gated.** A name may have exactly one definition, so
   `show(Point)` and `show(Name)` cannot coexist. Since a generic body type-checks
   *after* substitution, that one-definition rule — not the closed constraint set — is
   what stops a generic from running over several user types. It is the small feature
   that sits where traits were asked for
   ([non-goals](docs/architecture.md#decided-non-goals)). The trigger is a real program
   that needs it and hits the wall, not a design impulse; like the corelib, it is built
   against a program that wants it or not at all.

## What 1.0 requires

The number was declared on 2026-08-05 and withdrawn on 2026-08-09. What was
wrong with it is worth stating precisely, because it decides the list: **the
engineering was never the problem.** Two platforms green under `make ci`, a
200-seed adversarial fuzzer with zero findings, sanitizer lanes over the
compiler and the programs it emits, 45 corelib packages each tested
and golden-locked, and a normative spec the implementation is gated against —
that is a stronger evidence base than most languages have at 1.x.

What was missing is that **1.0 is a promise not to break people, and there were
no people.** `git tag` showed `v0.1.0` and nothing else; the `[1.0.0]` changelog
entry described a release that was never cut. A freeze nobody has pulled on is
not a freeze, it is a guess about which parts matter.

So the conditions below are mostly about contact with reality, not about
building more.

What remains for 1.0 is §1 and §7, and neither is sequenced here because neither
is a task this list can complete. §1 gates the freeze, not the work: it wants a
program by a second author, so a fourth program by the same one does not advance
it. §7 wants a reviewer who is not the project. Both need a person;
`docs/internals/audit-brief.md` is what §7's needs handing to them.


### 1. Someone other than the author has written a real program

The blocking one, and nothing else on this list substitutes for it. Every
ergonomic finding in [docs/internals/FRICTION.md](docs/internals/FRICTION.md)
came from the author dogfooding against a program he also designed. That
catches a lot — the file is unusually honest about its own defeats — but it
cannot catch what only a stranger's mental model produces.

Concretely: **three non-trivial programs written against the docs alone**, each
with its friction written down, and each targeting a different half of the
surface. Until then the API freeze is guesswork about which parts of the surface
people actually reach for.

The original wording asked for two *people*. No second human reviewer was found
— the author tried and could not recruit one — so this condition is met by
programs written by LLM agents working from `docs/` without reading
`src/tychoc.c`. That is a weaker instrument and the weakness is stated here
rather than hidden: an agent prompted from the same docs by the same person
converges on the same paths, which is why the requirement is *different halves
of the surface*, not merely three programs. A fourth program over ground already
covered is worth less than the first one that reaches an untouched part.

**One of the three exists**: `tools/tycho-diff`, a Myers O(ND)
line differ with unified output, written against `docs/` alone by someone who had
not read `src/tychoc.c`, with its friction in
[tools/tycho-diff/FRICTION-OUTSIDE.md](tools/tycho-diff/FRICTION-OUTSIDE.md).
Four first-contact findings, all diagnostics and all in the first ten minutes: the
`die` collision warning offers a remedy that `package main` forbids; a missing
import is reported as a missing SYMBOL; `eprintln` does not exist and the
suggestion points at `println`, the wrong stream for an error; and `main` cannot
return a status while the error names `Result(void, string)` without mentioning
`exit(code)`, which is what every CLI in `tools/` actually uses. The report also
records what went RIGHT, because a friction log that only complains is not
evidence: value semantics made the algorithm's frontier snapshot
(`push(trace, v)`) correct as written, which is the thesis doing its job.

**A second landed the same day**: `tools/tycho-hash`, sha256 over a directory
tree by a worker pool, written for the OTHER half of the surface — `spawn`,
channels, backpressure, and the determinism a parallel tool has to prove. Its
[FRICTION-OUTSIDE.md](tools/tycho-hash/FRICTION-OUTSIDE.md) repeats none of
tycho-diff's four, which is the argument for a second program rather than a
longer first one: `io.list` is the directory listing but its name contains no
"dir" so no search finds it; it returns `[]` for a directory it cannot read,
where everything around it returns `Result`; `sha256.hex` takes a string where a
file is bytes; and a spawned fn must return a value while a task cannot live in
an array, so a worker pool is a fixed list of named spawns whose width cannot be
computed — `parallel for` fans out properly but reduces only int/float, so
between them no shape both fans out dynamically and returns non-scalar results.

It also records a mistake worth more than the findings: the first version's
`--workers` did nothing, so the determinism check compared eight workers against
eight, five times, and passed. The lane now reads the per-worker split back, and
the width-1 case (first worker takes ALL, rest none) is the negative control for
the option itself.

**A third landed 2026-08-15**: `tools/tycho-fold`, wrapping text by CODEPOINTS,
chosen for a surface neither of the others touched — UTF-8, where the bug is that
everything looks right until the input stops being ASCII. Its
[FRICTION-OUTSIDE.md](tools/tycho-fold/FRICTION-OUTSIDE.md) again repeats none of
the earlier findings: `utf8.decode` returning `nb <= 0` on invalid input is an
infinite loop for a caller who trusts it, and every ergonomic path (`len`, `s[i]`)
is byte-based while the codepoint count lives in a separate import — correct
layering that makes the NATURAL program the wrong one, wrong only on input the
author probably never tried.

**Four now exist, across four different halves of the surface** — `tycho-diff`
(algorithms), `tycho-hash` (concurrency), `tycho-fold` (text and codepoints), and
the 2026-08-19 FFI probe ([record](docs/internals/probe-ffi-2026-08-19.md);
its program was thrown away, the record being the artifact). By the letter above
that meets the requirement. It is left OPEN deliberately: the probes keep
returning things the author could not have found, and
[docs/internals/probe-procedure.md](docs/internals/probe-procedure.md) shows the
surface still at zero — generics, newtypes, `subscript`, `bounded[N]`, `select`
and the enum/Option/Result error paths have no non-author program at all. Closing
it at four would stop the one instrument that is working.

What they established is worth more than the count. Across `tycho-diff`,
`tycho-hash` and `tycho-fold`, **every one found the CHECK harder to get right
than the code** — a determinism sweep that compared eight workers against eight,
a word-set comparison that flagged 111 legal hard breaks, a `\xff` fixture that
was valid UTF-8 under dash. Each was caught by asking what the check would show
if the feature were broken, and that is the reusable finding. The fourth makes
the same point from the other side: its most useful finding was not in its own
report at all — its C shim returned a pointer into a directory stream it had
already closed, and only ASan saw it.


### 2–6. ~~Papercuts, expressiveness, `core:net`, Windows, a shipped release~~ — **all CLOSED**

Closed between 2026-08-10 and 2026-08-15, in that order. The record of each —
what was measured, what was refused and why, and the commit that closed it — is
[`docs/internals/roadmap-closed-2026-08.md`](docs/internals/roadmap-closed-2026-08.md).

### 7. An external security review

[SECURITY.md](SECURITY.md) says plainly that there has been no third-party
audit. The internal review was real and found real things — the FFI ownership
conventions were checked per shim, and the shell-injection class it flagged was
closed on 2026-08-09 by `core:os`'s argv path — but the FFI boundary is unsafe
by design and nobody outside the project has looked at it. 1.0 invites people
to build on that boundary.

**A packet for a reviewer now exists**:
[docs/internals/audit-brief.md](docs/internals/audit-brief.md) — threat model,
where untrusted bytes reach hand-written C, what each existing lane does and does
not cover, how to reproduce any of it, and five suggested starting points. It
does not close this section; it removes the excuse that "get an audit" had no
scope attached. Three internal passes found 5, 9 and 10 issues respectively with
zero overlap between them, which is the argument for §7 stated as evidence.


### What is explicitly NOT required

More features, more corelib packages, more benchmarks, or a package manager.
The language is feature-complete for the thesis it exists to prove. Adding
surface before anyone has used the existing surface is how the freeze gets
guessed wrong a second time.

## What production-ready requires

Set 2026-08-15, and it replaces the premise the section above was written under.
**The thesis is proven** — hierarchical implicit arenas under value semantics are
a viable memory-management strategy, and that phase is closed. The goal now is to
ship a complete, production-ready language.

That reframe invalidates a justification, not a decision. "What is explicitly NOT
required" below still reads *"the language is feature-complete for the thesis it
exists to prove"* — feature-complete for a thesis is not feature-complete for
production, so that sentence no longer settles anything and the Non-goals list
resting on it is open for re-decision. Left standing deliberately: those are the
owner's calls, not a documentation edit.

### 1. It runs where developers are

Artifacts exist for `linux-x86_64`, `mingw64-x86_64` and **`darwin-arm64`**, the
last built 2026-09-19. **None of them is PUBLISHED for macOS yet** — the archive
is built, reproducible and content-gated, and `gh release create` is the owner's
call. A language that cannot be installed on an Apple laptop or a Graviton
instance is not production-ready whatever its internals are; half of that gap is
now closed, and **ARM64 Linux is no longer compile-only**: `make test` is
1065/1065 on Ubuntu 26.04 / aarch64, in a VM on the Apple Silicon box, at native
speed. No Graviton artifact is BUILT yet, but the "never been run" half of that
risk is gone (FRICTION 110).

**"Not shipped" and "not run" are different claims, and as of 2026-09-19 the
second one is closed.** `make ci` was run on `darwin-arm64` that day and is
**green**, repeatedly measured between 267s and 390s (that spread is FRICTION
91, not a regression); both compilers build native Mach-O arm64 with no source
change and `make test` is 1065/1065. That green carries **12 skips over 7
causes**, each printing its reason, and one is a genuine coverage loss:
**Apple's ASan ships no LeakSanitizer**, so the leak lane and two sanitizer legs
do not run there and Linux remains the only host that scores them. The others
are lanes with no subject on this platform — gdb, the glibc symbol floor,
`ilp32`, `resource.prlimit`, an x86-64 `--target` leg, and `-Wl,--wrap`, which
ld64 does not implement.

**It was not free**: the run found three defects — FRICTION
[100](docs/internals/FRICTION.md), 101 and 102 — two of them latent in shipped
code, including a listen backlog that made every Tycho server refuse peers under
a burst on any BSD-derived kernel. What remains is packaging, not porting.

**Why the native build needed no porting**, which is what the 2026-09-19 run
confirmed rather than discovered:

- `runtime/tycho_rt.c` already carries **explicit macOS support** — the Darwin
  `sys/ucontext.h` include, `pthread_get_stackaddr_np` for the stack bounds, and
  per-architecture stack-pointer extraction for **x86_64, i386 and arm64** — plus
  a generic `__aarch64__` branch for Linux. Someone wrote for these platforms.
- `src/tychoc.c` has **zero** platform conditionals, so the compiler itself has
  nothing to port.
- With `zig cc` as the cross driver, `src/tychoc.c` builds clean for
  `aarch64-macos`, `x86_64-macos` and `aarch64-linux`, and so does the **emitted
  C including the embedded runtime** — three targets, no source change.

**This paragraph used to end by saying none of those binaries had been RUN —
"there is no Darwin or ARM machine here and no qemu" — while the top of the same
section claimed `make ci` had been run on macOS.** Both sentences sat here at
once; the second was added without retiring the first. The 2026-09-19 run
settles it on the evidence: a real `darwin-arm64` machine, the whole gate, and
three defects that no amount of cross-compiling would have surfaced — which was
precisely the risk the retired sentence named. `aarch64-linux` stopped being compile-only the same
day, in a VM on that same Mac — which is the answer to "there is no ARM machine
here": there was, it just needed a hypervisor. **`x86_64-macos` went the same
day**, under Rosetta: 288 fixtures built through an x86_64 compiler and matched
their goldens. **All three targets have now had binaries RUN**, and doing so
found a float-determinism defect that no single machine could have shown
(FRICTION 112-113).

### 2. ~~A story for using other people's code~~ — **DECIDED 2026-08-15: vendoring, Odin-style**

**The decision, from the owner:** follow Odin and Go — *vendor your dependencies
like Odin, and ship an extensive core library like Go.* No package manager, no
registry, no lockfile. A dependency is a directory you commit.

**It already works, and needed no compiler change.** `resolve_pkg_dir`
(`src/tychoc.c@resolve_pkg_dir`) resolves a non-`core:` import relative to the
importing FILE's directory, so `vendor/` is a directory like any other:

```
app/
  main.ty          package main; import "vendor/a"
  vendor/
    a/a.ty         package a;    import "../b"      <- a sibling dependency
    b/b.ty         package b;    import "core:strings"
```

Verified end to end, not assumed: a vendored package reaches `core:` exactly as a
top-level one does, and reaches a sibling by relative path. `tests/pkg/vendor_deps/`
gates both shapes — it is a convention nothing enforced, so it worked by
construction and would have broken in silence.

**What this buys, and what it costs.** No resolver, no version solver, no central
index to run or trust, and a build that is exactly what is committed. The cost is
transitive-dependency management by hand and no mechanical way to learn that a
vendored copy has a security fix upstream — the same trade Odin makes, taken
deliberately rather than by omission.

**Written and gated.** The convention is in
[docs/reference/packages.md](docs/reference/packages.md#vendoring-goodin-style),
reached from the reference index; `tests/pkg/vendor_deps/` gates both shapes.

### 3. The promise, written down

The deprecation machinery is real and exercised end to end — `sort.by_key`
(`corelib/sort/sort.ty:109`) and `decimal.from_str`
(`corelib/decimal/decimal.ty:32`) each carry a `# deprecated:` marker, every call
site draws a warning naming its replacement and its removal version, and
`CHANGELOG.md` records it.

The policy that machinery serves is written: [SUPPORT.md](SUPPORT.md) states what
may break in which release, that a deprecation survives until the next major,
that Linux x86_64 is the one supported platform and everything else is
best-effort, and that there is no support window for old versions. It also says
plainly that the 1.0-onward promises are not yet ones you can rely on at 0.x.

### 4. Trust that did not come from here

§1 and §7 of the section above survive the reframe unchanged, and matter more
under it rather than less. Shipping to strangers is precisely the situation in
which an unaudited FFI boundary is a liability, and a language whose only
programs were written by its author has not been used, only demonstrated.
[docs/internals/audit-brief.md](docs/internals/audit-brief.md) is the packet for
the second of those.

## Non-goals

Several things are deliberately, permanently out of scope — traits, a package manager, a
C-style ternary, Hindley-Milner inference, refcounting/GC, and hosted CI among them. The
full list with rationale is in [docs/architecture.md](docs/architecture.md#decided-non-goals).
Please don't open issues proposing them; issues that explore *new* directions are welcome.

**Traits came off that list on 2026-09-11 and went back on the same day**, and the round
trip is recorded rather than erased. The old rationale — the language is feature-complete
for its thesis — was retired by the reframe above and cannot be cited again. The new one
does not depend on it: the *dynamic* form (an interface value in a field) is impossible
under value semantics rather than merely declined, the *static* form is largely already
present because generic bodies type-check after substitution, and open-world extension is
the one thing traits uniquely buy in a language that has deliberately no registry to extend
from. The argument and its measurements are in
[docs/architecture.md](docs/architecture.md#traits--typeclasses--reopened-2026-09-11-re-closed-the-same-day).
