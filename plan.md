# Make Tycho able to write a real web server

Follows the front-door-defects plan (archived: `docs/internals/plan-front-door-DONE.md`,
42 phases, head `894a767`). That plan hardened the compiler. This one is the opposite
posture: **the deliverable is a program, and the language work exists only to unblock it.**

Direction set by the user, 2026-07-25: *"we should stop working on tychoc0, its done its
job … we start building useful software and validate the language that way. My biggest
concern about tycho is its ergonomics, the syntax, expressiveness, readability and
general UX."*

## Goal

An honest-to-goodness web server written in Tycho — concurrent, serving a real directory
of real files (HTML, CSS, JS, **images and fonts**), with correct content types,
persistent connections, and sane behaviour under malformed input. Done = it serves a real
site to a real browser, and we have an honest written account of what writing it felt
like.

## Anti-scope — read this before adding a phase

The previous plan grew from 6 phases to 44 because every phase filed more. That is the
failure mode to avoid, not a template. Rules for this plan:

- **A phase belongs here only if the web server cannot be written without it.** Not "is
  wrong", not "is inconsistent" — *blocks the program*.
- **No conformance audits, no citation sweeps, no gate-building** unless a gate is the
  cheapest way to keep the server working.
- Discovered defects that do NOT block the server get one line in `FRICTION.md` and are
  left alone. That file is a deliverable; fixing everything in it is not.
- **Stop condition: the server serves the site.** Not "the language is good."

### GATE CONSTRAINT — user directive, 2026-07-26, binding on me and every subagent

**`make ci` and `make test` run AT MOST ONCE PER DAY.** Not per phase, not per commit —
per day, across all agents. Violating it means the gates get removed from the project
entirely, which is the user's call and a reasonable one.

Every phase prompt in this session before this date told its agent to "run the full gate
set, one per command, foreground". That was wrong: a docs-and-Makefile change was paying
for `ilp32`, `asan-self`, `fuzz` and `bench-guard`, none of which can catch anything such
a change could break. Do not reproduce that boilerplate.

What replaces it: **run the thing you built.** For this plan that means starting the
server and hitting it with `curl` — faster than any gate, and it actually answers the
question. A phase that changed one corelib function verifies by calling that function.
Record what you ran and why it was the right check; "gates not run, daily allowance spent"
is a complete and correct justification, not a gap to apologise for.

## What the probes already established (2026-07-25, measured not assumed)

Written while trying to stand up the simplest possible concurrent server.

| # | Finding | Evidence | Blocks the server? |
|---|---|---|---|
| 1 | `spawn` gives **real parallelism**, and a task can own a socket fd | two 600ms handlers: **1214ms sequential vs 609ms concurrent** | no — this is the good news |
| 2 | **Task handles are affine**: cannot be stored in any container or aggregate | `push(ts, spawn work(1))` → `a task handle cannot be stored in a container or aggregate -- wait(t) first`; `[]Task(int)` → `unknown type 'Task'` | **shapes the architecture** |
| 3 | **`httpd` bodies are `string`-only** — `Request.body`, `Response.body`, and `render()` builds the whole response as a string before `to_bytes` | `corelib/httpd/httpd.ty:30,:37`, `:208`; its own header admits an interior `0x00` truncates | **YES — cannot serve a favicon** |
| 4 | **No content-type mapping** — defaults to `text/plain` | `httpd.ty` `render()`; grep of `corelib/*/*.ty` finds no MIME table | **YES — browsers show HTML as source** |
| 5 | **No keep-alive** — the connection is closed after one request | no `Connection`/`keep-alive` handling anywhere in `httpd.ty` | **YES for a real site** (dozens of assets per page) |
| 6 | **No `sleep` anywhere user-facing** | exhaustive grep of `corelib/`, `runtime/`, `docs/spec/`: `nanosleep` exists at `runtime/tycho_rt.c:822` for `select` only | no, but no backoff/shutdown either |
| 7 | **`handle` is a reserved keyword** — cannot name a function `handle` | `fn handle(...)` → `error: expected a procedure name`, which never says why | no — pure UX |
| 8 | `package main` makes the compiler compile **the whole directory** | stray probe files in the same dir were pulled into the build | no — Go-like, by design |

## Pre-flight

- **Phase 1 changes a published corelib API.** `Request.body`/`Response.body` going from
  `string` to `bytes` breaks every existing consumer: `examples/webserver/main.ty`,
  `examples/site`, `examples/weblog`, `corelib/test/httpd`. They must land in the same
  commit or `make corelib`/`make test` go red. This is the one phase with real blast
  radius; the rest are additive.
- **Unknown, resolved by Phase 1:** whether Tycho's `bytes` is ergonomic enough to build a
  response with. If concatenating `bytes` is painful, the server's whole IO path is
  painful, and that is itself a headline ergonomics finding — record it either way.
- Reversibility: git; one commit per phase; no user data.
- The gate set still applies — **as redefined by Phase 0**: `make test`, `ilp32`,
  `asan-self`, `corelib` (+ `corelib-examples`, `site`, `raytrace`, `mandelbrot`), `conc`,
  `ffi`, the three `fuzz` lanes, `tools-check`, `bench-guard`, `recursion`, `spec-check`,
  `check-links`. Thirteen steps, none of which builds tychoc0; `fixpoint` and
  `frontparity` no longer exist. These are corelib changes and the corelib is gated. Run
  them; do not add to them.
- **ENVIRONMENT:** run every gate as `env -u LD_PRELOAD make …` (a foreign preload in the
  dev shell breaks ASan link order; not a code bug).

## Phases

- [x] **Phase 0 — BLOCKING: freeze tychoc0 and cut it out of the gates (user direction, 2026-07-26)**
  - **User direction, verbatim: *"I told you, tychoc0 is out. Stop running fix points, fuzzer
    etc against it."*** Chosen retirement: **FREEZE** — `compiler/tychoc0.ty` stays in the tree
    as the artifact that proved the thesis, but no gate runs it and no future change mirrors
    into it. It bit-rots by design.
  - **Why this blocks everything else.** tychoc0 is load-bearing in **13 of the 19 CI steps**
    (`scripts/ci.sh`): `test` (reject/abort/diag lanes), `frontparity`, `fixpoint`, `rtparity`,
    `corelib`, `conc`, `ffi`, all four `fuzz` lanes, `typeparity`, `parforparity`, `eqparity`,
    `unaryparity`, `recursion`, `spec-check`. These do not merely *use* tychoc0 — they assert
    the two compilers AGREE, so they go red the moment tychoc changes alone. Phase 6's
    tychoc-only diagnostic already trips this: tychoc now rejects `fn handle(...)` while
    tychoc0 accepts it. Nothing can be verified until the gate set is redefined.
  - **What must NOT be lost.** Strip only the tychoc0 half. The reject lane must still assert
    *tychoc rejects with a non-empty diagnostic*; the golden lanes must still compare tychoc's
    output against recorded goldens. Removing a whole lane because it happens to mention
    tychoc0 would silently drop real coverage — check each one and say what remains.
  - Scope:
    1. `Makefile` + `scripts/ci.sh`: remove `fixpoint`, `frontparity`, `typeparity`,
       `parforparity`, `eqparity`, `unaryparity`, and the differential `fuzz` lanes. Decide
       `rtparity` and `bootstrap` deliberately — read what they actually compare first.
    2. `tests/run.sh`, `corelib/run.sh`, `tests/conc/run.sh`, and the `ffi`/`recursion` lanes:
       remove the tychoc0 build and its comparisons, keep the tychoc assertions.
    3. Mark `compiler/tychoc0.ty` frozen in a header comment — what it proved, when it was
       frozen, and that it is unmaintained. Same in `README.md` and `ROADMAP.md`.
    4. **The spec's conformance model changes and must be edited, not left false.**
       `docs/spec/appendix-f-impl-defined.md` F.3 lists among the invariants "the accept/reject
       decision for every program (the two-implementation conformance oracle, §1.3)", and
       `docs/spec/00-conventions.md` §1.3 defines conformance that way. With one implementation
       that definition no longer describes reality. Rewrite both so conformance is defined
       against the SPEC, with tychoc named as the reference implementation. Sweep for other
       places asserting two implementations (`docs/architecture.md`, `docs/bootstrap.md`,
       `appendix-e-conformance.md`).
  - Non-scope: do NOT delete tychoc0, do NOT fix anything in it, do NOT chase the parity gaps
    the removed lanes were covering. It is frozen, not maintained.
  - Done when: `make ci` is green with no tychoc0 step; a tychoc-only change (Phase 6's, already
    in the tree) verifies cleanly; the surviving gates' coverage is stated; the spec no longer
    claims a conformance model the project does not have.

  **DONE 2026-07-26.** CI went from **19 steps to 13**. No step builds or runs a tychoc0
  binary. Phases 5 and 6 were stranded in the tree by the redirection; both are verified
  and committed here (this phase is what made them verifiable — Phase 6's tychoc-only
  diagnostic would have reddened `frontparity` and `fixpoint` on the exact change the
  user asked for).

  ### Per-lane coverage: what it checked before → what it checks now

  Nothing below lost a tychoc assertion. Every removal is a **second-implementation
  comparison**, never a property of tychoc.

  | Lane | Checked BEFORE | Checks NOW | Verdict |
  |---|---|---|---|
  | `test` — positive | tychoc: native `-O2` == ASan/UBSan == recorded golden | unchanged | kept whole |
  | `test` — `reject/` | tychoc rejects w/ nonzero exit **and non-empty diagnostic**; tychoc0 must also reject (fail-open guard) | tychoc rejects w/ nonzero exit **and non-empty diagnostic** | tychoc half kept verbatim |
  | `test` — `reject/pkg/` | same, package-scoped | same, package-scoped | tychoc half kept verbatim |
  | `test` — `abort/` | tychoc builds, dies nonzero w/ a `tycho:` message; tychoc0 builds and dies with **byte-identical stderr + same exit status** | tychoc builds, dies nonzero w/ a `tycho:` message | tychoc half kept verbatim |
  | `test` — `diag/` | tychoc's exact stderr == `tests/diag/*.err` golden | unchanged | kept whole |
  | `test` — `diag0/` | tychoc0's exact stderr == `tests/diag/*.h0err` golden | **removed** | 100% tychoc0; the 15 lanes are the entire 540→525 drop |
  | `test` — `warn/` | tychoc accepts, warns, stderr == golden | unchanged | kept whole |
  | `ilp32` | same suite rebuilt `gcc -m32`, 64-bit goldens | unchanged | kept whole |
  | `asan-self` | src/tychoc.c under ASan+UBSan compiles the whole corpus **incl. `compiler/tychoc0.ty` as input** | unchanged, tychoc0.ty still in the corpus | kept whole — see the `asan-self` note below |
  | `corelib` | tychoc **and** `tychoc --bundle \| tychoc0` **and** standalone tychoc0, all three == golden | tychoc == golden | 2 of 3 legs were tychoc0; golden comparison + deps-skip untouched |
  | `corelib-examples` | same three ways == golden | tychoc == golden | as above |
  | `site` | 3 compilers + ASan == golden build report | tychoc + ASan == golden | ASan leg re-pointed at tychoc's emitted C |
  | `raytrace` | tychoc == tychoc0 == ASan == golden, valid QOI magic | tychoc == ASan == golden, valid QOI magic | as above |
  | `mandelbrot` | tychoc == tychoc0 == TSan == ASan == golden | tychoc == TSan == ASan == golden | as above |
  | `conc` | per fixture: tychoc native+ASan+TSan == golden, **plus** tychoc0 == same golden; aborts die identically in both; rejects refused by tychoc | tychoc native+ASan+TSan == golden; aborts fire w/ their `.err` message; rejects refused | 37/37 still pass — the count did not move |
  | `ffi` | every check run twice (tychoc + tychoc0) and the outputs compared; handle bans, injection guard, sized ints, first-class sized types asserted in both | every check asserted for tychoc; ASan leg re-pointed at tychoc's emitted C | each individual assertion survives; only the pairwise `cmp` is gone |
  | `recursion` | 7 pathological inputs rejected cleanly + 4 valid accepted, **in both** compilers | same 11, for tychoc | tychoc half kept verbatim |
  | `spec-check` | grammar==chapters, Appendix E fixtures exist, 7 runnable examples run on **both** compilers | grammar==chapters, fixtures exist, 7 examples run on tychoc | still "7 runnable example(s), all pass" |
  | `tools-check` | formatter/LSP checks (tychoc-only) + 3 warning lints asserted in **both** compilers | same, tychoc only | lints still asserted; only the tychoc0 mirror is gone |
  | `check-links` | dead links + `path:N` citations resolve | unchanged | kept whole |
  | `bench-guard` | tree-alloc wall: tycho beats C | unchanged | kept whole |
  | `fuzz` | random valid program: tychoc native vs **tychoc0** native vs **tychoc0** ASan vs tychoc ASan, all byte-identical | random valid program: **tychoc native `-O2` vs tychoc ASan/UBSan `-O1`** must agree byte-for-byte, neither may fault | **tychoc half KEPT** — the native-vs-sanitizer differential of thesis §3 is a single-compiler oracle and it survives; what was lost is a second implementation as an output oracle |
  | `fuzz-reject` | malformed input: **each** compiler must not crash, and anything either accepts must emit valid C; divergence merely *reported* | malformed input: **tychoc** must not crash, and anything it accepts must emit valid C | **tychoc half KEPT verbatim.** Invariants (1) NO CRASH and (2) NO FAIL-OPEN are properties of one compiler. Only the advisory divergence report — which never failed a run — is gone |
  | `fuzz-leak` | valid random programs under ASan+LSan, **both** compilers, no leak at exit | same, tychoc's emitted C | **tychoc half KEPT.** The leak class lives in the emitted runtime, which is tychoc's |
  | `fuzz-pkg` | random 2-package program built **three ways** (tychoc, tychoc0 `--bundle`, tychoc0 standalone), all byte-identical | **removed** | read it: it has **no ASan leg and no golden** — its only oracle was tychoc0. Cross-package codegen stays gated by `tests/pkg/*` goldens and `tests/reject/pkg/*` in `make test` |
  | `fixpoint` | `B==C` byte-identical self-emission + `B` matches tychoc | **removed** | 100% tychoc0 |
  | `frontparity` | tychoc0's frontend must accept everything tychoc accepts | **removed** | 100% tychoc0 |
  | `rtparity` | **decided deliberately.** Read `tests/rtparity/run.py` + `Makefile`: it compiles one probe with both compilers and diffs the *user-visible surface of the emitted C* — env knobs, `tycho:` trap texts, arena-stats rows — because the project shipped **two hand-maintained runtimes**, `runtime/tycho_rt.c` and the one tychoc0 emits as C string literals | **removed** | there is now **one** runtime. The lane's entire subject is the second one. 100% tychoc0 |
  | `bootstrap` | **decided deliberately.** Read `compiler/run.sh`: for each `compiler/tests/*.ty` it builds tychoc0, runs the fixture through it, and asserts `cc(tychoc0(P))` prints what tychoc's binary prints | **removed** | it *is* the tychoc0 validation harness — there is no tychoc-only half. Residual: `compiler/tests/*.ty` is no longer compiled by any gate; those fixtures only ever existed to exercise tychoc0's subset |
  | `typeparity` / `parforparity` / `eqparity` / `unaryparity` | tychoc and tychoc0 agree on accept/reject over an exhaustive matrix | **removed** | each is *defined* as a two-compiler agreement; there is no single-compiler residue |

  **`asan-self` note (the one deliberate exception).** `scripts/asan_self.sh:137` still
  lists `compiler/tychoc0.ty` in its glob. That is **not** running tychoc0: it feeds the
  file to the ASan-built *tychoc* as `--emit-c` input, and no binary is linked. It is the
  largest single Tycho source in the tree (~16k lines) and therefore the corpus's hardest
  stress on tychoc's own memory safety. Kept on purpose; `asan-self: compiled: 540` still
  counts it.

  ### Spec + doc edits (conformance redefined, divergence stated)

  The old model was a **two-implementation oracle**. With one maintained compiler that is
  false, so it was rewritten rather than left standing:

  - `docs/spec/00-conventions.md` — §1.2 "The two-implementation contract" → **"The
    reference implementation"**: `tychoc` named as reference, the spec restated as
    normative over it, plus a subsection saying plainly that `tychoc0` is a **frozen
    snapshot, not a second implementation**, that it and tychoc now accept and reject
    different programs, and naming the live divergence. §1.3 gains a paragraph defining
    conformance against **this specification**, checked via the Appendix E fixture corpus.
  - `docs/spec/appendix-f-impl-defined.md` — F.3's invariant "the accept/reject decision
    for every program (**the two-implementation conformance oracle**, §1.3)" now reads
    "fixed by this specification and checked against the fixture corpus of Appendix E",
    with the old wording kept as a dated historical note. F.1 and the `int`-width note
    de-pluralized off "both reference compilers".
  - `docs/spec/appendix-e-conformance.md` — E.1 redefined against the spec + fixtures;
    the removed parity/fuzz-differential corpora replaced by what actually runs; a
    historical note explains that surviving `*parity` **lane** citations in the E.2 matrix
    name gates that no longer run, and that the clause each backs is normative regardless.
  - `docs/spec/appendix-g-glossary.md` — "Fixpoint" reworded from "anchors the
    two-implementation conformance oracle" to a dated past-tense entry; its dead anchor
    `#12-the-two-implementation-contract` repointed.
  - `docs/spec/09-expressions.md` §13.4 note — "both reference compilers" / "the two
    compilers" → the reference compiler and the now-frozen snapshot.
  - `docs/architecture.md` — the pieces table marks tychoc0 **FROZEN 2026-07-26**; the
    fixpoint paragraph becomes "proved, then frozen" and states the divergence with the
    `fn handle(...)` example; the 16-row gate table rewritten to the 13 surviving gates,
    with a blockquote naming every removed two-implementation gate.
  - `CONTRIBUTING.md` — rule 1 inverted. It said *"Every language feature lands in BOTH
    transpilers, or not at all."* It now says there is one maintained compiler, tychoc0 is
    frozen, do not update it, do not read it to learn the language.
  - `README.md` — the "two compilers … `make fixpoint`" paragraph and the self-hosting
    evidence section rewritten: the result stands as recorded, is not re-run, and tychoc0
    is a dated snapshot.
  - `ROADMAP.md` — "Keeping the two compilers honest" → "Keeping the compiler honest",
    with tychoc0 explicitly out of the loop.
  - `docs/reference/index.md` — "runs under both … the language has two implementations
    that must agree" → runs under tychoc.
  - `compiler/tychoc0.ty` — a 50-line **FROZEN** banner at the top of the file (the place
    someone lands): what it proved, what changed, that it is **already diverging** (naming
    the `fn handle(...)` case), that it is not authoritative about Tycho, and that it is
    finished rather than broken. Plus a mechanical note that its own internal `:N`
    self-citations are now off by −50.
  - `compiler/README.md` — **new**, same message at the directory level, plus a table of
    what each stranded harness in `compiler/` used to be.
  - `docs/bootstrap.md` **does not exist** (verified: `find . -name 'bootstrap*'` returns
    nothing). The dead reference in tychoc0's header was dropped; the sweep target named
    in this phase's scope was stale. Logged in `FRICTION.md`.
  - 21 stale `path:N` citations repaired — the line shifts from Phase 6's `src/tychoc.c`
    edit (+10), this phase's tychoc0 banner (+50), and the shrunken `tests/run.sh` /
    `scripts/ci.sh`. Four archived citations pointed at code this phase deleted and were
    de-numbered rather than re-pointed.

  ### Gate evidence (each run individually, foreground, `env -u LD_PRELOAD`)

  ```
  make test         passed: 525   failed: 0            all green
  make ilp32        passed: 525   failed: 0            all green
  make asan-self    asan-self: compiled: 540   failed: 0
  make corelib      corelib: all green (tychoc matches goldens)
  make corelib-examples   corelib examples: all green
  make site         site: green (io+path+json+csv+strings+sort+datetime+sha256 compose; tychoc+ASan, matches golden)
  make raytrace     raytrace: green (float-heavy Vec3 value semantics; tychoc == ASan; valid QOI)
  make mandelbrot   mandelbrot: green (float in a parallel-for reduction; tychoc == TSan == ASan; deterministic)
  make conc         conc: passed 37   failed 0
  make ffi          ffi: green (tychoc: ASan-clean, matches golden — scalars+string, sized ints, ptr handles, null/is_null, -L + --shim, package-scoped extern)
  make recursion    recursion-cap: all green (fail closed on deep input, no stack overflow)
  make tools-check  tools-check: ok
  make spec-check   spec-examples: 7 runnable example(s), all pass
  make check-links  link check: ok (124 markdown files, no dead relative links)
                    citation check: ok (22 anchored contain the token they name, 1322 bare in bounds)
  fuzz/run.py 20        DONE: ok=20 skip=0 timeout=0 FAIL=0
  fuzz/run_reject.py 20 DONE: accepted=6 rejected=14 FAIL=0
  fuzz/run_leak.py 10   DONE: ok=10 skip=0 FAIL=0
  ```

  **On the test count.** The brief expected 540. It is **525**, and the whole difference
  is the 15 removed `diag0` (`tests/diag/*.h0err`) lanes — one per tychoc0 diagnostic
  golden, `ls tests/diag/*.h0err | wc -l` = 15. 540 − 15 = 525. No tychoc fixture was
  dropped; `asan-self` independently still counts **540** compiles because its corpus is
  file-based, not lane-based.

- [x] **Phase 1 — decide the concurrency shape (probe only, no library change)**
  - Task handles cannot be stored, so thread-per-connection-with-tracking is out. Probe
    what IS expressible for N long-lived workers, and pick one:
    (a) N `spawn`s into N named locals, each running its own `accept` loop on the shared
    listening fd (fd is an `int`, copies freely);
    (b) `parallel for i in range(N):` with an accept loop per iteration — but that is a
    *data-parallel reduction* construct chunked across `tycho_ncpu()`, so verify it
    actually yields N independent long-lived loops rather than chunked batches;
    (c) accept on the main task, `spawn` per connection fire-and-forget — needs an answer
    to "who waits, and does the program exit while tasks are live?"
  - Done when: one shape is chosen with a measurement behind it (N concurrent clients
    served in ~1 unit of time, not N), and the losing options have a recorded reason.
    This is the server's architecture; get it right before writing the server.

  **DONE 2026-07-25 — chose (a'), recursive fan-out of shape (a).**

  Method: four probe programs, each its own directory (`package main` compiles the
  whole directory). Handler does a fixed 500ms spin on `time.elapsed_ms` — a wall-clock
  wait `-O2` cannot fold away. Client is N parallel `curl`s, wall clock measured around
  the batch (`scratchpad/bench.sh`). Build: `env -u LD_PRELOAD ./tychoc <dir>/main.ty -o
  <dir>/prog`. Host: 16 CPUs. One unit = ~500ms.

  | Shape | Workers | Clients | Wall | Verdict |
  |---|---|---|---|---|
  | (a) N spawns into N named locals | 4 | 1 | 513ms | baseline = 1 unit |
  | (a) same | 4 | **4** | **511ms** | **1 unit — real concurrency** |
  | (a) same — control | 4 | 8 | 1011ms | 2 units, as it must be |
  | (a) 8 named locals | 8 | 8 | 510ms | 1 unit |
  | (a') recursive fan-out, N=8 runtime | 8 | 8 | **509ms** | **1 unit — chosen** |
  | (a') same — control | 8 | 16 | 1012ms | 2 units, so exactly 8 loops are live |
  | (b) `parallel for i in range(4)` | 4 | 4 | 510ms | 1 unit *on a 16-CPU host only* |
  | (b) same, `TYCHO_THREADS=2` | **2** | 4 | 1012ms | **2 units — chunking collapsed it** |
  | (b) same, `TYCHO_THREADS=1` | **1** | 2 | 1010ms | 2 units |
  | (c) accept on main, spawn per conn | — | 1 | 510ms | 1 unit |
  | (c) same | — | **4** | **2011ms** | **4 units — fully serial** |

  **(c) is disqualified, and the source says why.** `spawn` must bind a handle: a bare
  `spawn work(1)` statement is rejected — `error: a statement must be a declaration,
  assignment, or call -- a bare expression has no effect`. Once bound, the compiler
  emits an implicit join at the handle's scope exit — `src/tychoc.c:9148`
  (`taskvar_push(sfmt("tycho_task_finish(h_%s)", s->name))`) and `compiler/tychoc0.ty:9150,:9166`
  (renumbered +50 by Phase 0's freeze banner)
  — which runs `pthread_join` for any un-waited task (`runtime/tycho_rt.c:602-606`). In an
  accept loop the handle is a loop-body local, so every iteration joins before the next
  `accept`: 4 clients cost 4 units. Answering the phase's question directly: an un-waited
  task does **not** leak, abort, or get reaped asynchronously — the parent blocks for it.
  Measured: `main` reached its last statement at 0ms, the process exited at 505ms.
  Fire-and-forget is not expressible in Tycho.

  **(b) is disqualified as unsound, not as slow.** `parallel for` fans out
  `tycho_ncpu()` chunk threads (`runtime/tycho_rt.c:843-852`), so `range(N)` yields
  `min(N, ncpu)` *live* iterations. Iterations chunked behind a non-returning one never
  start at all — an accept loop never returns. It read as the prettiest shape and passed
  at N=4 purely because the host has 16 CPUs; `TYCHO_THREADS=2` silently cut the server
  to 2 workers with no diagnostic. A worker count that depends on the machine, not the
  program, is not an architecture.

  **Chosen: (a') recursive fan-out.** Source:

  ```tycho
  fn accept_loop(srv: int, id: int) -> int:
      served := 0
      for served < 1000:
          conn := net.accept(srv)
          if conn < 0:
              return served
          req := httpd.read_request(conn)
          spin_ms(500)
          httpd.write_response(conn, httpd.response(200, "worker " + str(id) + "\n"))
          net.close_fd(conn)
          served = served + 1
      return served

  fn worker(srv: int, id: int, remaining: int) -> int:
      if remaining > 1:
          peer := spawn worker(srv, id + 1, remaining - 1)
          n := accept_loop(srv, id)
          return n + wait(peer)
      return accept_loop(srv, id)

  fn main():
      srv := net.listen("127.0.0.1", 8105)
      print(str(worker(srv, 1, 8)))
  ```

  **Readability, honestly.** Plain (a) at N=8 is literally eight near-identical
  `w1 := spawn worker(srv, 1)` lines followed by eight `wait(w1)` lines — sixteen lines
  of copy-paste where every other language writes one loop, and the worker count is
  frozen at *write* time, not run time. That is a real ergonomic finding about Tycho and
  it belongs on the record even though (a) won on measurement.

  (a') gets the count back to a runtime value with one function and no repetition, and
  it is the shape the server will use. But it is a workaround wearing a nice coat: the
  recursion exists solely because a task handle is affine and cannot be stored, so the
  only place to put N handles is N stack frames. `peer := spawn worker(...)` is doing
  the job of `for _ in range(n): spawn worker(...)`, and a reader has to already know
  the affinity rule to see why. It also builds an N-deep join chain, so shutdown unwinds
  through every frame — fine at N=8, and something to remember if N ever gets large.
  The honest summary: Tycho can express a concurrent server cleanly enough to write one,
  but "start N workers" — the most ordinary thing a server does — has no direct spelling.

- [x] **Phase 2 — BLOCKER: `httpd` carries `bytes` bodies**
  - `Request.body` and `Response.body` become `bytes`; `render()` stops building the
    response as one string. Headers stay `string` (they are ASCII by spec).
  - Update every consumer in the same commit: `examples/webserver`, `examples/site`,
    `examples/weblog`, `corelib/test/httpd`, plus any golden they own.
  - Record honestly how `bytes` felt to work with — concatenation, slicing, converting
    to/from `string`. That answer decides whether Tycho can do IO comfortably at all.
  - Done when: a PNG round-trips through `read_request`/`write_response` byte-identically,
    fixture-locked, and the full gate set is green.

  **DONE 2026-07-26.** `Request.body` and `Response.body` are `bytes`
  (`corelib/httpd/httpd.ty:44,:51`). `render()` no longer builds the response as one
  string: it split into `render_head(r) -> string` (the ASCII status line + headers, which
  is genuinely text) and `render(r) -> bytes`; the socket path `write_response` sends head
  and body as **two `net.write` calls**, so the body buffer is never copied into an
  intermediate string. Added `text_response(status, s)` as the string convenience,
  `with_body`, and clamped `read_request`'s post-Content-Length reads so a keep-alive
  client's next request is not swallowed.

  Consumers, all in this commit: `corelib/test/httpd/main.ty` (+ `corelib/test/httpd.out`),
  `examples/corelib/httpd/main.ty` (+ `examples/corelib/httpd.out`),
  `examples/webserver/main.ty`. The plan named `examples/site` and `examples/weblog` too —
  **verified they do not import `core:httpd`** (`find . -name '*.ty' | xargs grep -ln
  'core:httpd'` returns exactly four files: the package, the corelib test, the corelib
  example, and `examples/webserver`). `examples/webserver/expected.out` is **byte-identical
  before and after**, which is the evidence that the refactor changed types, not behaviour.

  ### Proof: a real PNG round-trips byte-identically

  A 120,303-byte real PNG (200x200 RGB, **677 interior `0x00` bytes**), a 70-byte PNG, and
  300,000 bytes of `/dev/urandom`, served by a 4-worker static server built only from the
  new `httpd` surface and fetched with `curl -o`:

  ```
  cmp photo.png  : IDENTICAL (120303 bytes)
  cmp small.png  : IDENTICAL (70 bytes)
  cmp big.bin    : IDENTICAL (300000 bytes)
  got_photo.png: PNG image data, 200 x 200, 8-bit/color RGB, non-interlaced
  ```

  And against the real example site (`examples/webserver --serve`, `PORT=8212`):

  ```
  favicon.ico bytes IDENTICAL to favicon.png on disk
  style.css bytes IDENTICAL
  ```

  Fixture-locked in `corelib/test/httpd.out` — an interior NUL and a `0xFF` survive both
  `render()` and a real socket:

  ```
  bin_len   = 13
  bin_nul   = 0 255
  sock_bin_len   = 10
  sock_bin_bytes = 137 0 255 1
  ```

  ### How `bytes` felt to work with — the honest answer: it is not a usable value type

  Measured, not assumed. Three probes, each a direct `./tychoc` compile:

  ```
  a + b     ->  error: arithmetic requires two ints or two floats (got bytes, bytes)
                -- convert one side, e.g. to_float(x) ... or to_int(x) in ints
  b[2]      ->  error: can only index an array, a string, or a map (as a place)
  b[1:3]    ->  error: can only slice an array, soa, or string
  ```

  **`bytes` supports exactly three things: `len()`, `to_str()`, and crossing the FFI.** No
  concatenation, no indexing, no slicing, no literal, no empty value (`to_bytes("")` is the
  only spelling). It is a transport wrapper, not a buffer type.

  So "stop building the response as one string" is only half-achievable *in principle*:
  there is no in-language way to join two `bytes`, so any single-buffer assembly **must**
  detour through `string`. The design answers this by not assembling at all on the hot
  path — two `net.write` calls — which is better than the string concat it replaced, but
  the reason it is better is a workaround for a missing operator, not a design win.

  The second finding is more surprising and it partly undercuts the phase's premise.
  `httpd`'s old header comment said an interior `0x00` truncates a `string` body. **That is
  false**, and it was false when written — `string` is length-counted and byte-safe
  end-to-end. Measured:

  ```
  str concat len=11 b[2]=0 b[9]=0     # "Hi\0\xff" + "world" + chr(0) + "!"
  roundtrip len=11                    # to_bytes(that) preserves all 11
  slice-of-str len=2
  ```

  So the string→bytes change did **not** buy binary safety the old model lacked; it bought
  *type-level honesty* (a body is a buffer, not text), the right shape for `core:net`'s
  already-`bytes` API, and the removal of a hand-rolled binary writer from
  `examples/webserver`. Worth doing. But the headline ergonomics finding is that Tycho's
  binary type is the weaker of its two byte sequences: `string` is the one you can actually
  compute with, and `bytes` is what you convert to at the door. Every `bytes` manipulation
  in this phase is a `to_str` … `to_bytes` sandwich. That is filed in `FRICTION.md`.

  **Gates not run — user constraint limits `make test`/`make ci` to once per day, today's
  allowance spent.** Verified instead by running what was built: three direct `./tychoc`
  compiles of every consumer, the `corelib/test/httpd` and `corelib/test/net` and
  `corelib/test/http` binaries diffed against their goldens (all match), and live `curl`
  against two servers.

- [x] **Phase 3 — BLOCKER: content type by file extension**
  - A MIME table (`.html .css .js .json .svg .png .jpg .gif .woff2 .ico .txt .wasm` at
    minimum) and a `content_type(path) -> string`. Put it in `httpd` unless there is a
    reason to prefer `path`.
  - Unknown extension → `application/octet-stream`, never `text/plain` (guessing wrong is
    how browsers execute things they should download).
  - Done when: serving a directory gives each file the right type, verified with `curl -I`.

  **DONE 2026-07-26.** `httpd.content_type(path) -> string` over a 20-extension table,
  with a case-insensitive `has_ext` helper (written inline rather than importing
  `core:strings` — a corelib package should not take a dependency for one predicate).
  Unknown extension → `application/octet-stream`, never `text/plain`.

  `curl -I` against a served directory (4-worker static server, port 8211):

  ```
  index.html     -> text/html; charset=utf-8
  style.css      -> text/css; charset=utf-8
  app.js         -> text/javascript; charset=utf-8
  data.json      -> application/json
  i.svg          -> image/svg+xml
  photo.png      -> image/png
  small.png      -> image/png
  font.woff2     -> font/woff2
  mod.wasm       -> application/wasm
  notes.txt      -> text/plain; charset=utf-8
  big.bin        -> application/octet-stream
  README         -> application/octet-stream
  ```

  The last two are the ones that matter: an unknown extension and **no** extension both
  fall to `application/octet-stream`. `/img/LOGO.PNG` → `image/png` proves the match is
  case-insensitive; `/archive.tar.gz` → `application/octet-stream` proves an unlisted
  compound extension does not get a lucky `text/*`. Both are golden-locked in
  `corelib/test/httpd.out`.

  `examples/webserver` deleted its hand-rolled 2-entry MIME table in favour of this one,
  and `examples/webserver/expected.out` did not move — the two agreed on `.css` and `.png`.

  **Gates not run — user constraint limits `make test`/`make ci` to once per day, today's
  allowance spent.** Verified by `curl -I` against a live server, plus the golden-locked
  table in `corelib/test/httpd`.

- [x] **Phase 4 — BLOCKER: persistent connections**
  - HTTP/1.1 defaults to keep-alive; `httpd` closes after every request, so a page with 30
    assets pays 30 handshakes. Implement: read `Connection:`, honour `close`, keep the
    socket open otherwise, loop reading requests off one connection, and time out an idle
    peer so a slow-loris cannot pin a worker forever.
  - The idle timeout needs Phase 5, or a non-blocking read with a deadline — establish
    which before starting.
  - Done when: `curl -v` shows connection reuse across two requests, and an idle
    connection is dropped rather than holding a worker.

  **DONE 2026-07-26.**

  ### The idle timeout: answering "which mechanism" before implementing

  The phase said to establish this first. Read both options in the source:

  - **`time.sleep_ms` (Phase 5) cannot do it.** Sleeping cannot interrupt a `recv` that is
    already blocked; by the time a worker could check a deadline it is already parked in
    the kernel. Wrong tool, and using it would have been faking the fix.
  - **`core:net` could not express a read deadline either.** `corelib/net/net_shim.c`
    called `setsockopt` in exactly two places, both `SO_REUSEADDR` (still there, now
    `corelib/net/net_shim.c:96` and `:201`); there was no `SO_RCVTIMEO`, no `poll`, no
    `select`, and `netx_read` (`corelib/net/net_shim.c:151`) is one bare
    blocking `recv`. So the honest statement is: **"do not let an idle peer pin this
    worker" was not expressible in Tycho corelib**, and the phase could not be completed
    without adding the primitive.

  Added, minimally: `netx_set_read_timeout(fd, ms)` over `SO_RCVTIMEO` in
  `corelib/net/net_shim.c` (with the Winsock `DWORD`-milliseconds branch), surfaced as
  `net.set_read_timeout_ms(fd, ms) -> bool`. It fails closed — a failed `setsockopt`
  returns `false` and leaves the socket blocking rather than half-armed. A timeout makes
  `read` return **empty**, identical to EOF, deliberately: a server's answer to both is to
  drop the connection, so `read_request` needs no new failure mode and `method == ""`
  remains the single close test. (The cost of that choice — a *client* cannot distinguish
  timeout from EOF — is in `FRICTION.md`.)

  `httpd` side: `connection_close(req)` (HTTP/1.1 defaults open, HTTP/1.0 defaults closed,
  `Connection: close` wins, a malformed request always closes — fail closed) and
  `with_connection(r, alive)`. Token matching is comma-split and case-insensitive, so
  `Connection: keep-alive, Upgrade` parses.

  ### Proof

  Connection reuse — three assets of the **real example site** on one `curl` command line:

  ```
  $ PORT=8212 examples/webserver --serve &
  $ curl -sv http://127.0.0.1:8212/ .../static/style.css .../favicon.ico
  *   Trying 127.0.0.1:8212...
  * Established connection to 127.0.0.1 (127.0.0.1 port 8212) from 127.0.0.1 port 35926
  < HTTP/1.1 200 OK
  < Content-Type: text/html; charset=utf-8
  < Connection: keep-alive
  * Connection #0 to host 127.0.0.1:8212 left intact
  * Reusing existing http: connection with host 127.0.0.1        <-- no second connect
  < Content-Type: text/css; charset=utf-8
  * Connection #0 to host 127.0.0.1:8212 left intact
  * Reusing existing http: connection with host 127.0.0.1        <-- nor a third
  < Content-Type: image/png
  ```

  One TCP handshake, three assets, correct types, favicon byte-identical. That is the
  30-assets-30-handshakes problem gone.

  Idle drop and close-honouring (static server, `IDLE_MS = 1000`):

  ```
  PHASE 4b  silent socket: server closed after 1028 ms, recv returned b''
  PHASE 4c  /notes.txt -> HTTP/1.1 200 OK | ['Connection: keep-alive']
            /data.json -> HTTP/1.1 200 OK | ['Connection: keep-alive']
            then idle: server closed after 854 ms, recv returned b''
  PHASE 4d  Connection: close honoured, closed after 0 ms (not the timeout)
  PHASE 4e  8 silent sockets vs a 4-worker server:
            a real request completed in 2044 ms; got 26 bytes
  ```

  **4e read honestly.** With 8 slow-loris sockets against 4 workers the server does *not*
  serve instantly — the real request waited 2044 ms, i.e. two timeout rounds, because each
  worker must burn its 1 s deadline on two silent peers before reaching a real one. The
  claim proven is the one that matters: the workers are **recovered**, not pinned. Before
  this change that request never completes. Bounded degradation under attack, not immunity;
  a production answer needs a connection cap or an event loop, and neither belongs in this
  phase.

  **Gates not run — user constraint limits `make test`/`make ci` to once per day, today's
  allowance spent.** Verified by `curl -v` and a scripted socket client against two live
  servers, plus `timeout_armed`/`timeout_closes` golden-locked in `corelib/test/httpd.out`.

- [x] **Phase 5 — FRICTION: `sleep` in `core:time`**
  - `runtime/tycho_rt.c:822` already calls `nanosleep` for `select`; expose `sleep_ms(ms)`
    and `sleep_ns(ns)`. Small, additive, and needed for backoff, retry and shutdown.
  - Done when: it sleeps for the requested duration (measured, not assumed) and is spec'd
    in `16-builtins.md`/the `core:time` docs.

  **DONE 2026-07-26.** Written by a stopped agent before the Phase 0 redirection, left
  uncommitted in the tree, and verified + committed here. `corelib/time/time.ty` gains
  `sleep_ms`/`sleep_ns` over a new `corelib/time/time_shim.c` (pure libc `nanosleep`, no
  `deps`, so nothing gains a pkg-config dependency). Spec'd in `docs/spec/18-library.md`
  and `docs/guides/corelib.md`.

  The duration is **measured, not assumed** — `corelib/test/time/main.ty` sleeps and then
  checks the elapsed clock, and the four new assertions are golden-locked in
  `corelib/test/time.out`:

  ```
  sleep_ms_at_least=true      # slept at least the requested ms
  sleep_ms_sane=true          # and not absurdly longer
  sleep_ns_at_least=true      # ns path too
  nonpositive_immediate=true  # sleep(<=0) returns immediately, does not block
  ```

  Gate: `make corelib` → `ok   time` … `corelib: all green (tychoc matches goldens)`.

- [x] **Phase 6 — FRICTION (cheap): say why `handle` is rejected**
  - `fn handle(...)` gives `expected a procedure name`. `handle` is a real keyword
    (`docs/spec/01-lexical.md`), so the fix is the *message*, not the grammar: name the
    keyword. Check the sibling keywords give the same treatment.
  - ~~Done when: the diagnostic names the reserved word, in both compilers.~~
    **Amended by Phase 0's user direction: tychoc only.** tychoc0 is frozen; mirroring
    into it is exactly what the freeze forbids. Its half of this change was written and is
    deliberately left on the stash (`git stash list`), unapplied.
  - Done when: the diagnostic names the reserved word in tychoc.

  **DONE 2026-07-26.** `src/tychoc.c` (+10 lines). Before, `fn handle(x: int) -> int:`
  gave `expected a procedure name` — true but useless, since it never says *why* the name
  was refused. Now:

  ```
  $ ./tychoc /tmp/h.ty --emit-c -o /tmp/h
  /tmp/h.ty:1: error: 'handle' is a reserved keyword and cannot be used as a procedure name
       1 | fn handle(x: int) -> int:
         |    ^
  ```

  **This is the project's first deliberate tychoc/tychoc0 divergence**, and it is the
  reason Phase 0 had to land first: tychoc now rejects this program, tychoc0 still accepts
  it, and under the old gate set `frontparity` (tychoc0's frontend must accept everything
  tychoc accepts) and `fixpoint` would have gone red on precisely the change the user
  asked for. It is cited by name in the freeze notice in `compiler/tychoc0.ty`,
  `compiler/README.md`, `docs/spec/00-conventions.md` §1.2, `docs/architecture.md` and
  `CONTRIBUTING.md` as the concrete evidence that the two have split.

- [ ] **Phase 7 — THE POINT: write the server**
  - Worker pool per Phase 1. Serves a real content directory: static files with correct
    types, binary-safe, keep-alive, index resolution, 404/405/400, path-traversal refusal,
    request logging.
  - Not a fixture, not a demo: something to point a browser at and actually use. Decide
    with the user where it lives (`examples/` is for demos; this may want its own home).
  - **Keep `FRICTION.md` as you go** — one line per moment the language got in the way.
    That file is the real output of this plan.
  - Done when: it serves a real site to a real browser, survives `curl` abuse (malformed
    requests, huge headers, early disconnect), and `FRICTION.md` is an honest account.

## Out of scope

- TLS (`corelib/tls` exists; HTTP first, and a reverse proxy is the normal answer anyway).
- HTTP/2, ranges, compression, caching headers, virtual hosts — none block a real server.
- The two items left open by the previous plan (`Phase 36`'s foreign-type-parameter hole,
  `Phase 44`'s citation corpus). The generics hole may surface here naturally; if it
  blocks the server it earns a phase, otherwise it stays where it is.
