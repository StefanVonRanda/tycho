# Contributing to Tycho

Thanks for trying Tycho and wanting to help. **Tycho is 0.6 — pre-1.0** (see the
status note in the [README](README.md)): the language surface and the corelib API
can still change, so the most useful thing you can send is a **bug report, a
repro, or some design feedback** — much more than a big feature. Feedback on
ergonomics is worth more than usual right now: what 1.0 is waiting on is real
programs written by someone other than the author. Please don't be shy about
filing an issue.

## Reporting bugs and giving feedback

- **Found a miscompile, crash, or wrong output?** Open a
  [bug report](.github/ISSUE_TEMPLATE/bug_report.md). The single most useful
  thing is a **small `.ty` program that reproduces it**, plus what you expected
  vs. what happened, and your OS.
- **Have an idea, a rough edge, or a "why does it work this way?"** Open an
  [idea / feedback](.github/ISSUE_TEMPLATE/idea.md) issue. Even "I bounced off
  X" tells me something useful.
- A miscompile that a fixture in `tests/` would have caught is gold — it shows
  me where the fuzzer and suite have a blind spot.

## Building and running

All you need is a C compiler (`cc`) and `make` — the transpiler is a single
dependency-free C file. See the README's [Trying it](README.md#trying-it).

```
make                 # build ./tychoc
./tychoc f.ty && ./f  # compile + run a program
```

## The local CI gate (run it before a PR)

**Tycho has no cloud CI — that's on purpose.** There are no GitHub Actions; the
gate is `scripts/ci.sh`, run locally:

```
make ci              # build · test · ilp32 · asan-self · corelib + examples ·
                     # concurrency · FFI · the three fuzz lanes · tooling ·
                     # perf guard · recursion · spec-check · link+citation check
make ci N=0          # same, skipping the (slow) fuzz lanes for a quick check
```

A change is "green" iff `make ci` passes.

**Run `make hooks` once after you clone.** It points `core.hooksPath` at
`.githooks/`, so `git push` runs the citation/link gate and a fuzz smoke, and
blocks the push if either fails. That setting is **per-clone git config and is
not tracked**, so a fresh clone has no hooks at all until you run it — which is
exactly how a red citation gate reached `main` on 2026-08-10.

**The hook does not run the sweep**, deliberately: it holds only checks that are
too cheap to argue with. `make ci` before a substantial push is yours to run,
and it is the only thing that covers the wide lanes — a dogfood link break or a
cross-package mangling divergence is invisible to `make test`.

The sweep is not as long as it looks: **`make ci` measured 495–499s across four
runs on a 16-core box, 2026-08-10** — all thirteen steps including 200 fuzz
seeds. `make ci N=0`, which skips the fuzz lanes, is **274s**. It parallelises (`run_lanes`
forks each lane group), which is why the whole sweep costs barely more than
`make test` alone.

So do not run the lanes one at a time hoping to save time — the sum is far
slower than the sweep. Run a single lane when you are ITERATING on one failure
and want its verdict in seconds; run `make ci` to cover the tree. If something
is watching its output, redirect to a log (`make ci > ci.log 2>&1`) rather than
piping it — a pipeline dies with whatever is reading it and you lose the partial
result.

The one gate to never skip is the cheapest:

```
make check-links     # relative links, every path:line citation, every commit hash — ~1s
```

`path:line` refs are load-bearing in this tree and drift on any insertion into a
cited file. `scripts/reanchor_citations.py` remaps them mechanically after you
move lines yourself — read its header first, it is the wrong tool when only some
refs are stale.

**A commit hash is a citation too, and the gate resolves it.** Write it
backticked at git's default seven characters (`` `e96d6fc` ``) or introduced by
the word `commit` (`commit e96d6fc`); either form must name a commit in this
repository. A wrong-but-plausible hash — one transposition away from the one you
meant — is exactly what this catches. Two things follow:

- **Do not backtick a bare digest.** A checksum, a CRC, a hash of program
  output: give it a label (`sha=cbf43926`) rather than lone backticks. Seven
  characters is the only width checked, so the even-width digests (CRC32 8,
  md5 32, sha256 64) are already safe, but the rule keeps it that way.
- An 8-to-12 character hash in bare backticks is **not** checked. Write
  `commit <hash>` if you want it verified at that width.

On a shallow clone the hash check skips itself loudly and passes: there is no
history to resolve against, and that is the checkout's doing, not the tree's.

### Which gate for which change

`make ci` covers everything and is the honest default. When you are iterating
and want a verdict in seconds instead of minutes, this is the narrowest gate
that can actually fail for what you touched. The **"cannot redden for"** notes
are the important column: several changes are invisible to the gate you would
reach for first.

| You changed | Run | Notes |
|---|---|---|
| Markdown, comments, a `path:line` citation, a commit hash in prose | `make check-links` | ~1s. Nothing else can tell you more — none of it reaches a compiled artifact |
| a fixture under `docs/spec/`, or moved a `tests/` directory | `sh scripts/spec_check.sh` | ~6s. Also checks Appendix A against §3/§4, and that every `tests/…` path in Appendix E resolves |
| added a `run.sh`, or recorded a new golden | `make goldens-check` | ~0.1s. Asserts every golden a runner names is **tracked by git**. `.gitignore` ignores `*.out` broadly, so a new golden is green on your disk and absent from a fresh clone — `make test` reads the copy that exists and cannot redden for it |
| `src/tychoc.c`, `runtime/tycho_rt.c`, or any `.ty` fixture | `make test` | ~8 min |
| anything under `corelib/` | `make corelib` | ~49s. **`make test` cannot redden for it** — `tests/run.sh` globs the top level only and never descends into `corelib/`. Add `make corelib-examples` (~44s) if the package has a worked example. A package whose external dependency is absent (no `libpng-dev`, say) is SKIPPED rather than failed, and the verdict line names it: `N ok, M SKIPPED -- image(missing: libpng)`. Only `all green` means everything ran. `make corelib-examples` skips and reports the same way |
| a corelib change that ADDS, RENAMES or RETYPES a symbol | also `sh scripts/entrypoints.sh` | ~0.15s. **Neither corelib lane can redden for this.** A new symbol changes the compiler's global state, and that can break an unrelated **consumer** program: `9f601a6` changed only `corelib/`, was gated exactly as the row above says, and still shipped a red `make ci` — two extra `core:io` entries exposed a latent compiler bug that stopped `tools/tycho-vm/main.ty` compiling. `make corelib` builds `corelib/test/<pkg>/main.ty`; `make corelib-examples` builds `examples/corelib/**`; neither compiles anything under `tools/`. This lane compiles every entry point in the tree — `examples/`, `server/` and `tools/` — so it is the cheap consumer check |
| a corelib `<pkg>_shim.c` | `make shim-check` | <1s. **`make corelib` cannot redden for it**: the real build appends the shim with no `-std`, so a missing feature-test macro compiles there and fails only here |
| how a float is read or written as text | `make locale-check` | ~1.5s. **`make test` cannot redden for the compiler sites** — it runs in the `"C"` locale, and an `LC_ALL=` prefix is inert because a C program stays in `"C"` until something calls `setlocale`. This lane forces it with an `LD_PRELOAD` constructor |
| `tools/tycho-ar/` · `-q/` · `-vm/` · `-kv/` · `-scheme/` | `make ar-check` · `q-check` · `vm-check` · `kv-check` · `scheme-check` | 1–4s each, and **each is the only lane that runs its tool** |
| `tools/tycho-db/` — `sql/`, `store/`, `exec/`, `wal/`, `plan/` or `srv/` | `make db-check` | ~13.4s, and **the only lane that runs the database**. Two fresh runs of `demo.sql` must give a cmp-identical transcript, store file *and* log; four processes prove a row survives a process exit and that a reopened store still takes writes; a real `kill -9` mid-script must replay to every completed row and no partial one, idempotently, discarding a torn trailing record; the equality index and the scan must return identical rows over every key while examining 1 row against 6, and a constant-false `WHERE` must examine 0 of 6; a real server must answer over TCP on a kernel-chosen port and survive a client that hangs up mid-statement; all twenty-five `store.StoreErr` / `exec.ExecErr` / `wal.WalErr` / `plan.PlanErr` / `srv.SrvErr` variants must exit non-zero with their own whole message. `RECORD=1 sh tools/tycho-db/run.sh` re-records the golden — it cannot bless a lost row, because the persistence and crash rows are literals in the runner |
| `tools/tycho-flow/` — `stage/` or `main.ty` | `make flow-check` | ~11.1s, and **the only lane that runs the pipeline engine**. Its subject is concurrency, so the golden is the weakest of five legs: the transcript must be byte-identical over 8 runs and at `TYCHO_THREADS=1` and `2`; `--race 200` must find the pool draining out of source order at least 190 times (and 25 runs at one thread exactly 0, the negative control) or the determinism above is proving a sequential program deterministic; the bounded ring's three witness lines are asserted against literals in the runner, not the golden, so `RECORD=1 sh tools/tycho-flow/run.sh` cannot bless a channel that stopped being bounded; all three `stage.FlowErr` variants exit non-zero with their own whole message through a probe built against a copy of `stage/`, and the variant list is read out of the enum; and the whole demo plus 15 more pipelines run under **TSan**, which fails the lane for any race except the known interned-string-literal one (`plan.md`) |
| `server/`, or the `core:net` accept/recv/send path | `make server-check` | ~7s, starts the server for real |
| `examples/weblog/`, `examples/webserver/` | `make weblog webserver` | ~4s. **The only lanes that run either program** — `entrypoints` compiles them and asserts nothing |
| a `bench/` benchmark, or a language change that could break one | `sh scripts/entrypoints.sh` | ~0.22s. **The only lane that compiles anything under `bench/`.** `bench/guard.sh` checks one wall-time ratio and nothing else, so before 2026-08-11 the ~51 benchmarks could stop compiling in silence. Compile-only (`--emit-c`) — it never runs a benchmark, so it stays milliseconds. `make bench` depends on it |
| `tools/tychofmt.ty`, `tools/lsp.ty` | `sh scripts/tools_check.sh` | ~1 min |

`make test-fast` runs the same fixtures over a worker pool and is much quicker —
**use it to iterate, not as your gate.** It is compiled by the compiler it tests,
so a single `tychoc` regression can land inside the judge and turn every verdict
green at once. `tests/run.sh` scores with `cmp`, `grep` and `test`, which nothing
in this repo can break; when the two disagree, it is right by definition.

## Two rules that will surprise you

1. **There is one maintained compiler, and `compiler/tychoc0.ty` is not it.**
   `src/tychoc.c` (`tychoc`) is the reference transpiler; the
   [spec](docs/spec/) is normative. The tree also holds `compiler/tychoc0.ty`, a
   transpiler for Tycho written in Tycho — the artifact that proved self-hosting
   (compiled by itself, it reproduced its own emitted C byte-for-byte).

   **It was frozen on 2026-07-26.** Do not update it, do not mirror a language
   change into it, and do not read it to learn how Tycho behaves — no gate builds
   or runs it, so it compiles the language as it stood on the freeze date and
   already diverges from `tychoc` (which now rejects `fn handle(...)`, a reserved
   word as a procedure name, where `tychoc0` still accepts it). Until that date the
   opposite rule applied: every feature had to land in both, enforced by
   `make fixpoint` and the accept/reject parity lanes. Those gates are gone.

2. **The arena memory model is the whole point.** Value semantics + implicit
   per-scope arenas (no GC, no manual `free`) is the thesis
   ([docs/thesis.md](docs/thesis.md)). Changes that quietly break the in-place
   optimizations (string append, the map accumulator, move-on-last-use) turn an
   O(n) idiom into O(n²), and `make bench` / `bench/` guard against that. When in
   doubt, read [docs/guides/memory-model.md](docs/guides/memory-model.md).

## Where feature work is useful

The language surface is **feature-complete but not frozen** — value semantics,
implicit arenas, concurrency, generics, closures, UFCS, FFI, and the `sink`
consuming convention are all in. Pre-1.0 means they can still change; what a
freeze is waiting on is in
[ROADMAP.md](ROADMAP.md#what-1-0-requires). So the feature work I find useful
now is **ergonomics polish, not new pillars**:

- **User-defined projections** — yielding subscripts that generalize the built-in
  `&m[k]` (zero-copy views into part of a value). This is the one
  limited-reference idea that fits the arena + deep-copy-thread-boundary model;
  see [docs/rfc/limited-references-spike.md](docs/rfc/limited-references-spike.md).
  Low priority, scope it if a real need appears.
- **Small rough edges** real use turns up — clearer diagnostics (e.g. a
  keyword-used-as-variable message), FFI read-once-borrow docs, corelib gaps.

Also out of scope **by decision** (please don't propose them): a
ternary/conditional expression, a package manager, user-defined traits /
type-classes, Swift-style reference-counted copy-on-write, and **shared-mutable /
`remote-parts`-style references** for graphs — resolved against the model; store
graph-shaped data as an index pool (see
[docs/rfc/limited-references-spike.md](docs/rfc/limited-references-spike.md) and
[docs/internals/value-semantics-limits.md](docs/internals/value-semantics-limits.md)).
Generics, on the other hand, *are* supported — `$T`, see
[docs/guides/generics.md](docs/guides/generics.md).

## Code style

- Match the surrounding code — its comment density, naming, and idioms.
- C in `src/`/`runtime/` follows the existing C89/C11-ish style; Tycho in
  `compiler/`/`corelib/` follows the existing Tycho style (run `tycho fmt` and
  `make tools-check`).
- One focused change per commit; the commit message says **what was wrong** and
  **how the fix was verified** (which test / gate / fuzz run).
- New behavior gets a regression test under `tests/` (or a `corelib/test/`
  fixture) with a recorded golden, so it can't silently regress.

## Submitting

Open a pull request against `main`. Confirm `make ci` is green locally and say so
in the PR. Small, well-scoped PRs with a test are the easiest for me to accept.

By contributing, you agree your contributions are licensed under the project's
[MIT License](LICENSE).
