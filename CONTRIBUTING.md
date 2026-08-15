# Contributing to Tycho

Thanks for trying Tycho and wanting to help. **Tycho is 0.7 — pre-1.0** (see the
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

**A change is "green" iff the lanes that can redden for it pass.** Pick them from
the table below — it maps each part of the tree to the one lane that covers it.
Running `make ci` instead is not a stronger answer, it is the same answer after
nine minutes, and the table exists so you do not have to spend them.

**Run `make hooks` once after you clone.** It points `core.hooksPath` at
`.githooks/`, so `git push` runs the citation/link gate and a fuzz smoke, and
blocks the push if either fails. That setting is **per-clone git config and is
not tracked**, so a fresh clone has no hooks at all until you run it — which is
exactly how a red citation gate reached `main` on 2026-08-10.

**The hook does not run the sweep**, deliberately: it holds only checks that are
too cheap to argue with. `make ci` before a substantial push is yours to run,
and it is the only thing that covers the wide lanes — a dogfood link break or a
cross-package mangling divergence is invisible to `make test`.

**`make ci` measured 626 / 620 s over two runs on a 16-core box, 2026-08-15**
(the sweep prints its own duration on its last line). It has grown, not shrunk:
548 / 551 / 563 s on 2026-08-14 and 495 s on 2026-08-10, even though `make test`
fell from ~8 min to 113 s in that window — new lanes more than absorbed the
saving, and there are 77 steps now, including 200 fuzz seeds. `make ci N=0`, which skips
the fuzz lanes, is **~380s** (383 / 379 / 381 s, measured 2026-08-15; it was 274s
on 2026-08-10). It parallelises (`run_lanes` forks
each lane group), though the sweep no longer costs "barely more than `make test`"
— 620 s against 113 s is 5.5x, because `make test` stopped being the long pole.

**Two different things get confused here.** Running EVERY lane one at a time is
slower than the sweep — the sum beats the parallel whole, so never do that. Running
the ONE lane that can redden for your change is seconds against nine minutes, and
that is what the table below is for. Reach for the sweep when you cannot name a
lane that covers what you touched, or when you want the three things no lane
gives cheaply: the fuzz lanes, `ilp32`, and `asan-self`.

If something is watching its output, redirect to a log (`make ci > ci.log 2>&1`)
rather than piping it — a pipeline dies with whatever is reading it and you lose
the partial result.

The one gate to never skip is the cheapest:

```
make check-links     # relative links, docs/ reachability, every path:line citation, every commit hash — ~1s
```

`path:line` refs are load-bearing in this tree and drift on any insertion into a
cited file. `scripts/reanchor_citations.py` remaps them mechanically after you
move lines yourself — read its header first, it is the wrong tool when only some
refs are stale.

It diffs your working tree against `HEAD`, so **run it before you commit**. If
you have already committed and the push is blocked, give it the commit you
started from: `python3 scripts/reanchor_citations.py --ref <base> --apply`.
Without that it compares your change against itself, reports `no line moved;
nothing to re-anchor`, and leaves you stuck.

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
| Markdown, comments, a `path:line` citation, a commit hash in prose | `make check-links` | ~2.7s (measured 2026-08-16; check_citations alone is 2.05 s, up from 1.4 s when it started resolving backticked repo paths). Also fails a NEW document under `docs/` that no index links to, and any backticked path like `docs/guides/ffi.md` that is not a tracked file — from the repo root or from the citing document's own directory. Nothing else can tell you more — none of it reaches a compiled artifact |
| a fixture under `docs/spec/`, or moved a `tests/` directory | `sh scripts/spec_check.sh` | ~9.6s (measured 2026-08-15; was ~6 s). Also checks Appendix A against §3/§4, and that every `tests/…` path in Appendix E resolves |
| added a `run.sh`, or recorded a new golden | `make goldens-check` | ~0.08s (measured 2026-08-15). Asserts every golden a runner names is **tracked by git**. `.gitignore` ignores `*.out` broadly, so a new golden is green on your disk and absent from a fresh clone — `make test` reads the copy that exists and cannot redden for it |
| `src/tychoc.c`, `runtime/tycho_rt.c`, or any `.ty` fixture | `make test` | **~113s** (measured 2026-08-15, 719 fixtures, 16 cores). It parallelises — `TYCHO_THREADS=1` takes 725s, so narrow it only when you need the sequential ordering. This row said ~8 min until 2026-08-15, from a 2026-07-31 measurement taken before the runner was parallelised |
| anything under `corelib/` | `make corelib` | ~74s (measured 2026-08-15; was 49 s on 2026-08-01). **`make test` cannot redden for it** — `tests/run.sh` globs the top level only and never descends into `corelib/`. Add `make corelib-examples` (~57s) if the package has a worked example. A package whose external dependency is absent (no `libpng-dev`, say) is SKIPPED rather than failed, and the verdict line names it: `N ok, M SKIPPED -- image(missing: libpng)`. Only `all green` means everything ran. `make corelib-examples` skips and reports the same way |
| a corelib change that ADDS, RENAMES or RETYPES a symbol | also `sh scripts/entrypoints.sh` | ~0.5s over 92 entry points (measured 2026-08-15). **Neither corelib lane can redden for this.** A new symbol changes the compiler's global state, and that can break an unrelated **consumer** program: `9f601a6` changed only `corelib/`, was gated exactly as the row above says, and still shipped a red `make ci` — two extra `core:io` entries exposed a latent compiler bug that stopped `tools/tycho-vm/main.ty` compiling. `make corelib` builds `corelib/test/<pkg>/main.ty`; `make corelib-examples` builds `examples/corelib/**`; neither compiles anything under `tools/`. This lane compiles every entry point in the tree — `examples/`, `server/` and `tools/` — so it is the cheap consumer check |
| `corelib/crypto/crypto_shim.c` | also `sh scripts/crypto_hygiene.sh` | ~1.4s (measured 2026-08-15). **`make corelib` cannot redden for it.** That lane checks the ANSWERS — a ciphertext decrypts, a signature verifies — and every one of those passes whether or not the plaintext is still sitting in a freed heap block, because hygiene produces no output. `-Wl,--wrap=free` interposes only the shim's own frees, so a hit names that file and not libcrypto. Two controls run first: a dirty block that must be FOUND, and a cleansed one that must NOT be. Also asserts the key-import hex decode never branches on the secret — under valgrind, not a stopwatch — and that the branch-free rewrite classifies all 256 byte values exactly as the branching original did |
| `corelib/tls/` | also `make tls-verify` | ~1.5s (measured 2026-08-15). The existing tls test connects to a closed port, so it cannot tell a refused certificate from a refused connection — turning verification off passed every gate. This runs a real TLS server on the loopback with an untrusted cert and asserts three outcomes that must disagree: untrusted refused, the same server accepted once its CA is trusted and the name matches, refused again when the name does not. The middle one is what stops the other two passing on a dead server |
| `server/main.ty@resolve`, `corelib/path/` | also `make traversal-check` | ~16.5s (measured 2026-08-15). `server/run.sh` proves a traversal is **refused**; it cannot say by **which** guard. `resolve` has two independent ones — `hidden_segment(path.clean(rel))` and `path.safe_join(root, rel)` — and either alone refuses every payload, so **deleting one changes no observable behaviour** and every gate stays green while the defence halves. This defeats them one at a time in a copy of the server and requires the refusal to hold; the control defeats both and must see a canary outside the root actually leak |
| `corelib/math/`, `corelib/fmath/` | also `make math-diff` | ~2.2s (measured 2026-08-15). Differential against Python. `make corelib` compares this package to a golden **this repo recorded**, which agrees with whatever the code did the day it was written — and both defects the package has had were found by a human reading a claim, not by a gate. Where the semantics differ the oracle encodes the documented tycho answer and says so: `gcd(min, 0)` is 2^63 and does not fit, so it follows `abs()` in returning min; an `ipow` whose true answer leaves int64 is skipped by design, with the skip count printed. **The float arm is the load-bearing half** — the int-only first version scored 1197 clean answers while the sign-of-infinity defect sat in front of it, because `min`/`max`/`clamp`/`sign` are generic and their int instantiation says nothing about their float one. Each arm is proved able to redden on its own |
| `corelib/http/` | also `make http-verify` | ~3.1s (measured 2026-08-15). The HTTPS client every program reaches for, and until 2026-08-15 the one place a `CURLOPT_SSL_VERIFYPEER, 0L` left in after debugging would have passed every lane here. It needed a fix before it could have a gate: nothing could point `core:http` at a private CA, so no leg could be made to SUCCEED and "the untrusted server was refused" was indistinguishable from "nothing connected". The shim honours `SSL_CERT_FILE`/`SSL_CERT_DIR` now. Four outcomes that must disagree: untrusted refused, the same server accepted once its CA is trusted by **either** variable and reached by the cert's name, refused again under a name it does not carry — plus a control built against a copy with verification off, which must accept what the first leg refused |
| `corelib/csv/`, `corelib/json/`, `corelib/datetime/`, `corelib/sort/`, `strings.parse_float`, `path.safe_join`, `corelib/utf8/`, `corelib/regex/`, `corelib/cli/`, and the codecs (`sha256`, `md5`, `base64`, `hex`, `url`) | also `make format-diff` | ~19s (measured 2026-08-15; it has doubled from ~9 s as arms were added). Round-trips both against Python's own modules. `make corelib` compares them to goldens this repo wrote, which cannot say whether the format was ever readable by anyone else — that is how a row of one empty field silently lost its field |
| anything that must be memory-safe **on Windows** | `make wine-ubsan` | ~13 min, manual. The other wine lanes prove behaviour — a golden matches — and a golden matches fine when the program reads past an array. This rebuilds the emitted C, the shims and the runtime so undefined behaviour TRAPS, then runs the same corpus under wine. mingw-w64 ships no libasan or libubsan here, so the trap form is the only one that links. A control proves a deliberate out-of-bounds access really does die before the sweep is believed |
| `corelib/bignum/`, `corelib/decimal/` | also `sh scripts/bignum_diff.sh` | ~5s. Differentials against Python's integers and `Decimal`. `make corelib` compares against a golden this repo recorded, which proves the answers have not CHANGED — not that they were ever right. Decimal is compared on VALUE and SCALE, never on Python's chosen notation |
| a corelib `<pkg>_shim.c` | `make shim-check` | <1s. **`make corelib` cannot redden for it**: the real build appends the shim with no `-std`, so a missing feature-test macro compiles there and fails only here |
| how a float is read or written as text | `make locale-check` | ~1.5s. **`make test` cannot redden for the compiler sites** — it runs in the `"C"` locale, and an `LC_ALL=` prefix is inert because a C program stays in `"C"` until something calls `setlocale`. This lane forces it with an `LD_PRELOAD` constructor |
| `tools/tycho-ar/` · `-q/` · `-vm/` · `-kv/` · `-scheme/` | `make ar-check` · `q-check` · `vm-check` · `kv-check` · `scheme-check` | 1–4s each, and **each is the only lane that runs its tool** |
| `tools/tycho-db/` — `sql/`, `store/`, `exec/`, `wal/`, `plan/` or `srv/` | `make db-check` | ~13.4s, and **the only lane that runs the database**. Two fresh runs of `demo.sql` must give a cmp-identical transcript, store file *and* log; four processes prove a row survives a process exit and that a reopened store still takes writes; a real `kill -9` mid-script must replay to every completed row and no partial one, idempotently, discarding a torn trailing record; the equality index and the scan must return identical rows over every key while examining 1 row against 6, and a constant-false `WHERE` must examine 0 of 6; a real server must answer over TCP on a kernel-chosen port and survive a client that hangs up mid-statement; all twenty-five `store.StoreErr` / `exec.ExecErr` / `wal.WalErr` / `plan.PlanErr` / `srv.SrvErr` variants must exit non-zero with their own whole message. `RECORD=1 sh tools/tycho-db/run.sh` re-records the golden — it cannot bless a lost row, because the persistence and crash rows are literals in the runner |
| `tools/tycho-flow/` — `stage/`, `graph/` or `main.ty` | `make flow-check` | ~11.8s, and **the only lane that runs the pipeline engine**. Its subject is concurrency, so the golden is the weakest of six legs: the transcript must be byte-identical over 8 runs and at `TYCHO_THREADS=1` and `2`; `--race 200` must find the pool draining out of source order at least 190 times (and 25 runs at one thread exactly 0, the negative control) or the determinism above is proving a sequential program deterministic; the bounded ring's three witness lines are asserted against literals in the runner, not the golden, so `RECORD=1 sh tools/tycho-flow/run.sh` cannot bless a channel that stopped being bounded; all five `stage.FlowErr` variants exit non-zero with their own whole message through a probe built against a copy of `stage/` and `graph/`, the variant list is read out of the enum, and one more probe arm asserts that `graph.collect` still instantiates stage's generics across the package boundary; cancellation gets its own leg, because the transcript looks the same whether the first error stopped upstream production or was only reported at the end — `--cancel 25` must show the source producing under 64 of 256, with the never-fails control producing all 256 every run; and the whole demo plus 15 more pipelines run under **TSan**, where any warning at all fails the lane |
| `tools/tycho-ed/` — `buf/`, `main.ty` or `demo.ed` | `make ed-check` | ~3.9s, and **the only lane that runs the editor**. Its subject is UTF-8, which is the one thing a golden cannot see — a backspace that takes one byte off `é` still renders as plausible text — so the counts are asserted against literals in the runner and `RECORD=1 sh tools/tycho-ed/run.sh` cannot bless them: a backspace over a 2-byte codepoint takes the line 13 bytes → 11 and 11 codepoints → 10, a forward delete of a 3-byte one 19 → 16 and 13 → 12, and no dump may report `INVALID UTF-8`. The demo transcript must be byte-identical over two runs. Six edits are undone to an empty buffer and redone back to a byte-identical dump, cursor and journal depths included, on a script the runner writes — `demo.ed` never closes that loop. All seven `buf.BufErr` variants exit non-zero with their own whole message through a probe built against a copy of `buf/`, and the variant list is read out of the enum. `--stress` is a timing measurement and is not run here |
| `tools/tycho-sheet/` — `cell/`, `sheet/`, `main.ty` or `demo.sheet` | `make sheet-check` | ~14.0s, and **the only lane that runs the spreadsheet engine**. Its subject is float text, which a golden cannot see — a renderer that drops a digit and a golden re-recorded from it agree with each other — so the round trip is computed in the runner and `RECORD=1 sh tools/tycho-sheet/run.sh` cannot bless it: 98411 generated values are rendered, parsed back and compared as doubles, and none may fail. `0.1+0.2`, 2^53, `DBL_MAX` and the min subnormal are each asserted separately, since a count of 98411 does not say which values were in it. Round-tripping is only half the contract: four values are pinned as **exact text**, because several decimals of the same length can read back as one double and only one of them is the nearest. A cycle must be *named* (`F1 -> F2 -> F3 -> F1`, and `G1 -> G1` for a self-reference), 10000- and 100000-deep chains must evaluate exactly, and four depth limits past them fail closed by name. 13 of the 14 `CellErr`/`ParseErr` variants exit non-zero with their own whole message through a probe built against a copy of `cell/` and `sheet/`; the 14th is the `#NUM!` arm nothing constructs, and that is asserted too. Both variant lists are read out of the enums, and every run is bounded by `timeout(1)` |
| `tools/tycho-sim/` — `world/`, `main.ty` or `run.sh` | `make sim-check` | ~3.3s, and **the only lane that runs the entity simulation** — also the only program in the tree that uses `soa` and `subscript` as an API rather than as a fixture. Its subject is swap-remove, which a golden cannot see: despawn moves the last entity down over the hole and pops, so forgetting to re-point the moved entity's slot leaves the pool length right — every count and every dense walk still reads correctly while one id starts addressing somebody else, and a golden re-recorded from that build agrees with it. So the survivor set is generated in the runner (entity `i` gets `hp = i*7+1`, the odd ones are despawned, the survivors are that expression over the even `i`) and compared as a whole block *separately from the live count*, which is computed from `N`. The two redden independently. A stale id must be refused **by the generation**, with the reuse of the slot asserted alongside so the refusal is not just an empty slot. All three `world.SimErr` variants exit non-zero with their own whole message through a probe built against a copy of `world/`, and the variant list is read out of the enum |
| `tools/tycho-make/` — `graph/`, `build/`, `main.ty`, `demo.mk`, `build.mk` or `run.sh` | `make make-check` | ~3s, and **the only lane that runs the build tool**, graph and executor both. Its subject is a topological order, which a golden cannot see: drop an edge in the parser and the result is still an order — the same nodes, each exactly once, in a sequence that looks entirely plausible — and a golden re-recorded from that build agrees with it byte for byte. The only thing that moved is a constraint nobody printed. So the order is checked three ways `RECORD=1` cannot reach: against literals in the runner over a rulefile built so **declaration order and alphabetical order disagree** (which pins the tie-break *rule*, not just one answer); against the edges the program itself printed, computed in the runner (survives a rulefile change, but is blind to a dropped edge by construction); and against a second rulefile exactly **one edge apart** from the first, which is the leg a parser ignoring dependencies entirely would fail. A cycle must be **named** (`a -> c -> b -> a`, the self-edge `a -> a`) and named as the *loop* — one fixture puts two innocent nodes behind a cycle and the message may not mention them. Every run is bounded by `timeout(1)`: a cycle detector that recursed forever is what half these legs exist to catch. All 8 `graph.MakeErr` variants exit non-zero with their own whole message and an empty stdout, and the variant list is read out of the enum. The executor half repeats the trick one layer up: the build log is reassembled into topological order, so the DAG cannot be read off it — each recipe appends its name to a `trace` file and specific **pairs** are asserted there, never the whole list, because three rules sit at one depth and race. Staleness is asserted against literals: a no-op rebuild runs **zero** rules, changing one input reruns exactly its two dependents, and moving a file's **mtime** with its bytes intact reruns **nothing** — the only leg that can tell a content hash from a stat. The log is byte-identical over two runs at each of `TYCHO_THREADS` 1, 2 and 8, compared over a *sequence* rather than a cold build, since a cold build's outcomes are all the same shape and a misfiled one is invisible in it. All 6 `build.BuildErr` variants are accounted for |
| `tools/tycho-diff/` — `diff/`, `main.ty` or `run.sh` | `make diff-check` | ~2s, and **the only lane that runs the differ**. Its subject is an EDIT SCRIPT, which a golden cannot judge: two different scripts can both be minimal, because Myers does not specify a tie-break — GNU diff picks another one on ~18% of random inputs while being just as correct, so a byte comparison against it reports failures that are not. The golden therefore pins only the RENDERING, and the two properties that define correctness are computed in the runner where `RECORD=1` cannot reach them: the Keep+Del steps must rebuild the OLD file exactly and Keep+Ins the NEW one, and the number of edit steps must equal GNU diff's edit distance on the same input. The two legs catch different things — reconstruction alone passes a script that emits every line as delete-plus-insert, which is perfectly reversible and useless, and minimality alone passes a script with the right COUNT of edits on the wrong lines. Both run over 206 pairs including the cases a generator almost never produces: two empty files, one empty, identical, and a reversal. The `diff(1)` exit contract (0 same, 1 different, 2 error) is asserted on all three outcomes and on five error paths, each of which must also leave **stdout empty** — a differ that reports a failure on stdout corrupts the patch it is piped into |
| `tools/tycho-hash/` — `main.ty` or `run.sh` | `make hash-check` | ~3s, and **the only lane that runs the parallel tree hasher**. Its subject is a report that must not depend on the pool WIDTH, which no transcript can see: a golden recorded from a build whose workers happened to serialize is byte-identical to one whose workers raced. So the report is compared across **1, 2, 3, 5 and 8 workers**, and — because that alone also passes when one worker does everything — the per-worker split is read back: at width 8 every worker must take at least one file, and at width 1 the first must take **all** of them and the rest none, which is the negative control for `--workers` itself. The first version of the program ignored that option entirely and its determinism check compared eight workers against eight, five times, and passed. Correctness is against an **independent implementation**, `sha256sum(1)`, not against the program's own earlier output, plus the empty file's known `e3b0c442…` digest as a leg that fails even if the comparison loop silently ran zero times. The per-worker counts must sum to **exactly** the file count at every width — a dropped job and a job done twice both leave a plausible-looking report. All three legs were confirmed able to fail |
| `tools/tycho-snap/` | `make snap-check` | ~2s. The only lane that runs the snapshot tool, and the only one that hands its output to a foreign reader (python3 `zipfile`) rather than trusting our own CRCs |
| `tools/tycho-tally/` | `make tally-check` | ~3s. The only lane that runs `core:sqlite` or `core:testing`, and the only one whose control BREAKS an assertion in a copy to prove the test framework can fail |
| `tools/tycho-agg/` | `make agg-check` | ~3s. The only lane that asserts a generic INSTANTIATION reached codegen -- it greps the emitted C for the mangled `pipe__fn__type` symbols, which no other run.sh does |
| `tools/tycho-tmpl/` | `make tmpl-check` | ~3s. The only program using `sink`, and the only lane that asserts a consume rule still REFUSES four shapes |
| `tools/tycho-stat/` | `make stat-check` | ~2s. The only program using variadics or `zero$(T)`. Checks the ANSWERS -- count/sum/min/max/mean are recomputed in the runner, so re-recording cannot bless wrong arithmetic |
| `tools/tycho-ledger/` | `make ledger-check` | ~3.6s. The only program using newtypes across a package boundary. A newtype is erased at runtime, so the load-bearing leg is five probes that must each FAIL to compile -- and each must name an unmangled type |
| `tools/tycho-fh/` | `make fh-check` | ~2s. The only program declaring a `handle`. Proves the scope-exit destructor runs exactly once (a C-side counter, since no transcript shows it) and that six aliasing shapes stay refused -- two of them double-freed until 2026-08-14 |
| `tools/tycho-grid/` | `make grid-check` | ~5.6s. The second consumer of `subscript`, `bounded[N]T` and `# deprecated:`. Its load-bearing legs are compile-time: the deprecation warnings land on stderr, so no golden carries them, and the five subscript rules are probes that must fail |
| `server/`, or the `core:net` accept/recv/send path | `make server-check` | ~7s, starts the server for real |
| `examples/weblog/`, `examples/webserver/` | `make weblog webserver` | ~4s. **The only lanes that run either program** — `entrypoints` compiles them and asserts nothing |
| a `bench/` benchmark, or a language change that could break one | `sh scripts/entrypoints.sh` | ~0.22s. **The only lane that compiles anything under `bench/`.** `bench/guard.sh` checks one wall-time ratio and nothing else, so before 2026-08-11 the ~51 benchmarks could stop compiling in silence. Compile-only (`--emit-c`) — it never runs a benchmark, so it stays milliseconds. `make bench` depends on it |
| `tools/tychofmt.ty`, `tools/lsp.ty` | `sh scripts/tools_check.sh` | ~12s (measured 2026-08-15 — five times faster than the ~1 min this said) |

`make test-fast` runs the same fixtures over a worker pool. It used to be much
quicker; **as of 2026-08-15 it is not** — 104s against `make test`'s 113s on a
16-core box, because `tests/run.sh` parallelises too now. **Just run `make
test`.** The reason `test-fast` was never the gate is unchanged and is why the
nine seconds are not worth it: it is compiled by the compiler it tests,
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
