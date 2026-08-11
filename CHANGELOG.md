# Changelog

All notable changes are recorded here against the version that shipped them.
The version constant lives in `src/tychoc.c` (`TYCHO_VERSION`, printed by
`tychoc --version`); bump both together. Per-release publishing notes stay in
`RELEASE_NOTES.md`; this file is the accumulating record.

## [0.5.0] — 2026-08-10

**The first release ever cut.** Everything below ships in it. The section was
split in two until 2026-08-10 — an `[Unreleased]` block on top of a `[0.5.0]`
block dated 2026-08-05 that was never tagged — which would have shipped a
changelog that did not describe its own artifact. The 08-05 block is a draft,
not history, so the two are merged here under the date this actually shipped.

- **`or_return` works in `main()`.** `fn main() -> Result(void, string):` is a
  second legal shape for the entry point, so error propagation no longer stops
  one frame short of where a program starts. On `Ok` the process exits 0; on
  `Err(msg)` the message is written to stderr — **bare, with no prefix**, the
  same way `die(msg)` prints a user message — and the process exits 1.

  The error type is `string` and the ok payload is `void`, both deliberately:
  an `Err` reaching the entry point is *printed* and `has_str` admits no enum,
  so the error is the message; and a value in the ok slot would make the exit
  status ambiguous. Both wrong shapes are refused with a diagnostic that names
  which half is wrong. Only expressible because `Result(void, E)` landed first.

- **DEPRECATED: `sort.by_key(xs, key)`** — use `sort.sort_by(xs, cmp)`. The
  rewrite is mechanical: `by_key(xs, k)` is
  `sort_by(xs, fn(a, b) -> int: k(a) - k(b))`. It keeps working for all of 0.x
  and is **removed in 1.0**. Calling it warns.

  This is the deprecation policy in `docs/guides/corelib.md` run end to end for
  the first time, which is the point — a policy that has never been executed is
  prose. Step (3), the compiler warning, is now a general mechanism rather than
  a one-off: a `# deprecated: <text>` comment line **directly above** a `fn`
  marks it, and every call site warns with that text. Locked by
  `tests/warn/deprecated.ty`.

- **`core:net`'s concurrency ceiling is now stated rather than implied.** There
  is no `poll`/`select`/`epoll`/`O_NONBLOCK` in any of its 12 exports, so a
  server's worker count is a hard limit on concurrent connections and one slow
  client occupies one worker. Written into the corelib guide instead of being
  fixed: readiness polling was costed at ~283 lines across 4 files plus a
  redesign of `core:httpd`'s read surface, and adding it later is additive.

- **The Windows parked-`recv` difference is a documented platform limit.** A
  thread parked in `recv` is not released by the shutdown handler as it is on
  Linux, so a Windows server winds down within its idle timeout rather than a
  millisecond. Nothing is lost or corrupted. Recorded in the README's platform
  notes; the measurement stays in `SECURITY.md`.

- **`pass`, the no-op statement.** A block cannot be empty, so a `match` arm or
  an `if` branch with nothing to do had to bind something nobody reads — `ok :=
  sz`, `_ := 0` — because a declaration is a legal statement where a bare
  expression is not. `pass` says it instead.

  It is **contextual, not reserved**: significant only when it is the whole
  statement. `pass` was already a variable name in `corelib/test/testing` and
  `tools/prunner`, and reserving it would have broken the runner that scores
  `make test-fast`, so `pass := 0` and `pass = pass + 1` still mean what they
  did. Used where a value is required — a value `if`/`match` branch tail — it is
  refused with a message naming the fix, unless a variable of that name is in
  scope, in which case it is that variable.

- **`core:sort` gains `sort_by(xs, cmp)`.** A three-way compare
  `fn($T, $T) -> int`, so an order can use several keys, mixed directions, or a
  type with no `comparable` instance — none of which fit `asc`/`desc`/`argsort`
  (values, one direction) or `by_key` (an `int` key, and no order-preserving
  injection of a string into one). Bottom-up merge, stable: equal elements keep
  their input order under a descending compare, which a total-by-index
  comparator would silently reverse.

- **`tychofmt` no longer writes `-1` as `- 1`.** Prefix negation is told from
  binary subtraction by what precedes it. Its two existing gates could not catch
  this: `- 1` is idempotent and emits identical C, so the formatter now also
  asserts canonical spellings.

- **`Result(void, E)`, for an operation that either fails with a reason or
  succeeds with nothing to report.** `void` is now spellable as a `Result`'s ok
  payload — and in no other position — as a type with no values: constructed by
  a zero-argument `Ok()`, matched by a bare `Ok:` arm, never bindable. The
  permission does not nest, so `Result(Option(void), E)` is still refused, and a
  `Result`'s error type still may not be void. Retires the one-field `Unit`
  struct that fallible-but-valueless helpers carried, and with it the
  meaningless `Ok(0)` at the end of every validator.

  Shipped with it, because the feature is unusable without it: **`f() or_return`
  is now a statement** when the ok payload is void. It was previously rejected
  as a bare expression with no effect, which left a void-payload `Result` no
  propagation form at all — `x := f() or_return` has nothing to bind. Over any
  other payload type it is still an error, now naming the type it would have
  dropped. `docs/internals/FRICTION.md` §4 recorded these as one defect with
  three independent sightings; they are closed together.

  Internally, the generic bind vector's "not yet bound" sentinel moved off
  `T_VOID` to its own `T_UNBOUND`. That collision was latent rather than live —
  `void` was not in the type grammar at all — but it is what would have made
  binding a type parameter to `void` read as unbound.

- **Demoted from 1.0.0 to 0.5.0.** `TYCHO_VERSION` in `src/tychoc.c` now reads
  `0.5.0`, and every document that promised a stable surface says pre-1.0
  instead. Nothing about the implementation changed and nothing regressed: the
  demotion is about the promise, not the code. 1.0 says "I will not break you",
  and that had never been tested — `git tag` showed only `v0.1.0`, the
  `[1.0.0]` entry below describes a release that was never cut, and no one
  outside this repo has written a line of Tycho. Withdrawn with the number: the
  corelib API freeze (`docs/guides/corelib.md`) and the spec's "first frozen
  version" (`docs/spec/00-conventions.md` §1.5). The spec stays normative and
  the implementation stays gated against it. `ROADMAP.md` gains "What 1.0
  requires" — the conditions, so the number is earned next time rather than
  declared.

- **Native Windows support (MSYS2 + mingw-w64).** The port ran as its own
  track: the compiler and runtime (SEH stack guard, binary stdio,
  winpthreads), the corelib shims (`core:io`'s directory classification,
  `core:signal`'s `SetConsoleCtrlHandler` branch, `core:datetime`'s
  `localtime_s`/`mktime`-difference, `core:regex` on `pcre2posix.h`,
  `core:net` and `core:tls` on winsock), a `_WIN32:` section in the `deps`
  manifest for Windows-only linker flags, the tools, and the harness itself
  (loud platform skips in `scripts/ci.sh`, `tests/run.sh`, `tests/conc`,
  `tests/ffi`, `tests/recursion`, `corelib/run.sh`; `check_goldens.py` moved
  to `posixpath` so goldens resolve against git's forward-slash paths).
  MSVC is not a supported C target; WSL2 remains the zero-setup path.
- **`make ci` is green on native Windows — 2026-08-08, x86-64 Windows 11
  26200 under KVM, MSYS2 + mingw-w64 gcc 16.1.0, gdb 17.2.** Exit 0 from a
  clean checkout with an empty working tree, and **114 skips**, every one of
  them printing its reason. Green means nothing reddened, not that everything
  ran: the sanitizer legs (mingw ships no ASan/UBSan runtime and gcc has no
  Windows-target TSan), the fuzz lanes, `ilp32`, `locale-check`, `asan-self`,
  `selfhost-check`, `bench-guard`, `core:signal`'s test, `server/run.sh`'s six
  shutdown cases and `tycho-ar`'s newline-in-filename member do not execute
  there. The prior recorded run (2026-08-07, ARM64 under Prism emulation) was
  `make test` 589/591, `conc` 34/38, `corelib` 43 ok / 2 fail; the residuals it
  could not separate from emulator noise are resolved below.
- **Six port defects the sweep found, each measured before and after.**
  - *The `tycho` dispatcher was entirely broken on Windows.* `tycho_run` is
    `system()`, which is cmd.exe there: `> /dev/null` is a path and not a sink,
    so `run`, `build`, `check`, `watch` and `fmt` all died at their first
    shell-out — each reporting a misleading cause (`check` said "has errors"
    about a file that compiles). `rm -f`/`cp`/`mv`/`diff -q` moved into
    `tools/tycho_shim.c` as native calls, `MoveFileEx` keeps `fmt -w`'s atomic
    install, and the file comparison returns 1/0/**−1** so an unreadable file
    cannot compare equal and fail open. The five commands had no lane of their
    own; `scripts/tools_check.sh` gained one.
  - *`tycho-debug` was dead on Windows*, all 6 legs, while shipping in the
    release tarball. Three causes: the same cmd.exe shell-outs; `_fullpath`
    answers in backslashes while gdb reports forward slashes, so every stop
    took the "runtime glue" branch (whole path, raw `h_main`, no source line);
    and `_fullpath` succeeds for a file that does not exist, so the "no such
    file" refusal never fired.
  - *`core:time` violated its own contract.* `sleep_ms(200)` read back 199 —
    winpthreads wakes a timer tick before the observing clock crosses the mark.
    1 in 60 under CPU contention, 0 in 60 idle, which is why it surfaced only
    inside a full sweep. The Windows branch now sleeps against a deadline read
    from the same `CLOCK_MONOTONIC` the caller measures with.
  - *`tycho-fetch`'s `tar` line was spelled for `sh`*, so cmd handed `tar`
    paths with the quotes still in them and every fetch died as "not a tarball
    with a single top-level directory" — naming the wrong cause.
  - *`check_citations.py` passed by not looking.* `git ls-files "*.md"` through
    `subprocess` returns 8 files on Windows where the shell returns 126, so the
    gate reported ok over 8 anchored and 6 bare citations instead of 134 and
    812. It filters by suffix in Python now; both platforms print the identical
    summary. Same class as the `posixpath` bug in `check_goldens.py`.
  - *Nine CI lanes carried POSIX assumptions* — five sanitizer legs, the server
    shutdown cases (which **hung** the sweep for 43 minutes rather than failing
    it), `selfhost`, `bench-guard`, and leg 7's `file://`/recipe/`.exe` paths.
- **The packaged Windows tools are now run, not just built.** The
  `release.sh --mingw` layout was previously smoke-tested only as far as
  `tychoc.exe --emit-c` under Wine. Staged and exercised on Windows: a full
  build with `corelib/` found beside the compiler and no `TYCHO_CORELIB`, a
  `core:strings` import resolved from that corelib, `tychofmt.exe` formatting
  idempotently, `tycho-lsp.exe` answering a framed `initialize`, and
  `tycho-debug.exe` setting and hitting a breakpoint under real gdb.
- **`scripts/release.sh --mingw` now ships the tools.** The Windows tarball
  carries `tychofmt.exe`, `tycho-lsp.exe` and `tycho-debug.exe` beside
  `tychoc.exe` (parity with the native leg), and the staged layout is
  smoke-tested under Wine — the packaged compiler must find `corelib/` beside
  itself and emit C — skipping loudly when Wine is absent.
- **A line ending on an operator continues onto the next one.** Brackets
  already joined lines, so `(1 +\n2)` worked; the unparenthesised form reported
  `expected an expression` pointing at the FOLLOWING line, which reads as a bug
  in the next statement rather than a missing operand. Now a physical line whose
  last token cannot end an expression — arithmetic, comparison, bitwise or
  logical operator, `,`, `=`, `:=` — joins the next line **when that line is
  indented strictly deeper than the statement's first line**. `:` is excluded;
  it opens a block. Because only tokens that cannot end an expression qualify,
  no previously-accepted program changes meaning.
  **The deeper-indent condition exists because the naive rule degraded a
  diagnostic the project had deliberately locked.** `tests/diag/caret_expr.ty`
  is `x := 1 +` followed by `print(str(x))` at equal indent: joining them parses
  as `x := 1 + print(str(x))` and reports `unknown variable 'x'` on the wrong
  line, about a variable that is merely being defined. The golden caught it; the
  indent condition keeps the original `expected an expression` with its caret,
  and still joins every continuation anyone writes. Spec §3.2 rewritten — it had
  claimed there was no implicit line joining at all, which was already false for
  brackets. Fixture: `tests/line_continuation.ty`.
- **A procedure that shadows a builtin now warns.** Declaring `fn len(...)` in
  a package is legal and stays legal (spec §3.7 makes builtins ordinary
  identifiers on purpose), but it silently took over every unqualified call to
  that name inside the package — so a package's own 4-character string could
  report whatever its `len` returned, with nothing on stderr. It was the only
  entry on the 1.0 papercut list that produced a wrong *answer* rather than an
  error. `core:utf8` hit it: `decode` called the package's own `len` and
  recursed to the stack guard, which is why that API is `count` today. The
  warning fires at the declaration and names the consequence; which procedure
  gets selected is unchanged, so no program changes behavior. Spec §3.7 records
  that an implementation is expected to warn. Fixture:
  `tests/warn/shadow_builtin.ty`.
- **`make selfhost-check` retired; nothing builds `tychoc0` now.** It was
  `make ci` step [3n/20] and cost ~50s of every sweep re-asserting that the
  frozen compiler's own emission reproduces itself. `ROADMAP.md` has claimed
  since 2026-07-29 that "nothing builds it at all"; that sentence is true as of
  this change and was not before. `compiler/selfhost.sh` is kept unrun, with a
  header recording what it proved — the same treatment `compiler/fixpoint.sh`
  and `scripts/frontparity.sh` got. What is given up: the byte-identity is no
  longer re-checked at each HEAD. What is not: `compiler/tychoc0.ty` is still
  compiled by every sweep as ASan/UBSan **input** to `scripts/asan_self.sh`, so
  the largest Tycho source in the tree still exercises the compiler, and
  self-hosting remains a fact about the commit that proved it.
- **`core:os` gains `exec` / `exec_out` — an argv no shell parses.** They take
  a `[string]` and start no shell: `posix_spawnp` on POSIX, `CreateProcess`
  with no `cmd.exe` anywhere on Windows. This closes the injection class
  `SECURITY.md` had carried as its one open backlog item; `system`/`run` keep
  their documented shell semantics and nothing is deprecated. A `[string]`
  cannot cross the FFI (`docs/spec/14-ffi.md` crosses only `[int]`/`[float]`),
  so argv is pushed into an opaque builder one string at a time. Fail closed
  with `-1` on an empty argv, over 4096 entries, an allocation failure
  mid-build, or a program that cannot be spawned. Windows joins the vector by
  the `CommandLineToArgvW` quoting rules, round-tripped through the real
  splitter by `corelib/os/os_argv_quotecheck.c` — a new `make shim-check` leg
  that builds and RUNS on Windows (9 cases: embedded quotes, trailing
  backslashes, an empty argument, shell metacharacters) and skips loudly
  elsewhere. Residual and callee-side: a program that parses its own command
  line by other rules — `cmd.exe`, a `.bat` file — can still read it
  differently.
- **The last two Windows skips are gone.** `corelib/test/signal` was skipped
  because MSYS2's `kill` terminates a native PE instead of signalling it; it
  now steps onto a private console and raises a real `CTRL_BREAK`, and its
  output there is byte-identical to the Linux golden. `make corelib` on
  Windows has **zero skips**. `server/run.sh`'s six shutdown cases and the
  access-log tail run there too, through `server/winsignal.c`, which launches
  the server with `CREATE_NEW_PROCESS_GROUP` so the event can target it alone
  — group 0 would Ctrl-Break the sweep's own console — and reports the
  **Windows** pid, since `$!` is an MSYS pid and the console API will not take
  it. One measured behavioural gap stays and is recorded in `SECURITY.md`: a
  thread parked in `recv` on an accepted connection is not released by the
  handler's `shutdown()` as it is on Linux, so a Windows server winds down
  within its idle timeout rather than within a millisecond.
- **`goldens-check` never saw the `.win` siblings.** A `<golden>.win` ends
  `.win`, not `.out`/`.err`, so the token scan matched none — three existed
  and all three went unchecked, on the platform whose harness is thinnest. The
  gate now derives them from the resolved set: 432 files where it reported
  429.
- **The Wine lanes skipped on every modern Wine.** Five lanes and
  `release.sh` guarded on `command -v wine64`; Wine 9.0 merged it into a
  single `wine`, so the guard was false on any current install and each lane
  exited 0 saying "wine64 not on PATH". `release.sh` had it worst — the same
  test decided whether the staged Windows tarball got its smoke test, so it
  shipped unrun from a box that could have run it.
- **Compiler: a pattern discard binds nothing.** `Sized(_, _)` emitted one C
  local per binding named `h__`, so two discards in one pattern declared the
  same name twice and the generated C failed to compile. `_` in a pattern is
  now a discard everywhere: it fills its payload slot for the arity check,
  binds nothing, is not referenceable in the arm body, and is not emitted —
  while the arm's body still runs (`Err(_)`). Fixtures
  `tests/match_underscore.ty` and `tests/reject/match_underscore_use.ty`.
- **Diagnostic: bare `Ok`/`Err`/`None` in a `:=` or typed value-branch.** The
  old message suggested annotating the binding (`x : T = if/match ...`),
  which rejects the same program — neither form flows a destination type into
  branch unification. The message now states the rule and names the tails
  that do flow one in (an assignment or a return).

### Earlier in the 0.5.0 cycle (recorded 2026-08-05, at the time as 1.0.0)

The promotion that was later withdrawn. The language surface and the spec are
**not** a stability contract at 0.5 — the README's status banner says so, and
`ROADMAP.md` lists what 1.0 would require. What follows is the chain of work
that landed before the version number was corrected.

- **Batteries-included corelib (phase 1).** Six new packages, each with a test
  + golden + guide entry: `core:testing` (assert/eq/report unit-test state),
  `core:toml` (recursive table/array parser, fail-closed), `core:log`
  (leveled logging, threaded state), `core:sqlite` (direct FFI over
  libsqlite3 — the dbquery pattern), `core:utf8` (validation and codepoint
  iteration, strict rejection of overlong/surrogate/truncated sequences), and
  `core:zip` (reader/writer over a new raw-deflate path in `core:compress`,
  CRC-32 in-package). `core:compress` gained raw deflate/inflate.
- **Vendored dependencies (phase 2).** The importer-relative import story
  documented as the vendoring convention (`vendor/` layout, flat-vendor
  shape); `tycho-fetch <url> <name>` downloads and unpacks a package tarball
  into `vendor/` with sha256 verification (dogfooding core:http + core:sha256
  + core:io); `core:http` gained `body_bytes(r)` — the string `body()`
  truncated at NULs, so binary downloads were impossible.
- **Debugger (phase 3).** `tycho debug <file.ty>` / `tools/tycho-debug/` — a
  gdb adapter: compiles with `-g`, drives a scripted gdb session over pipes
  (breakpoints by source line, step/next, locals under their Tycho names,
  Ctrl-C interrupts a running inferior). Gated by `make debug-check`.
- **1.0 promotion (this phase).** `tychoc --version` and the version constant;
  the README's status banner flips from research prototype to the 1.0
  contract (stable: the language surface and the spec; not stable:
  performance tuning and benches); a security review of the FFI shims
  (string/bytes ownership, the core:os shell-out paths, the TLS wrapper)
  with findings recorded in `SECURITY.md`; and the corelib 1.0 API-freeze
  decision with its deprecation path, recorded in `docs/guides/corelib.md`.

## [0.1.0] — earlier

The pre-1.0 research-prototype release. See `RELEASE_NOTES.md` at the
`v0.1.0` tag for what it contained; it carried no stability guarantees.
