# Contributing to Tycho

**Tycho is 0.9.2 — pre-1.0** (see the status note in the [README](README.md)):
the language surface and the corelib API can still change, so the most useful
thing you can send is a **bug report, a repro, or design feedback** — much more
than a big feature. What 1.0 is waiting on is real programs written by someone
other than the author, so ergonomics feedback is especially welcome.

## Reporting bugs and giving feedback

- **Found a miscompile, crash, or wrong output?** Open a
  [bug report](.github/ISSUE_TEMPLATE/bug_report.md). The single most useful
  thing is a **small `.ty` program that reproduces it**, plus what you expected
  vs. what happened, and your OS.
- **Have an idea, a rough edge, or a "why does it work this way?"** Open an
  [idea / feedback](.github/ISSUE_TEMPLATE/idea.md) issue. Even "I bounced off
  X" tells me something useful.

## Building and running

All you need is a C compiler (`cc`) and `make`; see the README's
[Trying it](README.md#trying-it). `bootstrap/tycho_compiler.c` is a generated
cold-start artifact, not a second compiler.

```
make                 # bootstrap + build ./tychoc
./tychoc f.ty && ./f  # compile + run a program
```

After changing `compiler/`, regenerate the bootstrap and prove it still works:

```text
./tychoc1 --emit-c compiler/main.ty -o bootstrap/tycho_compiler
make bootstrap-check
```

If `make` fails immediately, install `cc` and `make` (Linux: your distro's
build tools; macOS: `xcode-select --install`; Windows: MSYS2 + mingw-w64 — see
[platforms](docs/platforms.md)).

### What the GATE needs, beyond that

**Run `make preflight` first.** It probes this machine for everything below and
names the missing package. It tests your box, not the tree; optional entries
only make a lane skip.

```
$ make preflight
required:
  asan + ubsan           MISSING -- make test fails ~335 fixtures at the LINK step
                           fedora: libasan libubsan
```

`cc` and `make` build and run Tycho. They are **not** enough to run the gate:

- **The sanitizer runtimes.** `make test` builds every fixture a second time
  under `-fsanitize=address,undefined`, and `make ci` adds a ThreadSanitizer
  lane. Without them the *link* fails, so hundreds of fixtures fail with
  `cannot find libasan.so.N` when nothing is wrong. On Fedora:
  `sudo dnf install libasan libubsan libtsan`; other distributions ship them
  with `gcc` or split them out. The check (must exit 0):

  ```
  echo 'int main(void){return 0;}' | cc -fsanitize=address,undefined -xc - -o /tmp/santest
  ```

  `make check-fast` needs them too: its fuzz smoke links an ASan/UBSan leg.
- **A static libc (Linux).** `tychoc1`, which `make test`, `make corelib` and
  `make ci` all depend on, links `-static-pie` through `TYCHOC1_CFLAGS`, and so
  do `tychofmt`, `tycho-lsp` and `tycho-debug` through `TOOL_CFLAGS`. On Fedora:
  `sudo dnf install glibc-static`. Overriding `TYCHOC1_CFLAGS` alone does not
  reach `TOOL_CFLAGS`, so `make tools-check` still fails. On Darwin there is no
  static link and the Makefile defaults both to non-static flags.
- **A 32-bit toolchain**, for the `ilp32` lane — it compiles the emitted C with
  `gcc -m32` to prove nothing assumes 64-bit `long`. The lane **refuses to skip**
  ("NOT skipping (RULE 4)"), so without it `make ci` fails rather than passing
  with a hole. On Fedora: `sudo dnf install glibc-devel.i686 libgcc.i686
  libatomic.i686`; elsewhere it is the `gcc-multilib` family. Check with
  `echo 'int main(void){return 0;}' | cc -m32 -xc - -o /tmp/m32`.

**`make ci` runs under gcc or clang.**

## The two gates: change and release

**Tycho has no cloud CI, on purpose.** There are two local gates:

```
make check-fast      # change gate (details under "The fast loop" below)
make test            # authoritative suite for compiler/runtime/bootstrap changes
make ci              # release gate: links · build · test · ilp32 · asan-self ·
                     # corelib + examples · concurrency · FFI · the three fuzz
                     # lanes · tooling · perf guard · recursion · spec-check
make ci N=0          # release gate without the fuzz lanes
```

`make check-fast` is the change gate: run it after every ordinary corelib,
runtime or docs change. For a compiler, emitter or bootstrap change, replace its
`test-fast` step with `make test` (see [the fast loop](#the-fast-loop) for why):

```
make test
make parse-check check-links corelib debug-check surface-check docs-counts-check
```

`make ci` is the release gate: nightly, before a merge, before a release, and
when no cheap lane covers the change. It costs tens of minutes and prints
per-lane wall time at the end; pay it once at the merge point, not per edit.
Fuzz depth follows the gate: `make fuzz-quick` is 20 seeds, `make ci` 200, and
`make ci-release` at least 1000 (it refuses a lower `N`).

`make ci` parallelises its lane groups. On a shared machine `CI_SERIAL=1 make ci`
serializes them without skipping lanes; `TYCHO_TEST_JOBS`, `DOCS_FENCES_JOBS`
and `FUZZ_JOBS` bound the fixture, doc-fence and fuzz workers. Redirect its
output to a log (`make ci > ci.log 2>&1`) rather than piping it — a pipeline
dies with its reader and you lose the partial result.

**Run `make hooks` once after you clone.** It points `core.hooksPath` at
`.githooks/`, so `git push` runs the cheap gates (scripts, links and citations,
version, doc counts, surface lock, runtime oracle, spec fast-path, fuzz smoke)
and blocks the push if one fails. The setting is per-clone and untracked, so a
fresh clone has no hooks until you run it. The hook does not run the sweep.

**A change is green iff the lanes that can redden for it pass.** Running the one
lane that covers your change is seconds; reach for `make ci` when you cannot
name one, or for the things no cheap lane gives: the fuzz lanes, `ilp32` and
`asan-self`.

**Docs-only change?** `make check-links` (~3s) is the whole gate.

`path:line` refs are load-bearing in this tree and drift on any insertion into a
cited file. `scripts/reanchor_citations.py` remaps them mechanically after you
move lines yourself — read its header first, it is the wrong tool when only some
refs are stale.

It diffs your working tree against `HEAD`, so **run it before you commit**. If
you have already committed and the push is blocked, give it the commit you
started from: `python3 scripts/reanchor_citations.py --ref <base> --apply`.
Without that it compares your change against itself, reports `no line moved;
nothing to re-anchor`, and leaves you stuck.

Running it twice is safe: each `--apply` records the target's bytes in
`$GIT_DIR/reanchor_citations.json` and the next run maps from there, so a
second run moves nothing. If the record goes stale (you reset, or hand-edited a
file it rewrote), `--apply` refuses; `--forget` drops the record and
`--ignore-state` maps from `<ref>` regardless. Given no target it re-anchors
every cited file your diff moved and names the changed files nothing cites.

**A commit hash is a citation too.** Written backticked at seven characters
(`` `e96d6fc` ``) or after the word `commit` (`commit e96d6fc`), it must name a
commit in this repository. So **do not backtick a bare digest** — label it
(`sha=cbf43926`). An 8-to-12 character hash in bare backticks is not checked;
write `commit <hash>` to have it verified. On a shallow clone the hash check
skips loudly.

### Which gate for which change

The narrowest lane that can actually fail for what you touched. Several changes
are invisible to the gate you would reach for first — those are called out.

| You changed | Run | Why this lane |
|---|---|---|
| Markdown, comments, a `path:line` citation, a commit hash in prose | `make check-links` | ~3s. Links, `docs/` reachability (a new page no index links to fails), every backticked tracked path, every citation and hash, plus `reanchor_citations.py --selfcheck` |
| an entry in `docs/internals/FRICTION.md` that claims to be closed | `make friction-check` | ~2 min. Runs each entry's `> Pinned-by:` command, deduped and in parallel, so a fix that stopped holding is named with its entry. An unpinned entry is reported, never a failure; `none -- <reason>` excuses one nothing can assert |
| a keyword, a builtin, or a corelib signature | `make surface-check` | <1s, also in the pre-push hook. Keywords and builtins are locked hard. Corelib may gain functions, but the gain must be recorded (`--record`) in the same commit or it fails as `UNRECORDED`; a removal or changed signature fails outright |
| `runtime/tycho_rt.c`, or a codegen arm that emits a `tycho:` trap | `make rtparity` | <1s, also in the pre-push hook. Checks the runtime surface — `getenv` knobs, every trap text, every arena-stats row — against a written oracle, both directions. `make test` cannot redden for it |
| adding or deleting any `.ty`, anywhere in the tree | `make corpus-check` | <1s. Recomputes the fixture runners' literal opening counts, so a deletion cannot leave them describing a tree that no longer exists |
| any `.py` or `.sh` in the tree | `make script-check` | <1s. Everything parses, and no statement is unreachable after a `return`/`break`/`continue` (what a deleted `if` header leaves behind) |
| a code snippet in any Markdown file | `make docs-fences` | ~3 min. Every fence that builds is also RUN with empty stdin and must exit 0; ` ```output ` blocks are compared; shell fences run with side effects stubbed. Each skip prints why |
| a code listing or install command on the site (`gh-pages:index.html`) | `make site-code-check` | ~6s. Compiles and runs every listing against the output printed beside it, requires release URLs to name the latest tag, and drives each install transcript on a real pty |
| a colour on the site (`gh-pages:index.html`) | `make contrast-check` | <1s. WCAG 1.4.3/1.4.11 over the palette, both themes, from a real render; a new token with no pair fails |
| a fixture under `docs/spec/`, or moved a `tests/` directory | `sh scripts/spec_check.sh` | Also checks Appendix A against §3/§4 and that every `tests/…` path in Appendix E resolves. `make spec-fast` is the sub-second subset the hook runs |
| added a `run.sh`, or recorded a new golden | `make goldens-check` | <1s. Every golden a runner names must be tracked: `.gitignore` ignores `*.out`, so a new golden is green on your disk and absent from a fresh clone |
| `runtime/tycho_rt.c`, or the embed rule in the `Makefile` | `make embed-check` | <1s. The runtime is escaped into one C string literal; this decodes it back byte for byte under every awk on `PATH` (mawk halves backslashes in `gsub`) |
| `compiler/emit/` or `runtime/` (anything that changes emitted C or its speed) | also `make bench-ab` | ~45s (+~35s once per base, cached in `build/ab/`). Interleaved A/B against `BASE=<rev>` (default `HEAD~1`, or `HEAD` if dirty) on this machine, so the ratio holds on any machine; fails a bench >5% slower beyond its own noise. The pre-push hook runs it against the remote sha |
| `compiler/`, `runtime/tycho_rt.c`, or any `.ty` fixture | `make test` | ~2 min parallel (`TYCHO_THREADS=1` is ~12 min) |
| anything under `corelib/` | `make corelib` | ~75s. **`make test` cannot redden for it** — `tests/run.sh` never descends into `corelib/`. Add `make corelib-examples` if the package has a worked example. A package whose external library is absent is SKIPPED and named; only `all green` means everything ran |
| a corelib change that ADDS, RENAMES or RETYPES a symbol | also `sh scripts/entrypoints.sh` | <1s. A new symbol changes global compiler state and can break an unrelated consumer; this compiles every entry point under `examples/`, `server/`, `tools/` and `bench/`, which neither corelib lane does |
| `corelib/crypto/crypto_shim.c` | also `sh scripts/crypto_hygiene.sh` | ~1.5s. Checks freed shim buffers were cleansed (`-Wl,--wrap=free`, with dirty/clean controls) and, under valgrind, that the key-import hex decode never branches on the secret |
| `corelib/tls/` | also `make tls-verify` | ~1.5s. A loopback TLS server with an untrusted cert: refused, accepted once its CA is trusted, refused again on a name mismatch |
| `server/main.ty@resolve`, `corelib/path/` | also `make traversal-check` | ~16s. `resolve` has two independent traversal guards and deleting one changes no observable behaviour; this defeats each in turn and requires the refusal to hold, with a both-defeated control that must leak |
| `corelib/math/`, `corelib/fmath/` | also `make math-diff` | ~2s. Differential against Python, int and float arms each proved able to redden; documented divergences (`gcd(min, 0)`) are encoded and say so |
| `corelib/http/` | also `make http-verify` | ~3s. Untrusted server refused, accepted via `SSL_CERT_FILE` or `SSL_CERT_DIR`, refused under a wrong name, plus a verification-off control that must accept |
| `corelib/csv/`, `json/`, `datetime/`, `sort/`, `utf8/`, `regex/`, `cli/`, `strings.parse_float`, `path.safe_join`, and the codecs (`sha256`, `md5`, `base64`, `hex`, `url`) | also `make format-diff` | ~19s. Round-trips against Python's own modules; a self-recorded golden cannot say whether anyone else can read the format |
| anything that must be memory-safe **on Windows** | `make wine-ubsan` | ~13 min, manual. Rebuilds with UB trapping (mingw-w64 ships no libasan/libubsan) and runs the corpus under wine, after a control proves an out-of-bounds access dies |
| `corelib/bignum/`, `corelib/decimal/` | also `sh scripts/bignum_diff.sh` | ~5s. Differentials against Python's integers and `Decimal`, on value and scale |
| a `packed struct`, or struct layout in either compiler | `make packed-check` | <1s. No golden can see layout; compares `sizeof` of the emitted definitions from both compilers, with the attribute stripped as the control |
| a `vector[N]T`, or its element-wise lowering in either compiler | `make vector-check` | ~14s. No golden can see the lowering; checks the GCC vector member and its 8-byte alignment pin in the emitted C and counts packed instructions at -O0/-O1 (where gcc's own auto-vectoriser is off), for both compilers |
| an `align(N) struct`, or the arena's alignment guarantee | `make align-probe` | ~2.5s. Allocates from a real `Arena` and checks addresses modulo N from both compilers; grounds the >8 refusal (the arena gives 16-byte alignment on 0 of 4096 allocations) and `_Static_assert`s every emitted user type at ≤8 |
| the CONTENTS of a release archive | `make release-content` | ~90s. `make release-check` compares bytes, not contents; this extracts both archives and checks layout, runtime identity, reported version, DLL imports, that every `.exe` starts under wine, and that a `core:strings` program builds from the archive alone. Skips loudly without wine or mingw |
| a corelib `<pkg>_shim.c` | `make shim-check` | <1s. Compiles each shim on its own with `-std`, where a missing feature-test macro fails; `make corelib` cannot redden for it |
| a corelib `<pkg>_shim.c` | `make shim-warn` | <1s. All shims at `-Wall -Wextra -Wdeprecated-declarations` against `scripts/shim.warn` (empty: any warning fails). `RECORD=1` re-records |
| the emitted C, a corelib shim, or `scripts/release.sh --mingw` | `make mingw-warn` | ~33s. The shipped `.exe` programs cross-compiled at the release cc line against `scripts/mingw.warn` (empty). Skips loudly with no mingw-w64 on PATH |
| the compiler's handling of source BYTES | `make source-bytes` | ~2s. A CRLF file and a NUL byte, written at run time because no fixture can carry them |
| `runtime/tycho_rt.c`, the driver's `cc` line, or any C a program links | `make glibc-check` | ~2s. The libc floor: no `__isoc23_*` symbol and nothing above `GLIBC_2.34` (Rocky 9) |
| how a float is read or written as text | `make locale-check` | ~1.5s. Forces a comma-decimal locale with an `LD_PRELOAD` constructor; `make test` runs in `"C"` and cannot redden for it |
| `corelib/image/` | `make image-ceiling` | ~2.5s. A PNG header can ask for gigabytes; this watches the decode ceiling fire while a real image still decodes |
| `corelib/crypto/`, `tls/`, `http/`, `image/` — any handle a caller holds | also `make handle-guard` | ~22s. These handles are a bare `ptr`, so the compiler cannot refuse a double free or use-after-free; this proves the runtime guard names both, with guard-removed and pristine controls |
| `corelib/zip/` | also `make snap-check` | ~2s. `tools/tycho-snap` is the only `core:zip` consumer; its archives are read back by python3 `zipfile` |
| `corelib/raster/` | also `make raytrace` | `examples/raytrace` under ASan, validating the QOI it writes |
| `server/`, or the `core:net` accept/recv/send path | `make server-check` | ~7s, starts the server for real |
| `examples/weblog/`, `examples/webserver/` | `make weblog webserver` | ~4s. The only lanes that run either program |
| `tools/tychofmt.ty`, `tools/lsp.ty` | `sh scripts/tools_check.sh` | ~12s |

Each program under `tools/` has its own lane, and it is the only one that runs
that program: `make <name>-check` for `tycho-<name>` (`ar`, `q`, `vm`, `kv`,
`scheme`, `db`, `flow`, `ed`, `sheet`, `sim`, `make`, `diff`, `hash`, `snap`,
`tally`, `agg`, `tmpl`, `stat`, `ledger`, `fh`, `grid`, `chess`, `kvsrv`, `sat`,
`build`, `debug`, `fold`), 1–30s each. Most assert their answers against
literals or an independent oracle in the runner — published perft totals,
`sha256sum(1)`, GNU diff's edit distance, python3 `zipfile` — so `RECORD=1`
re-recording a golden cannot bless a wrong answer. `debug-check` skips loudly
without `gdb`.

### The fast loop

`make check-fast` (~5 min) is the per-change loop:
`test-fast`, `parse-check`, `check-links`, `corelib`, `debug-check`,
`surface-check`, `docs-counts-check`, `fuzz-quick` (20 seeds).

Every recipe `make` runs sits in a cgroup capped at `MEMCAP_MB` (default
8192 MiB): a runaway compiler is killed there instead of OOM-killing the
caller's session. `make ci` gives each forked lane its own cgroup at
`MEMCAP_LANE_MB` (default: the same value). `MEMCAP_MB=0 make test` disables the cap. A host without a systemd
user manager runs uncapped; nothing else changes.

`test-fast` runs the same fixtures as `make test` over a worker pool, but the
pool is driven by `tools/prunner`, which is compiled by the compiler it tests.
A `tychoc` regression can therefore land inside the judge and turn every verdict
green at once. `tests/run.sh` scores with `cmp`, `grep` and `test`, which nothing
in this repo can break; when the two disagree, it is right by definition. So
changes to the compiler, emitter or bootstrap still get `make test`, and a merge
gets `make ci`.

## The rule that will surprise you

**The arena memory model is the whole point** ([docs/thesis.md](docs/thesis.md)).
Changes that quietly break the in-place optimizations (string append, the map
accumulator, move-on-last-use) turn an O(n) idiom into O(n²); `make bench`
guards against that. When in doubt, read [docs/memory-model.md](docs/memory-model.md).

## Where feature work is useful

The language surface is **feature-complete and locked** by `surface.lock` —
value semantics, implicit arenas, concurrency, generics, closures, UFCS, FFI and
the `sink` convention are all in. Pre-1.0 it can still change deliberately; what
1.0 waits on is in [ROADMAP.md](ROADMAP.md#what-10-requires). So the useful
feature work now is **ergonomics polish, not new pillars**:

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
[docs/reference/generics.md](docs/reference/generics.md).

## Code style

- Match the surrounding code — its comment density, naming, and idioms.
- C in `runtime/` and the corelib shims follows the existing C89/C11-ish style; Tycho in
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
