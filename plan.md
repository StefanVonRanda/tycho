# What comes next

> 2026-08-09: owner-set agenda, in this order — (2) close the Windows lanes,
> (3) `core:os` array-argv, then (6) `core:net` readiness. **No release until
> 2 and 3 are done.** Two items were struck: continuing to check against the
> frozen `tychoc0` ("we're wasting time"), and the deferred demand-gated
> corelib work (`core:timezone`, JPEG/GIF), which is parked.
>
> **2 and 3 landed 2026-08-09** — see the evidence block at the bottom of this
> file. `make selfhost-check` was retired the same day on the owner's explicit
> instruction ("we're done with it"): it was `make ci` step [3n/20], ~50s of
> every sweep, and the last thing in the tree that BUILT `tychoc0` — which is
> what finally makes `ROADMAP.md`'s "nothing builds it at all" true.
> `compiler/selfhost.sh` is kept unrun with a tombstone header.
>
> Item 6 (`core:net` has no `poll`/`select`/`O_NONBLOCK` in any of its 12
> exports, so worker count is a hard concurrency cap) is untouched and was
> refused once before with a number: ~283 lines across 4 files plus a redesign
> of `core:httpd`'s read surface.
>
> Branch workflow dropped on the same instruction — work commits straight to
> `main` and is pushed there.

> 2026-08-04: the three-phase optimization chain (housekeeping, deterministic
> hash, map memory) is complete and the closing `make ci` is green. New
> owner-directed agenda for making Tycho day-to-day usable, in this order:
> (1) a batteries-included corelib, (2) a vendor-based dependency story
> (Go/Odin style — vendor your dependencies), (3) a debugger, (4) 1.0
> promotion. Explicitly NOT on the agenda: a REPL (the owner never reaches for
> one) and native Windows (deferred, WSL suffices — **that deferral was
> retired on 2026-08-07**: the native Windows port ran as its own track in
> `plan_windows.md` and MSYS2/mingw is now a supported path; see the README's
> platform notes). The corelib demand-gate is
> waived for Phase 1 by the owner's explicit batteries-included instruction;
> every package still ships with its test + golden + corelib guide entry per
> the established convention.

## Phase 1 — Batteries-included corelib

Sub-phases, each verified and committed on its own, in this order. Probed
against a Go/Odin stdlib baseline: these are the missing pieces real programs
touch every day. The corelib structure convention holds for each: a
`corelib/<name>/<name>.ty` package (plus `<name>_shim.c` when FFI is needed,
with a pkg-config `deps` manifest and the skip-if-absent convention),
`corelib/test/<name>/main.ty` + golden (re-record with
`RECORD=1 sh corelib/run.sh`), and a `docs/guides/corelib.md` entry.

Gate per sub-phase: `make corelib` + `make goldens-check` + doc gates. NOT
`make test` (tests/run.sh never descends into corelib — it cannot redden).
A new shim adds `make shim-check`. Expected per sub-phase: all existing
corelib tests green + the new one; the new golden locked.

### 1.1 `core:testing` — unit-test framework (the daily-dev gap)

No test framework exists: the harness is golden-output-based, and a unit-test
package is what day-to-day development actually uses. Library-first (no new
syntax): an `assert(cond, msg)` / `assert_eq(a, b)` family collecting failures
into a `Test` state (threaded like core:rand's state — no globals), and a
`run()`/`report()` that prints pass/fail per test and exits non-zero on any
failure. Design freedom on the exact shapes (e.g. a `test_case(name, fn())`
registration vs a returned list) — decided in the phase.

### 1.2 `core:toml` — config parsing

The common config format for real programs; nothing exists. Pure Tycho (like
core:json/csv): `toml.parse(s) -> Result(Table, string)` covering tables,
dotted keys, arrays, arrays-of-tables, and the scalar set (string/int/float/
bool/datetime-iso), fail-closed on malformed input, plus the basic
`get`/`keys` accessors. Exact surface mirrors core:json's shape where
sensible.

### 1.3 `core:log` — leveled logging

A leveled logger (debug/info/warn/error), state threaded explicitly (no
globals): `log.init(&st, level, dest)`, `log.info(&st, ...)`, timestamps via
the existing clock, stderr/stdout destinations. Pure Tycho over core:io + the
clock builtin.

### 1.4 `core:sqlite` — SQL database

The dbquery bench proves the FFI pattern; package it. `corelib/sqlite/` with
`sqlite_shim.c` over libsqlite3 (deps manifest names `sqlite3`, pkg-config,
skip-if-absent). Scope: `open`/`close`, `exec` (no results), `query` (SELECT →
rows as `[[string]]` or a typed row struct — decided in the phase), error
reporting (the sqlite error message), parameter binding for the common cases.
A small real fixture (create/insert/select/join) in the test.

### 1.5 `core:utf8` — Unicode validation and iteration

Strings are bytes today; text processing needs a codepoint view. Pure Tycho:
`utf8.valid(s)`, `utf8.decode(s, at) -> (cp, width)` (fail-closed on a
truncated/invalid sequence), `utf8.encode(cp) -> string`, `utf8.len(s)`
(codepoint count), and a rune-wise iteration idiom. Full Unicode tables
(case mapping, normalization) deferred — validation + iteration is the
batteries-included core.

### 1.6 `core:zip` — archive reader/writer

Deployment needs archives; only gzip/zlib exists. Pure Tycho over
core:compress: `zip.list(bytes) -> [Entry]`, `zip.extract(bytes, name) ->
bytes` (fail-closed on a corrupt/truncated archive, no path traversal —
entries are names, not paths), `zip.create(entries) -> bytes`. Reader first,
writer second, inside the same sub-phase.

### 1.7 `core:timezone` (rich datetime) — deferred

The roadmap names "richer date/time"; a timezone database is a large,
data-heavy package. Held until a real program needs it (the one item where
the demand-gate is NOT waived — it is the roadmap's own example of a gated
add).

### 1.8 image codecs (JPEG/GIF) — deferred

core:image is PNG (libpng), core:raster is BMP/QOI. JPEG via libjpeg FFI and
a pure-Tycho GIF are real, but no program in-tree needs them yet; attach to
this phase only if a caller appears.

## Phase 2 — Vendored dependencies (Go/Odin style)

Vendor your dependencies — no central registry, no version resolution. The
mechanism is ALREADY COMPLETE and verified end-to-end: `import "PATH"`
resolves relative to the importing package's own directory (cwd-independent),
so `import "vendor/foo"` from the entry and `import "../bar"` from inside a
vendored package compile and run as-is, mixed with `core:` imports;
multi-package merge, name mangling, visibility and cycle detection are all
spec'd and gated. tycho-build needs nothing — its recipes are shell lines; the
compiler resolves the imports. So the phase is documentation + one
convenience command, NOT compiler surface:

- **Document the vendoring convention** in `docs/guides/packages.md`: the
  `vendor/` directory pattern, the importer-relative resolution rule, the
  flat-vendor convention that keeps `../` chains at depth one, and the
  current "only the `core:` collection is exposed today" note corrected (the
  relative-import story works today, undocumented).
- **`tycho fetch <url> <name>`**: download a package tree and unpack it into
  `vendor/<name>`, with checksum verification. The HTTP + io + path + sha256
  pieces are proven by examples/fetch; the tool is a small addition to the
  `tycho` dispatcher, dogfooding core:http/core:io/core:path/core:sha256.
- **A tycho-build leg**: build-check gains a vendored-project fixture (entry
  + two vendored packages, one importing the other) proving the whole story
  builds through the build tool.

Explicitly NOT in scope, deferred until a real caller: a `vendor:` named
collection root (Odin's `collection:` mechanism). Relative imports cover the
story; the demand-gate says a second resolution rule waits for a real
multi-entry or nested-layout project to feel the `../` pain.

Gate: `make build-check` (the new leg) + doc gates. No compiler change, so no
`make test` / `make selfhost-check` / citation shift.

## Phase 3 — Debugger

`-g` already emits `#line` mapping and compiles (verified by tools-check's
line-info leg). The pragmatic route: a **gdb adapter** — the compiler's `-g`
output drives a scripted gdb session (breakpoints by source line, `next`,
locals via the emitted C names), wrapped in a `tycho debug <program>` command
in the dispatcher — rather than a from-scratch debugger. A minimal built-in
(breakpoint/step/locals over the runtime) is the alternative; decided in the
phase. The debugger itself is a Tycho tool (tools/tycho-debug/), dogfooding
core:os + core:signal.

Gate: `make tools-check` (the tool's lane) + `make test` (if any `-g`
emission changes) + `make build-check` if the dispatcher changes + doc gates.

## Phase 4 — 1.0 promotion

The last bit of prep, after 1–3 settle:

- **Versioning**: a version constant in the compiler, `--version`, and the
  changelog discipline (each phase's changes recorded against it).
- **Stability statement**: replace the README's "pre-1.0: no stability
  guarantees" with the 1.0 contract — what is stable (the language surface,
  the spec), what is not (performance tuning, benches).
- **Security audit**: the README names "no security audit" as a pre-1.0
  caveat; close it — a review of the FFI shims (string ownership, the
  shell-out paths in core:os, the TLS wrapper) with the findings recorded.
- **API freeze decision**: which corelib packages are in the 1.0 surface and
  what the deprecation path is.

Gate: the full `make ci` as the closing sweep, plus the doc gates.
Expected: the README's status banner flips from research prototype to 1.0.

## Phase 5 — `Result(void, E)` (ROADMAP order-of-work item 3)

Set 2026-08-09. Three commits, each verified on its own. The ROADMAP's table
recommended "re-sentinel `T_VOID` → `T_UNBOUND`" and said that buys the
expected spelling. **Probing says the re-sentinel is necessary but not
sufficient**: `void` is not in the type grammar at all today, so the checks at
`src/tychoc.c@Option(void) is not a type` and its two neighbours are
defensive-unreachable, and the `T_VOID`/unbound collision is purely latent —
it bites the moment `void` becomes spellable. Both halves are in scope here.

Surface decided by the owner, 2026-08-09: **zero-arg `Ok()`**, matched by a
bare `Ok:` arm, like a fieldless enum variant. `void` is a type with no values
— it is NOT spellable as an expression, and `Ok(void)` is not accepted.

### 5.1 Re-sentinel `T_VOID` → `T_UNBOUND`

Add `T_UNBOUND` to the type enum and use it as the bind-vector's "not yet
bound" value. Sites: `new_binds`, `subst_type`, `match_type`, and the two
post-match unbound checks in the generic-instantiation paths. Every bind
vector in the tree comes from `new_binds()` — no `calloc`ed one to miss.
Pure refactor, no behaviour change intended.

- Gate: `make test`. **Baseline measured 2026-08-09 before any edit: 595
  passed, 0 failed** (`make test-fast`, byte-identical corpus). CLAUDE.md's
  "560 fixtures" is stale prose, not the number to compare against.
- The two source edits are deliberately **line-count-neutral** (12 changed,
  12 changed), so no `src/tychoc.c:N` citation anywhere in the tree moves and
  `check_citations.py` stays green with no re-anchoring. Worth the awkward
  long comment lines: the ROADMAP prices a re-anchor at 50-110 anchors.

### 5.2 `void` as Result's ok payload

- Parser: `void` in type position, permitted **only** as Result's first type
  argument (one-level permission flag, cleared on entry to `parse_type_inner`
  so `Option(void)` inside a `Result(...)` still dies).
- `Ok()` with an empty argument list parses to `E_OK` with `lhs == NULL`;
  `Err()` stays an error (a Result's error type may not be void).
- Resolver: `Ok()` checks against `Result(void, E)`; a bare `Ok:` match arm is
  the only accepted Ok pattern when the payload is void, and `Ok(x)` on one is
  an error.
- Codegen: the `okv` field is emitted as a `char` placeholder for a void
  payload, `Ok()` initialises it to 0, `gen_match_side` binds nothing.
  `type_mangle_ident(T_VOID)` becomes `"void"` (it was the `t0` fallback).
- Fixtures: one runnable fixture + golden, plus reject fixtures for
  `Option(void)`, `Result(int, void)`, `Ok(x)` on a void payload, `x := f()`
  binding a void or_return, and bare `void` in an ordinary type position.
- Gates: `make test` (expect 560 + the new fixtures), then
  `sh scripts/tools_check.sh` and `make editors-check` — the new fixtures put
  the token `void` in type position in front of every parser in the tree.

### 5.3 Docs

Spec chapter for Result, `docs/internals/FRICTION.md`, `ROADMAP.md` (strike
item 3 and correct the "re-sentinel buys the spelling" claim), CHANGELOG.

- Gates: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`,
  `sh scripts/spec_check.sh`, `make docs-fences`. **Not `make test`** — no
  compiled artifact changes.

Closing sweep: one `make ci` after 5.3, not per sub-phase.

## Not in scope

- A REPL (owner decision), ~~native Windows (deferred; WSL is the supported
  path)~~ — **retired 2026-08-07**, the port landed on its own track and MSYS2
  is supported alongside WSL2 — a package registry (vendoring is the model),
  Unicode tables beyond
  validation/iteration, image codecs and timezone until a caller appears,
  and a `vendor:` named collection root (Odin-style) until a real multi-entry
  or nested-layout project needs it.

> Phase 1 evidence — 2026-08-04: all six packages shipped, each verified and
> committed on its own: 1.1 core:testing (check/eq/report + exit-code
> contract), 1.2 core:toml (recursive Value enum, tables as parallel
> key/value arrays; dotted keys, [table], [[array-of-tables]], escapes incl
> \uXXXX, int/float/bool, arrays, ISO datetimes as strings; fail-closed),
> 1.3 core:log (leveled, threaded state, stdout/stderr), 1.4 core:sqlite
> (direct FFI over libsqlite3, no shim — the dbquery pattern; open/close/
> exec/query/params, rows as [[string]], sqlite's own messages), 1.5
> core:utf8 (valid/decode/encode/count, strict rejection of overlong/
> surrogate/truncated), 1.6 core:zip (reader/writer over core:compress's new
> raw-deflate path; CRC-32 in-package; the writer's output verified
> byte-exactly by Python's zipfile during development). Each package: test +
> golden + guide entry. Gates: make corelib all green (46 packages), goldens-
> check ok, shim-check 12 ok / 1 skipped (the compress shim grew raw
> deflate), doc gates ok.
>
> Learnings recorded (several are real language sharp edges a package author
> hits daily): a package-level `len` SHADOWS the builtin inside its own
> package (utf8.decode called utf8.len recursively until the stack guard
> fired — the API became `count`); two `_` bindings in one multi-payload
> pattern collide in the emitted C (compiler bug surfaced — use named
> bindings); there is no bare `pass` and no `defer`; arithmetic refuses
> int/float mixing (use to_float); `or_return` requires a Result-returning
> fn (main() can't); a newtype-of-array blocks push (dropped the Row
> aliases — query returns [[string]]); the null pointer is `null`, not
> ptr(0); the typed empty of [[string]] is [][string]; FFI out-params must
> be `inout ptr`; lines can't continue with a trailing `+`; consts don't
> cross package boundaries (levels exposed as functions). The two-`_`
> compiler bug is the one finding worth a follow-up fix in src/tychoc.c.
>
> History note: on the owner's instruction the Co-Authored-By trailer was
> stripped from all 111 commits that carried it (including this session's);
> the pre-rewrite state is preserved in refs/original.

> Phase 2 evidence — 2026-08-04: all three pieces landed, gates green
> (make corelib incl. the http test, shim-check, goldens-check, doc gates,
> tools-check, and make build-check now 7 legs).
>
> The re-scoped plan held: the compiler needed NO change (verified again —
> the vendored project compiles and runs). (1) packages.md gains a
> "Vendoring" section documenting the vendor/ convention, the
> importer-relative rule (cwd-independent), the flat-vendor shape that keeps
> ../ chains at depth one, mixed core:+vendor, and the corrected "only the
> core: collection" note — a named vendor: root explicitly deferred. (2)
> `tools/tycho-fetch/main.ty` — a standalone tool (its own directory, like
> tycho-ar/tycho-build, because importing core:* needs a package header which
> would pull every tools/*.ty into the compile — the repo documents that
> trap); `tycho-fetch <url> <name>` downloads via core:http, prints the
> sha256, unpacks the tarball into vendor/<name> (tar does the extraction —
> a shell step like the build tool's recipes; the tarball must have a single
> top-level directory), fail-closed on HTTP errors, unsafe names (no ../),
> existing dest, and malformed tarballs. (3) build-check gains leg [7]: the
> whole story end to end — fetch two packages from local file:// tarballs,
> build a project whose entry imports "vendor/greet" which imports "../util",
> run the binary. The fetch test is fully offline.
>
> One real corelib gap found and fixed en route: `http.body` truncates at the
> first NUL (the FFI string return), so binary downloads were impossible —
> core:http gains `body_bytes(r) -> bytes` (a shim that hands over a
> malloc'd copy, matching the bytes-from-C convention where the wrapper
> frees; returning the Resp's own buffer double-freed — found and fixed).
> tycho-fetch uses it, which is its integration test.

> Phase 3 evidence — 2026-08-05: the gdb adapter landed, all gates green
> (tools-check incl. the emit-c of the new tool and the changed dispatcher,
> debug-check 6 legs, build-check 7 legs, doc gates, goldens-check).
>
> DECISION — gdb adapter over a built-in debugger, per the phase's "decided in
> the phase": `tools/tycho-debug/` (its own directory, the tycho-ar trap),
> `tycho-debug [-b line]... <program.ty> [args...]`, wrapped as `tycho debug`
> in the dispatcher (TYCHODEBUG env, same pattern as TYCHOFMT). The tool
> compiles with `-g` itself (absolute path, so breakpoints resolve from any
> cwd), spawns gdb in MI mode over two pipes (a small fork/pipe/signal shim,
> debug_shim.c — core:os is system/popen only and can't express a live
> session), and runs a REPL: b/r/n/s/c/p/loc/bt/l/`$ <gdb>`/q, Ctrl-D quits,
> Ctrl-C interrupts. Breakpoints are by source line; locals are shown with the
> `h_` prefix stripped and arena machinery (`_parent`, `_scope`, `_t`,
> `_scr*`) hidden; `p` and `$` use the raw emitted C names. `-g` line info is
> single-file only, so a `package` build is refused up front with the
> compiler's own explanation.
>
> The interrupt design was probed empirically on gdb 17.2 and the naive route
> rejected: `-gdb-set mi-async on` makes the FIRST -exec-run miss every
> breakpoint and swallow the inferior's output, and in sync mode gdb does not
> read commands while the inferior runs (so MI `-exec-interrupt` sits
> unprocessed in the pipe). Working design: gdb in its own process group
> (setpgid in the shim), the tool's SIGINT handler sets a sig_atomic_t flag,
> a blocked read EINTRs, and the tool sends SIGINT to the INFERIOR's pid
> (parsed from `=thread-group-started`; killed via the shim) — gdb is the
> tracer, so it intercepts the signal, stops the inferior at its CURRENT
> source line, and reports `*stopped`. One consequence: core:signal is NOT
> dogfooded (its API is socket-shutdown-specific; a debugger frontend needs a
> raw SIGINT flag + kill, which live in the shim) — documented in the tool
> header and debugging.md.
>
> Gaps recorded: the debuggee's stdin is not forwarded (a program that reads
> stdin can't be debugged interactively), a Ctrl-C before gdb reports the
> inferior pid is dropped (press again), and `next` across a loop-duplicated
> exit line can land on the loop body (faithful to the emitted C; the -g
> model documents debugging at the generated-C level for such cases).
>
> Lane: `make debug-check` ([3p/22] in ci) — behavioral, no golden, because
> the transcript carries gdb's own output (addresses, thread noise) that
> drifts across gdb versions; asserts breakpoint set + hit on the right line,
> stripped locals, print, step, clean quit, run-to-completion with the
> program's own output, a real Ctrl-C interrupt of a running inferior, three
> fail-closed refusals, and the `tycho debug` wrapper end to end. SKIPS
> loudly when gdb is absent (an external dep like sqlite3/libpng).
>
> `make test` was NOT run and cannot redden: the compiler is untouched (-g
> emission is pinned by tools-check's line-info leg), and tests/run.sh never
> descends into tools/.

> Phase 4 evidence — 2026-08-05: 1.0 promotion landed, closing `make ci`
> GREEN. Four parts:
>
> (1) VERSIONING. `TYCHO_VERSION "1.0.0"` in src/tychoc.c (the single source
> of truth), `tychoc --version` prints it, and CHANGELOG.md records releases
> against it (1.0.0 entry covering phases 1-4; [0.1.0] noted as the
> pre-1.0 release). scripts/release.sh now verifies the version it is given
> against `tychoc --version` (mismatch = exit 2, so the tarball and the
> compiler can never disagree) and ships tycho-debug in the tarball.
>
> (2) STABILITY STATEMENT. The README banner flips from "research prototype,
> pre-1.0: no stability guarantees" to the 1.0 contract: stable = the
> language surface and the spec (bugs in them are regressions); not stable =
> performance tuning/benches, internal implementation details (emitted C
> shape, arena sizes), and the documented sharp edges. ROADMAP, RELEASE_NOTES,
> CONTRIBUTING and architecture/memory-model guides updated to the same
> language; the thesis keeps its original framing (it is the historical
> academic document).
>
> (3) SECURITY AUDIT. Reviewed the FFI shims named by the phase and recorded
> the findings in SECURITY.md. The two ownership conventions verified per
> shim: `-> string` returns are arena-copied and NOT freed (shim contract:
> recycled/__thread buffers or handle-owned buffers freed by a paired call —
> crypto's __thread hex buffer, io's getline buffer, os/http's handle-owned
> buffers all correct); `bytes` out-params are copied AND freed by the
> runtime (tls/compress/image correct). core:os shell-out: system/popen via
> sh -c verbatim — injection is caller-side, documented, with the array-argv
> form named as the backlog item that removes the class. TLS wrapper:
> SSL_VERIFY_PEER + system CA + hostname + SNI + TLS1.2 min, fail-closed —
> sound; residual findings (empty-on-EOF-vs-error, uncapped read max, no
> ALPN/pinning) recorded as non-memory-safety. Residual risks stated: the
> FFI boundary stays unsafe by design, no formal third-party audit.
>
> (4) API FREEZE. All 46 corelib packages are in the 1.0 surface (each
> tested three ways + golden-locked), with the caveats inside the surface
> (non-crypto hashes, NUL caveat, FFI deps' skip-if-absent) and the
> deprecation path (doc notice + changelog entry + compiler warning; removal
> only in a major bump, never 1.x). Recorded in docs/guides/corelib.md.
>
> Compiler change = citation shift, as the phase-2 note predicted: the
> version define (+6 lines) and --version flag (+7 above line 13322) moved
> src/tychoc.c line numbers, and 99 anchored citations across docs/spec,
> FRICTION.md, asan_self.sh and fuzz/run_parforparity.py were re-anchored
> mechanically (each token's new line verified before the edit, then the
> citation gate re-run to zero).
>
> Gates: the full `make ci` (the phase's gate and the deferred phase-3
> confirmation) is GREEN — including the new [3p/22] debug-check step — plus
> the doc gates. `make test` ran 591/591 under `env -u LD_PRELOAD` (the
> tmux block-nnp.so shim pollutes the ASan lanes; the tree is not at fault —
> see CLAUDE.md).

> Windows lanes + `core:os` argv evidence — 2026-08-09. Five commits on
> `os-argv-and-windows-lanes`, each verified on BOTH boxes: this Linux host and
> the WinBoat VM (podman `ghcr.io/dockur/windows`, Windows 11 26200.8037,
> MSYS2 + mingw-w64 gcc 16.1.0, gdb 17.2, repo at `/c/tycho` with `origin`
> pointing at the host share). The VM is reachable over ssh on
> `127.0.0.1:2222`; two things a future session should not rediscover —
> OpenSSH-on-Windows hands the command line to **cmd.exe**, which shreds shell
> quoting (ship the script as base64 and decode inside bash), and `gcc`/`gdb`
> exist only under `MSYSTEM=MINGW64`, not plain MSYS.
>
> ITEM 3, `core:os` argv. `exec(argv)`/`exec_out(argv)` over `posix_spawnp`
> and `CreateProcess`. A `[string]` cannot cross the FFI at all, so argv goes
> through an opaque builder — the NUL-joined-blob alternative is the defect
> `http.body_bytes` exists to work around. `posix_spawnp` and NOT fork+execvp
> because a Tycho program is threaded and the arena allocator is not
> async-signal-safe. The POSIX security property is asserted BOTH ways in
> `corelib/test/os` so it can fail; the Windows quoter is asserted against the
> real `CommandLineToArgvW` in a new `shim-check` leg, because Windows
> guarantees no program that echoes argv without re-parsing it.
>
> ITEM 2, the Windows lanes. `make corelib` on Windows now has ZERO skips
> (`corelib/test/signal` raises a real CTRL_BREAK on a private console), and
> `server/run.sh` reports `server: OK` there with one loud skip. Two defects
> found in the harness itself along the way, both of the same class — a check
> that silently covered nothing: `goldens-check` never scanned a single
> `.win` golden, and all five Wine lanes plus `release.sh` guarded on a
> `wine64` binary that modern Wine no longer ships.
>
> THREE THINGS MEASURED, each of which cost a round:
>   * A console control handler does NOT follow a process to a newly allocated
>     console. Registered before the switch and not after, CTRL_BREAK killed
>     the test at exit 130 instead of reaching the handler.
>   * A thread parked in `recv` on an accepted connection is not released by
>     `shutdown()` on Windows as it is on Linux. `server/run.sh`'s 3s watchdog
>     fired on CORRECT behaviour; Windows gets 15s against an 8s idle.
>     `closesocket()` would release it but reintroduces the recycled-fd hazard
>     `signal_shim.c`'s registry header rejects with measurements.
>   * MSYS2's `$!` is an MSYS pid, not the Windows pid the console API takes.
>
> NOT DONE: the EMFILE window cannot be forced on Windows (Python's `resource`
> is POSIX-only, no RLIMIT_NOFILE equivalent) and skips loudly. `make ci` on
> the Windows box has not been re-run since the ARM64 sweep of 2026-08-08.
