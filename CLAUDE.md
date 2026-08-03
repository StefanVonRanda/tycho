# Working in this repo

## Gate budget — read this before running anything

This tree has a lot of gates and some of them are very expensive. **Run the
cheapest gate that can actually redden for your change.** Running a broader one
"to be safe" is not caution, it is twenty minutes of someone else's day.

| Gate | Cost | Reddens for |
|---|---|---|
| `python3 scripts/check_citations.py` | <1s | any `path:line` written in Markdown, comments, or evidence blocks |
| `sh scripts/check_links.sh` | <1s | relative Markdown links |
| `sh scripts/spec_check.sh` | ~6s | runnable examples in `docs/spec/`, Appendix A vs §3/§4, **and every backticked `tests/…` path in Appendix E resolving to a real file** — so any commit that moves or deletes a fixture directory must run this, not just the two doc gates |
| `make goldens-check` | ~0.07s | any new `run.sh`, any change to how one names its golden, and **any newly recorded golden** — it asserts every golden a runner names is tracked by git. `.gitignore` ignores `*.out` broadly and un-ignores per directory, so a new lane's golden is green on your disk and absent from a fresh clone; `make test` reads the copy that exists and cannot redden for it |
| `make shim-check` | <1s | any corelib `<pkg>_shim.c` — compiles each one standalone under `-std=c11`. **`make corelib` cannot redden for this**: the real build appends a shim to the generated `.c` on one `cc` line with no `-std`, so a missing feature-test macro compiles there and only here |
| `make ar-check` | ~3s | `tools/tycho-ar/`, and any `core:compress`/`sha256`/`io`/`path` change that moves a digest, the walk order or the archive round trip — **the only lane that runs anything under `tools/tycho-ar/`** |
| `make q-check` | ~3.5s | `tools/tycho-q/`, and any `core:csv`/`core:json`/`core:decimal`/`core:sort` change that moves a header, a cell's classification, a decimal's scale or a sort order — **the only lane that runs anything under `tools/tycho-q/`** |
| `make locale-check` | ~1.5s (1.73 / 1.46 / 1.44 s, measured 2026-08-02) | any change to how a float is read or written as text — `src/tychoc.c@c_strtod`, `src/tychoc.c@c_dtoa`, `runtime/tycho_rt.c@tycho_float_to_str`, or the `".0"` guard beside any of them. It compiles **and** runs `tests/float_lit_locale.ty` and `tests/float_str_locale.ty` with the process locale genuinely comma-decimal, forced by an `LD_PRELOAD` constructor calling `setlocale`. **`make test` cannot redden for the two compiler sites**: it runs tychoc in the grader's locale, which is `"C"` — the case that never broke — and an `LC_ALL=` prefix is *inert*, because a C program stays in `"C"` until something calls `setlocale`. Skips loudly, exit 0, on a host with no comma-decimal locale |
| `make vm-check` | ~2.3s | `tools/tycho-vm/` — **the only lane that runs anything under `tools/tycho-vm/`**. Asserts asm determinism, the `dis`→`asm` byte round trip, the three programs' output and listings against a golden, trace determinism, and that all seven runtime traps and four malformed-source diagnostics exit non-zero with empty stdout |
| `make scheme-check` | ~1s | `tools/tycho-scheme/` — **the only lane that runs the Scheme interpreter**. Four programs vs a byte-identical golden on two runs (determinism), five error cases dying non-zero with empty stdout. The programs stay shallow on purpose: generated-code recursion segfaults past the C stack (no guard in emitted code) — recorded in `plan.md` |
| `make server-check` | ~7s | `server/main.ty`, `server/www/`, `server/run.sh`, and the `core:net` accept/recv/send path |
| `make weblog webserver` | ~4s (1.9 s + 2.1 s, three runs each, measured 2026-08-02) | `examples/weblog/`, `examples/webserver/` — including `content/`, whose rendered pages are half the golden — and any `core:datetime`/`core:strings`/`core:sort` or `core:markdown`/`core:httpd` change that moves a parsed timestamp, a bucket order or rendered HTML. **The only lanes that run either program**; `scripts/entrypoints.sh` compiles them and asserts nothing. Neither binds a socket, so neither is `server-check` |
| `make corelib-examples` | ~44s (43.7 s, measured 2026-08-01) | `examples/corelib/**`, and any corelib change with a worked example — it compiles and runs each one against its golden. `sh examples/corelib/run.sh` is the same work at the same cost (43.5 s in the same session) |
| `make corelib` | ~49s (49.4 s, measured 2026-08-01) | **any `corelib/` change.** Builds and runs every `corelib/test/<pkg>/main.ty` against `corelib/test/<pkg>.out`. **`make test` cannot redden for a corelib change** — see the rule below — so this is the gate, not a supplement to one |
| `sh scripts/tools_check.sh` | ~1 min | `tools/tychofmt.ty`, `tools/lsp.ty` |
| `sh scripts/asan_self.sh` | minutes | `src/tychoc.c` under ASan/UBSan over the whole corpus |
| `make test-fast` | ~1 min | the same 560 fixtures as `make test`, over a worker pool — **advisory, see below** |
| `make test` | **~8 min** (473 s, measured 2026-07-31) | compiler or runtime behaviour, any fixture or golden |
| `make ci` | **~19 min** | a new CI step, or a release |

### The rule

- **Markdown, comments, evidence blocks** → the two doc gates. Nothing else.
  They cannot affect a compiled artifact, so `make test` cannot tell you
  anything `check_citations.py` did not.
- **A `.ty` fixture** → `make test`.
- **A `corelib/` change** → `make corelib`, plus `make corelib-examples` if the
  package has a worked example, plus `make shim-check` if a `<pkg>_shim.c` moved.
  **Not `make test`, which cannot redden for it**: `tests/run.sh:113` globs
  `examples/*.ty tests/*.ty` at the top level and never descends, so no file
  under `corelib/` is in its corpus. This line used to read "a `.ty` fixture or a
  corelib change → `make test`", which sent every corelib change to an eight-minute
  gate that could not fail for it — the expensive kind of wrong, because it looks
  like caution. Run `make test` for a corelib change only when something *outside*
  `corelib/` changed too.
- **`src/tychoc.c`** → `make test`.
- **`make ci` runs once**, at the end of a chain of related work, or when a
  phase adds a CI step. Not per phase. Not "to confirm". Once.
- If you are unsure which gate covers your change, that is a question to ask,
  not a reason to run the expensive one.

### `make test-fast` is the fast lane; `make test` is still the answer

`make test-fast` runs the identical 560 fixtures through `tools/prunner/main.ty`,
a Tycho program with a bounded worker pool: **473 s → 62 s, 7.6x**, and its report
is byte-identical to `tests/run.sh`'s over the whole corpus, unsorted
(the prunner plan). Both were measured on a
16-core box; width is
`ncpu()`, narrowable only by launching with `TYCHO_THREADS=N`.

Use it to iterate. **Do not use it as the gate**, and do not put it in
`scripts/ci.sh`. prunner is compiled by the compiler it tests, so one tychoc
regression — string comparison, `os.run`'s exit code, the `parallel for` fan-out —
lands inside the judge and can turn all 560 verdicts green at once. `tests/run.sh`
scores with `cmp`, `grep` and `test`, which no change in this repo can break, and
it is the only independent implementation left after the `tychoc0` differential
was retired (see "Two gates that used to be here"). When the two disagree,
`tests/run.sh` is right by definition; `./build/prunner --mode=seq` re-runs the
same jobs one at a time, which separates "the pool did it" from "the judge did
it". A FAIL from prunner prints the same parenthesised reason but not the log
dump — re-run `make test` for that.

### `make ci` is confirmation, not discovery — never debug with it

The expensive failure mode is not running `make ci` too often for no reason. It
is running it as a **feedback loop**: sweep, hit a red, fix it, sweep again, hit
the next red, fix it, sweep again. Each iteration costs nineteen minutes to learn
one thing that the individual gate would have reported in seconds. Observed on
this repo: four sweeps, seventy-six minutes, failing at `[4/13]`, then `[9b/13]`,
then `[12/13]` — three facts that `make conc`, `make editors-check` and
`scripts/spec_check.sh` together would have produced in under two minutes.

When `make ci` reddens, **do not re-run `make ci`.** Read which step failed, run
that step's own gate, fix, re-run *that gate*. Spend the full sweep once, at the
end, to confirm what you already believe.

| `make ci` step | run this instead while fixing |
|---|---|
| `[1b] goldens-check` | `make goldens-check` (~0.07s; it names the `run.sh:line` and the untracked file. The fix is a `.gitignore` un-ignore line for that lane's directory plus `git add`, not a re-record) |
| `[2] make test`, `[2c] asan-self` | `make test` (`sh scripts/asan_self.sh` for the ASan-specific case) |
| `[2b] ilp32` | `make ilp32` — **not `make test`**, which cannot redden for it. It rebuilds the same fixtures under `gcc -m32 -msse2 -mfpmath=sse`, so a red here is a 64-bit assumption in the emitted C, not a golden change. A fixture that passes at 64-bit and fails here is usually an `int` width or an FP-evaluation difference, and both are invisible to the 64-bit lane |
| `[2e] locale-check` | `make locale-check` (~1.5s; it `LD_PRELOAD`s a `setlocale` constructor and rebuilds the two locale fixtures under a comma-decimal `LC_ALL` — a red here is a float literal or a `str(float)` picking up the ambient decimal separator, and there is no `RECORD=1`: the goldens belong to `make test`, this lane only reads them harder) |
| `[3] corelib` and its dogfoods | `make corelib` / `make corelib-examples` / `make fetch` / `make weblog` / `make webserver` |
| `[3b] entrypoints` | `sh scripts/entrypoints.sh` |
| `[3c] server-check` | `make server-check` (~7s; it starts tycho-httpd for real — a red here is a behaviour change in `server/main.ty` or `core:net`, not a build break, which `[3b]` would have caught first) |
| `[3d] shim-check` | `make shim-check` |
| `[3e] ar-check` | `make ar-check` (~3s; it builds and runs `tycho-ar` over a fixture it writes itself — a red here is a digest, a walk order or a round-trip change, and `RECORD=1 sh tools/tycho-ar/run.sh` re-records the golden once you know why) |
| `[3f] q-check` | `make q-check` (~3.5s; it builds and runs `tycho-q` over fixtures it writes itself — a red here is a changed row, order, header or cell classification, and `RECORD=1 sh tools/tycho-q/run.sh` re-records the transcript once you know why) |
| `[3g] vm-check` | `make vm-check` (~2.3s; it builds and runs `tycho-vm` over the three `progs/*.tasm` plus fixtures it writes itself — a red here is a changed listing, program output, trace length or trap message, and `RECORD=1 sh tools/tycho-vm/run.sh` re-records the golden once you know why) |
| `[3h] scheme-check` | `make scheme-check` (~1s; it builds and runs `tycho-scheme` over the four `progs/*.scm` — a red here is a changed golden or a die-path regression, and `RECORD=1 sh tools/tycho-scheme/run.sh` re-records once you know why. Programs stay shallow: deep recursion segfaults, see `plan.md`) |
| `[4] conc` | `make conc` |
| `[5] ffi` | `make ffi` |
| `[6]/[7] fuzz` | `python3 fuzz/run.py <small N>` |
| `[9] tools-check` | `sh scripts/tools_check.sh` |
| `[9b] editors-check` | `make editors-check` |
| `[10] bench-guard` | `sh bench/guard.sh` |
| `[11] recursion` | `make recursion` |
| `[12] spec-check` | `sh scripts/spec_check.sh` |
| `[12b] docs-fences` | `make docs-fences` |
| doc gates | `python3 scripts/check_citations.py`, `sh scripts/check_links.sh` |

And if the *same* step reddens twice, stop patching and read the source that
governs it. Three different steps reddening in a row means the change is touching
more than its scope claims — say so and narrow it, rather than grinding the suite
until it goes quiet.

### Two gates that used to be here

`sh scripts/frontparity.sh` and `sh compiler/fixpoint.sh` were **retired on
2026-07-29** and are no longer runnable gates. Both built the frozen
`compiler/tychoc0.ty` and checked the live compiler against it; the breaking
loop-syntax change of that date means the frozen compiler can no longer parse
the corpus. The scripts are still on disk and their headers record what they
proved and what their loss costs — `ROADMAP.md` and `docs/architecture.md` carry
the same in prose. **Nothing replaces them**: a change that silently narrows what
`src/tychoc.c` accepts no longer has a second implementation to disagree with it.

### Why this file exists

A ten-phase plan in this repo spent most of its wall-clock waiting on `make ci`
runs that could not have failed — phases that edited only Markdown, each
re-running a nineteen-minute suite. The evidence is in
the friction plan and in `plan.md`'s "Gate ladder" section.
The gates are good; running all of them every time is not using them, it is
avoiding thinking about which one applies.

## Environment

`~/.zshenv` drops `LD_PRELOAD` when it is the tmux `block-nnp.so` shim. If you
ever see `ASan runtime does not come first in initial library list`, that shim
is back in the environment and **the tree is not at fault** — it scored 251/527
spurious failures before this was found. Re-run under `env -u LD_PRELOAD` and
say so rather than changing anything in the repo.

## Citations

`path:line` references are load-bearing here and `scripts/check_citations.py`
gates them: every `path:N` must name a real file and a line range inside it,
and every `@token` anchor must appear on exactly one line of that range. The
rules that bite:

- **A bare `:N` binds to the previously named path in the same document.** Name
  the path in evidence blocks, or the gate resolves your citation against
  whatever file you happened to mention last.
- **Repo-relative, never absolute.** `src/tychoc.c:402` — never that same path
  with the checkout's absolute prefix in front of it. An absolute path is a
  hard failure that names the relative form.
- **Inside a `> Provenance:` block, a single-line ref must be anchored
  `path:N@token`; a range stays bare, deliberately** — a range has no single
  subject token and forcing one produces a false anchor. Do not "fix" the
  exemption. A `> Provenance:` ref that inherits no path at all is a failure.
- **An anchor must name one line.** A token on more than one line of the cited
  range identifies none of them; that is a hard failure. Anchor a token that
  occurs once, tighten the range to its construct, or drop the anchor — a bare
  range is honest, a false anchor is not.
- **Citing a definition? Write `` `path@SYMBOL` ``, with no line number.** The
  gate checks only that the symbol still appears somewhere in that file, so the
  form survives insertions and reddens on a rename or deletion. Use it for a
  definition, or for a line whose identity is a distinctive token
  (`Makefile@SKIPPED`); a region has no name of its own and still wants
  `` `path:N-M` ``.
- **Never copy a figure the gate prints into prose.** If a command can produce
  the number today, name the command instead — the count is stale by the next
  commit. A one-time measurement that decided something stays, with its date.
- `compiler/tychoc0.ty` is exempt from the gate: frozen and unfixable.

## Plans

Substantial work runs through `plan.md`: one phase at a time, each phase
verified and committed on its own, evidence appended under the phase rather
than pasted into chat. Work discovered outside a phase's scope is appended to
`plan.md` as a new unchecked phase — never silently absorbed into the phase
that found it.

### `plan.md` rotates — never leave "`plan.md` phase N" behind

A completed plan is **deleted, not archived**. `plan.md` starts numbering at 1
again, so "`plan.md` phase N" written in a comment stops meaning anything the
moment the plan it was written under is gone. The old archives
(`docs/internals/plan-*-DONE.md`) were pruned on 2026-08-03; what they recorded
is still in git under the `docs-archive` tag if a decision record is ever
needed again. Name the feature or the date instead of a phase number.

### Writing a phase's brief

Whoever writes a phase — in `plan.md` or in the instructions handed to an agent —
sets that phase's gate cost. Most of the waste recorded in this file was
introduced there, not by the agent that obeyed it.

- **Name the specific gates the phase's own changes can redden, and say what NOT
  to run.** A brief that lists `make ci` as "the verification" invites it as the
  debugging loop. A phase editing only Markdown should be told, in words, not to
  run `make test`.
- **`make ci` belongs in a brief only when the phase adds or changes a CI step,
  or when it is the deliberate closing sweep of a finished chain.** Nowhere else.
- **State the expected count, not just the gate.** "`make test`, which was 541 at
  the previous phase" catches a silent loss that a bare "make test passes" does
  not. Several phases here caught real regressions purely because a number moved.
- **Sequence tooling before corpus.** A change to the language that rewrites many
  `.ty` files must teach `tools/` and `editors/` the new syntax *first* —
  `scripts/editors_check.sh` parses every file in the tree and compares against a
  known-bad set, so a corpus rewrite ahead of the grammar reddens it by
  construction. This cost a full sweep and a plan reordering on 2026-07-29.
- **Do not assert facts the phase should verify.** Briefs in this repo have
  confidently mis-stated where a token was lexed, which of two files was already
  fixed, how a golden's hash was computed, and how many call sites existed. Every
  one was caught by an agent reading the source. Write the claim as something to
  check, and it will be checked; write it as fact, and time is spent disproving
  it.
