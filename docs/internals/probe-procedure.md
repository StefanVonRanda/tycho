# Running an agent probe

**Rule: the probe must not read the compiler, the friction log, or a previous probe's findings. `ROADMAP.md` §1 wants programs written against `docs/` by someone who did not write the compiler. No second human reviewer was found, so the instrument is an LLM agent in a container.**

## Setup

```
git clone --depth=1 https://github.com/StefanVonRanda/tycho.git tycho-probe
cd tycho-probe
rm -rf runtime compiler docs/internals docs/rfc .git tests fuzz scripts bench
rm -f plan.md $(find . -iname '*FRICTION*')
```

The deletions are the point: the agent then **cannot** read the compiler source,
the friction log, or a previous probe's findings, so "do not read the compiler"
stops being an honour rule. `compiler/` matters most: a readable
`compiler/parse/parse.ty` is a syntax oracle for whatever feature is under test.
Verify with `test ! -e compiler && echo stripped`.

**The strip also guarantees one class of false finding, so discount it on sight.** `docs/internals`, `docs/rfc` and `tests` are gone, and the pages that survive link into all three, so every probe can truthfully report those links as dead. A link finding counts only if the target is missing from a full checkout.

**Build the compiler from `main`, not the release tarball**, or part of the report describes diagnostics already improved since. The strip removes `scripts/`, so the probe tree cannot build anything — build in the real checkout and copy the binary in:

```
make -C /path/to/tycho tychoc && cp /path/to/tycho/tychoc tycho-probe/tychoc
```

`corelib/` survives the strip and `tychoc` resolves `core:` from beside itself, so no `TYCHO_CORELIB` is needed. Check it with a program that imports one — `./tychoc` on a bare `fn main()` exercises neither the corelib path nor the package rules, and a probe that fails on its first `import "core:…"` reports setup as a language defect.

## Aim it at something untouched

A program over ground already covered returns little. The table is a **shape to re-derive**, not a gap list: when a feature lands, ask which of these it belongs to and whether anything outside this repo has driven it.

| surface | covered by |
|---|---|
| algorithms, strings, text | tycho-diff, tycho-fold, R5 `text`, R1 `core` |
| generics, enums, Option/Result | generics (`git show ba952872:docs/internals/probe-generics-2026-08-19.md`), R4 `gen2`, R5 `state` |
| newtypes, `subscript`, `bounded` | record (`git show ba952872:docs/internals/probe-newtype-subscript-bounded-2026-08-19.md`) |
| `select`, channels, concurrency | select (`git show ba952872:docs/internals/probe-select-2026-08-19.md`), tycho-hash, R3 `conc` |
| FFI, `handle`, `soa` | ffi (`git show ba952872:docs/internals/probe-ffi-2026-08-19.md`), handle (`git show ba952872:docs/internals/probe-handle-2026-09-21.md`), R3–R5 `ffi2`/`ffi3`/`ffi4` |
| value semantics, arenas | R1 `mem`, R3 `arena`, R7 `churn` |
| packages, libraries, visibility | R1 `pkg`, R8 `lib` |
| numerics, floats | R1 `num`, R6 `float` |
| sockets, files, argv | R6 `net`, R4 `io2` |
| unicode | R8 `uni` |
| layout and SIMD — `packed`, `align(N)`, `vector[N]T`, swizzling | simd (`git show ba952872:docs/internals/probe-simd-2026-09-07.md`), layout (`git show ba952872:docs/internals/probe-layout-2026-09-21.md`) |
| **the compiler itself** | R8 `cli`, `huge`, `msgs` |
| graphs as values, generational handles, exclusive update | graph (`git show ba952872:docs/internals/probe-graph-2026-10-18.md`), handles (`git show ba952872:docs/internals/probe-handles-2026-10-18.md`), update (`git show ba952872:docs/internals/probe-update-2026-10-18.md`) |

**Aiming at the compiler itself paid best**: probes of tychoc's CLI, its generated source and its diagnostics found real defects, where program-writing rounds mostly found doc gaps. When choosing where to aim, prefer a surface with no lane over one with no probe — `grep <feature> tests/run.sh` coming back empty is the strongest signal available.

## The brief

Name the features the program MUST use, let the agent pick the program, and ask for the log to record what went RIGHT as well as what did not — a log that only complains is not evidence. Do not hand over `~/.claude/skills/tycho-syntax`; the mistakes it lists are exactly what the probe exists to re-discover.

## Which model to use

A weaker model does not behave more like a newcomer; it mostly produces expectation mismatches against Python and C (comparison: `git show ba952872:docs/internals/probe-model-comparison-2026-08-20.md`). **A probe's value comes from where it is aimed, not from which model holds the pen.**

**Write down which model ran**; a failed launch in `pi-run.log` is not a record of what ran instead. The model belongs in the first paragraph of the record, beside the host and the `tychoc` version.

## Launching it

`sh scripts/probe_run.sh <probe-dir> "<prompt>" [model]` runs the agent under a watchdog and says why it ended. Do not just background `pi -p` and wait: it can print its closing summary and stay resident at 0% CPU.

From outside, "still working" and "finished but will not exit" look identical. From inside they do not — an agent thinking, or waiting on an API round trip, writes no files but still accrues CPU time; a wedged one accrues none. So the runner requires **both** signals before killing: zero CPU-time delta *and* no write under the probe directory, sustained for `QUIET` seconds (default 300), with `MAX` (default 3600) as a hard cap.

```
QUIET=240 MAX=3000 sh scripts/probe_run.sh /path/to/probe-foo \
    "Read BRIEF.md in this directory and carry it out completely." mimo-v2.5
```

**Do not replace this with a plain timeout**: a productive probe can look stuck at the five-minute mark while busy throughout. `--selfcheck` runs both controls — a wedged process must be caught, a working one must be left alone.

Across 31 probes the quiescence path has never fired; every one exited on its own. Its evidence is those two controls, not field use.

## Reading the result

**Build and run the agent's code under the sanitizers.** A probe's report is not its whole yield: a C shim returning a pointer into an already-closed directory stream printed correct output in the plain build.

Then check each finding against `main` before acting — some will already be fixed, and some are documented behaviour the agent simply met for the first time. Both are worth knowing apart.

## Filing it

If the program is worth keeping, it goes in `tools/` with a `FRICTION-OUTSIDE.md` beside it. If it is not — and usually it is not, the record being the artifact — write `docs/internals/probe-<surface>-<date>.md` instead and throw the program away.
