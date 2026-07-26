# Close out `FRICTION.md`

Follows the `Option`/`Result` plan (archived: `docs/internals/plan-option-result-DONE.md`,
5 phases, head `8aac642`), which closed **2 of `FRICTION.md`'s 12 phase-7 items** and created
6 new ones. That ratio is the reason this plan exists. `FRICTION.md` is a 235-line honest
account of writing `server/` and then converting the corelib; it has never been worked as a
list. This plan works it as a list.

## Goal

**Every open item in `FRICTION.md` is either fixed or explicitly refused with a reason.**
`FRICTION.md`'s own self-score section (`FRICTION.md:79`) is the scoreboard: it currently
reads "2 closed, 10 untouched" for phase 7 plus 6 created items and the earlier-phase list
below them. Done = that section is re-scored against the tree and no item is in the
"untouched, unexplained" state.

Fixed means: the friction is gone at the call site that complained, proven by compiling and
running the thing that used to be awkward. Refused means: one line in `FRICTION.md` saying
why, with a cost estimate — not silence.

## Anti-scope

- **One item, one fix. No redesigns.** `bytes` gets operators, not a new buffer type.
  `core:cli` learns `--root DIR`, it does not become `clap`.
- **No item is fixed by moving it.** Deleting a complaint from `FRICTION.md` without a code
  change or a written refusal is the one failure mode this plan can have.
- **`compiler/tychoc0.ty` is frozen** (2026-07-26, diverging, unmaintained). The live
  compiler is `src/tychoc.c`. Never touch tychoc0 to satisfy a phase.
- Discovered defects outside the phase's item go to `FRICTION.md` as one line, unfixed —
  the same rule the last plan ran under, and it held for 5 phases.
- **This plan must not grow beyond its item list.** The item list is `FRICTION.md`. When the
  file is re-scored, the plan is done.

### GATE CONSTRAINT — user directive, 2026-07-26, still binding

**`make ci` and `make test` run AT MOST ONCE PER DAY, across all agents.** Violating it
means the gates get removed. Verification is running the thing you built: compiling the
corelib and all 13 entry points directly with `./tychoc`, diffing the 9 goldens, and running
`server/` live. The archived plan's phases 1–5 each did exactly this and each caught real
bugs that way (`plan-option-result-DONE.md`, phase 4's `403`-on-`/` regression).

**Compiler phases are the exception worth naming:** a change to `src/tychoc.c` reaches every
program in the tree. A compiler phase may spend the day's single `make ci` if it has not been
spent, and **must record in its evidence whether it did or not**. If it is already spent, the
phase compiles the corelib, all 13 entry points and all goldens by hand and says so.

## Pre-flight

- Worst case: a `src/tychoc.c` change miscompiles something no golden covers. `FRICTION.md`
  already records that `examples/webserver` went uncompilable for a whole phase because no
  gate builds it — so the by-hand entry-point sweep is not optional, it is the mitigation.
- Reversibility: every phase is one commit on `main`; `git revert` is the escape hatch. No
  destructive operations, no data migration, nothing outside the repo.
- Verified: the item list is real and quoted — `FRICTION.md:118-131` (phase 7, 10 open),
  `:133-161` (the 6 created by the last plan), `:168-169` (phase 4's two), `:219-235`
  (earlier phases). The two unfixable-in-Tycho C-level defects at `:171-179` are already
  fixed in the shims.
- Assuming: the compiler items are individually small (a lexer escape, a resolution bug, a
  divergence flag). **Not verified** — none has been costed by reading `src/tychoc.c`.
  Risk if wrong: a phase that looks like 20 lines is 300, and the honest response is to
  refuse it in `FRICTION.md` with the real number rather than half-build it.

## Phases

Ordered by leverage: the items that tax every other item come first. A phase that finds its
item is not worth fixing **refuses it in `FRICTION.md` with the cost** and still ticks — that
is a completed phase under this plan's Goal, and it is not a failure.

- [ ] **Phase 1 — the resolution bug that taxes every generic call site**
  - Item: `FRICTION.md:148` — a qualified name anywhere in a *generic* call's argument list
    does not resolve. `result.unwrap_or(net.port_of(fd), -1)` → `error: package 'net' has no
    symbol 'net__port_of'`; `result.err_or(r, net.Failed)` → `error: unknown variable 'net'`.
    The identical spellings work in `==` and as arguments to concretely-typed parameters, so
    generic instantiation is losing the package qualifier.
  - This is first because it is a **bug, not a gap**, and it is the one that stops
    `core:result` — the library the last plan added — from being usable as a one-liner.
  - Scope: `src/tychoc.c` generic instantiation / name resolution. Plus a regression test.
  - Done when: all three failing spellings from `FRICTION.md:148` compile and run, and the
    corelib call sites that bound a local purely to work around it are un-worked-around
    (search for them; the last plan created several).
  - Verify: a scratch program with all three spellings compiles and prints correct values;
    corelib + 13 entry points compile; 9 goldens match; `server/` live matrix unchanged.

- [ ] **Phase 2 — `\r`, multi-line strings, and `const` folding**
  - Items: `FRICTION.md:123` — no `\r` escape in string literals, so the most common byte
    pair in HTTP is a function call (`httpd.crlf()`), and `const TERM = httpd.crlf() +
    httpd.crlf()` is rejected (`error: const value must be a literal`), making the header
    terminator reallocate two strings per loop iteration. `FRICTION.md:124` — no multi-line
    string literal and no line continuation, so a 10-line HTML error page is 10 consecutive
    `s += "..."` statements.
  - Scope: the lexer in `src/tychoc.c`, `docs/spec/` string-literal section, and the corelib
    sites that exist only because of the gap (`httpd.crlf`, `server/`'s error page builder).
    Decide whether `const` folding of a `+` over two literals is in or out — if out, say why.
  - Done when: `"\r\n"` is a literal, a multi-line string form exists (or is refused with a
    reason), `httpd.crlf()` is either gone or documented as kept deliberately, and the spec
    documents whatever landed.
  - Verify: corelib + 13 entry points compile; 9 goldens match; the HTTP wire bytes are
    unchanged (`server/` live matrix, byte-compared as phases 3–5 of the last plan did).

- [ ] **Phase 3 — nested patterns, and `Result` in a tuple literal**
  - Items: `FRICTION.md:139` — there are **no nested patterns**: `Err(net.Timeout)` is
    rejected with `error: expected ')'`, and worse, `Err(A)` for a nullary variant *parses as
    a binding named `A`*, surfacing only as `error: duplicate Err arm` if a second arm exists.
    That silent misparse is a correctness trap, not an ergonomic one. `FRICTION.md:159` — a
    tuple literal will not infer a `Result` element: `return (Err(A), "partial")` →
    `error: tuple element 1 needs a concrete value`.
  - Both are why the last plan's error enums are payload-free and why
    `httpd.read_request_capped` builds its outcome in a local.
  - Scope: `src/tychoc.c` pattern parsing + tuple element inference; `docs/spec/` patterns
    section; a regression test per shape. **If nested patterns are genuinely large, fix the
    silent-binding misparse anyway** — a wrong parse that only shows up as a duplicate-arm
    error is the higher-severity half, and refusing it needs a very good reason.
  - Done when: `Err(net.Timeout)` matches, or the misparse is a hard error at minimum; a
    `Result` can be a tuple literal element; the workaround locals are removed.
  - Verify: corelib + 13 entry points compile; 9 goldens match; `server/` live matrix
    unchanged; new regression tests pass.

- [ ] **Phase 4 — `exit(code)`, and `die()` as a diverging call**
  - Items: `FRICTION.md:128` — `die()` is the language's only exit and always exits **1**, so
    `--help` cannot answer status 0 through it; the workaround threaded a `help: bool` field
    through `server/`'s config struct. `FRICTION.md:140` — `die()` is typed `void` and is not
    modelled as diverging, so it cannot be the tail of a value-`match` arm
    (`a value if/match branch must produce a value, not void`), which is the one call site in
    `server/` where adopting `Result` cost a line.
  - Scope: an `exit(int)` builtin (or `die`-with-status), plus divergence modelling in
    `src/tychoc.c`'s type checker; `docs/spec/16-builtins.md`; then drop the `help: bool`
    data-flow workaround in `server/main.ty` and the dummy `srv := 0`.
  - Done when: `--help` exits 0 without threading a flag, and `srv := match net.listen(...):
    Ok(fd): fd / Err(e): die(...)` compiles.
  - Verify: `./tycho-httpd --help; echo $?` prints 0; corelib + 13 entry points compile;
    9 goldens match; `server/` live matrix unchanged.

- [ ] **Phase 5 — the corelib's small honest wins**
  - Items, all small and all measured as biting real code:
    - `FRICTION.md:122` — `httpd.reason_phrase` is a closed `if`-chain and `httpd.response()`
      takes no reason, so `431` went on the wire as `HTTP/1.1 431 Status`. **Bit twice**: the
      `408` in phase 3 needed the same positional-struct bypass, which got factored into a
      private `phrased_response()` — a reimplementation of the constructor the corelib should
      have. Estimated ~5 lines.
    - `FRICTION.md:129` — `core:net` exposes `getsockname` but no `getpeername`, so an access
      log cannot record the client address: the single most useful field in a real access log
      is unreachable. Then use it in `server/`'s log line.
    - `FRICTION.md:168` — `io.exists` lists the whole parent directory (O(entries), and blind
      to a `.`/`..`-only leaf) where the new `io.is_dir` is one `stat`. Make `exists`
      `stat`-backed; `resolve()` currently calls both on the same path.
    - `FRICTION.md:227` — `to_bytes("")` is the only spelling for an empty `bytes`.
  - Scope: `corelib/httpd/`, `corelib/net/` (+ shim), `corelib/io/`, and their consumers.
  - Done when: `431` and `408` carry real reason phrases with no positional bypass and
    `phrased_response()` is deleted; the access log records the peer address; `io.exists` is
    one syscall; the `bytes` zero value is decided (landed or refused).
  - Verify: 9 goldens match or their change is justified; `server/` live matrix shows the
    real reason phrases on the `431`/`408` lines and a client address in the log; corelib +
    13 entry points compile.

- [ ] **Phase 6 — `core:cli` and `args()`**
  - Items: `FRICTION.md:126` — `args()` includes `argv[0]` but `cli.parse` requires it
    removed, so every program opens with the same four-line copy loop (`server/main.ty` and
    `examples/weblog/main.ty:129` carry identical copies, the second with a comment
    explaining it). `FRICTION.md:127` — `core:cli` cannot express `--root DIR`: values must be
    `=`-attached by design, so **45 of `server/`'s lines** are a hand-rolled parser.
  - Biggest single line-count win in the whole file, and it is library-only.
  - Scope: `corelib/cli/`, then delete `server/main.ty`'s hand-rolled parser and both copy
    loops. Keep `=`-attached spellings working — this is an addition, not a migration.
  - Done when: `--root DIR` and `--root=DIR` both parse through `core:cli`, `args()` is
    usable without the copy loop, and `server/main.ty`'s 45-line parser is gone.
  - Verify: every existing CLI spelling in the tree still works (`server/` and
    `examples/weblog` both driven with real flags, output captured); 9 goldens match;
    `server/main.ty`'s line count recorded before and after.

- [ ] **Phase 7 — `bytes` gets operators**
  - Items: `FRICTION.md:224` — `bytes` supports **only** `len()`, `to_str()` and crossing the
    FFI: `a + b`, `b[i]` and `b[i:j]` are all rejected, so every non-trivial manipulation
    detours through `to_str`, works in `string`, and `to_bytes` back. `FRICTION.md:130` —
    that detour lands in `log_safe`, the one function where a server must be paranoid about a
    hostile request target: `string` → `[]int` → `to_bytes` → `to_str`. `FRICTION.md:225` —
    and the error message for `bytes + bytes` suggests `to_float`/`to_int`, neither of which
    applies to a buffer.
  - Scope: `src/tychoc.c` — concat, index, slice for `bytes`, plus the diagnostic text.
    `docs/spec/` bytes section. Then simplify `log_safe`.
  - Note the recorded fact that makes this cheap to get right: `string` is already fully
    byte-safe (interior `0x00` survives concat, index, slice and `len` — measured,
    `FRICTION.md:226`), so this is about intent and FFI shape, not new binary machinery.
  - Done when: `a + b`, `b[i]`, `b[i:j]` work on `bytes`; the misleading diagnostic is
    replaced; `log_safe` no longer round-trips through `[]int`.
  - Verify: a scratch program exercising all three operations against known bytes, including
    an interior `0x00`; `server/` still serves the PNG/ICO/TTF byte-exact (`cmp` against
    disk, as the last plan's phase-2 evidence did); 9 goldens match.

- [ ] **Phase 8 — tooling and doc debt, where each item is a trap for the next person**
  - Items: `FRICTION.md:141` — `tychoc` compiles every `.ty` in the entry file's directory,
    so two scratch programs side by side collide with `'main' is already defined` pointing at
    the file you asked it to build; it cost four compile cycles to work out.
    `FRICTION.md:152` — `examples/webserver/main.ty` was left uncompilable by a whole phase
    because **no gate builds it** (`make ci` skips it). `FRICTION.md:142` and `:151` — the FFI
    has no documented shape for returning a classification alongside a `bytes` payload; the
    `status: inout int` trick is now reproduced verbatim in two shims and written down only in
    each other's comments, while `docs/spec/14-ffi.md:20-47` lists neither.
    `FRICTION.md:160` — tuples are the right shape for "value AND classification" and
    **nothing pointed at them**: documented at `docs/spec/03-types.md:193` but demonstrated by
    no corelib function. `FRICTION.md:235` — `docs/bootstrap.md` is cited by two live files
    and does not exist; the citation gate missed it because it only checks Markdown→Markdown.
  - Scope: `src/tychoc.c` entry-file selection (or a clear diagnostic naming the sibling
    file); the gate/runner list so `examples/webserver` cannot rot again — **without** adding
    to `make ci`'s runtime if that would tempt anyone to run it more than once a day;
    `docs/spec/14-ffi.md`; the missing `docs/bootstrap.md` (write it or remove the citations);
    the citation gate's source→doc direction.
  - Done when: a sibling `.ty` no longer breaks a build (or the error names it), a gate or
    runner covers `examples/webserver`, the FFI classification shape is in the spec, and no
    live file cites a document that does not exist.
  - Verify: two scratch programs in one directory, one built, real output captured; the new
    coverage reddened deliberately once (break `examples/webserver`, see it fail, fix it);
    the citation gate run over the tree.

- [ ] **Phase 9 — the two items that need a decision, not a patch**
  - Item A: `FRICTION.md:131` — `parallel for` and `spawn` are the only concurrency shapes
    and neither expresses "hand this connection to whoever is free", so one worker owns one
    connection for its whole life and **N workers is a hard cap of N concurrent
    connections**. There is no way to write an event loop or a work queue over accepted fds
    without a channel of ints and a hand-rolled dispatcher. This is the largest open item in
    the file and the only one that limits what the server can *be*.
  - Item B: `FRICTION.md:149`/`:150` — two error types in one function make `or_return`
    unavailable again (no `map_err`, no conversion between error enums), and converting a
    call a big block consumes costs an **indentation level**: `serve_conn` went 60 → 71 lines
    because `match httpd.parse_request(raw)` wraps the whole body. There is no `if let` and no
    early-return binding form.
  - Scope: **cost both by reading `src/tychoc.c` and `runtime/tycho_rt.c`, then decide.** A
    channel-of-fds work queue may be writable today with what exists (the file says a
    hand-rolled dispatcher is possible) — if so, write it in `server/` and measure against
    the recorded 79,712 req/s. `map_err` may be a library function once Phase 1 lands. An
    `if let` is a language addition and may be the right refusal.
  - Done when: each item is either landed with a measurement or refused in `FRICTION.md`
    with a real cost estimate from reading the source. **A refusal with a number is a pass.**
  - Verify: whatever lands, measured against the recorded baseline (79,712 req/s, 8 clients);
    whatever is refused, the estimate cites `path:line`.

- [ ] **Phase 10 — re-score `FRICTION.md`, and settle this plan's Goal**
  - Walk every item in the file against the tree — phase 7 (`:118-131`), the created ones
    (`:133-161`, `:168-169`), and the earlier-phase list (`:219-235`, which includes the
    frozen-tychoc0 debris items that are *deliberately* kept and should be marked as such,
    not counted as open).
  - Rewrite `FRICTION.md:79`'s score section: closed, refused-with-reason, and deliberately
    kept — with no item left in "untouched, unexplained".
  - Then answer this plan's Goal honestly, to the standard the last plan was held to: what
    the fixes cost in library and compiler lines, whether `server/main.ty` got shorter this
    time (it went 371 → 380 last time and the plan said so), and which refusals a future
    reader should treat as the real remaining debt.
  - Done when: the score section matches the tree item for item, and the Goal verdict is
    written with numbers.
  - Verify: every "CLOSED" claim in `FRICTION.md` is checked against the code it names —
    spot-check by `path:line`, not by memory. A stale CLOSED is the one thing this phase
    exists to prevent.

## Out of scope

- **`compiler/tychoc0.ty`** — frozen 2026-07-26. `FRICTION.md:231-234` records four items
  about its debris (non-gated runners still building it, orphaned goldens, the `+50` citation
  correction). Those are **deliberately kept**, not open work; Phase 10 marks them as such.
- The two C-level defects at `FRICTION.md:171-179` (`SIGPIPE` killing the server, Nagle
  costing 620×) are **already fixed** in the shims and recorded as things a Tycho program
  could not have fixed. Nothing to do; do not re-open them.
- `FRICTION.md`'s "What was good" section (`:181-194`). It is the other half of an honest
  account and it stays exactly as written.
