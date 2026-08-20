# Running an agent probe

`ROADMAP.md` §1 wants programs written against `docs/` by someone who did not
write the compiler. No second human reviewer was found, so the instrument is an
LLM agent in a container. This is how to run one so it costs an afternoon and
returns something the author could not have found.

## Setup

```
git clone --depth=1 https://github.com/StefanVonRanda/tycho.git tycho-probe
cd tycho-probe
rm -rf src runtime docs/internals docs/rfc .git tests fuzz scripts bench
rm -f plan.md $(find . -iname '*FRICTION*')
```

The deletions are the point: the agent then **cannot** read the compiler source,
the friction log, or a previous probe's findings, so "do not read the compiler"
stops being an honour rule. Verify with `find . -name 'tychoc.c' | wc -l` — zero.

**Build the compiler from `main`, not the release tarball.** The 2026-08-19 run
used v0.7.0 and part of its report described diagnostics already improved since;
that half was archaeology.

## Aim it at something untouched

A program over ground already covered returns little — the 2026-08-19 markov run
found two doc gaps against tycho-diff's four. Measured over the existing
records, these have **no** non-author program at all:

| surface | non-author programs touching it |
|---|---|
| generics | 1 ([record](probe-generics-2026-08-19.md)) |
| newtypes | 1 ([record](probe-newtype-subscript-bounded-2026-08-19.md)) |
| `subscript` | 1 (same record) |
| `bounded[N]` | 1 (same record) |
| `select` | 1 ([record](probe-select-2026-08-19.md)) |
| enums / `Option` / `Result` error paths | 1 (covered by the generics run) |
| channels | 1 (`tools/tycho-hash`) |

**Every surface listed above now has at least one record** (2026-08-19). The
table is kept as the shape to re-derive when new language features land, not as
a list of gaps.

Covered: algorithms and strings (`tycho-diff`), concurrency (`tycho-hash`), text
and codepoints (`tycho-fold`), FFI + `handle` + `soa`
([record](probe-ffi-2026-08-19.md)), and generics + payload enums + Option/Result
([record](probe-generics-2026-08-19.md)) — the generics row above is now 1, not 0.

## The brief

Name the features the program MUST use, let the agent pick the program, and ask
for the log to record what went RIGHT as well as what did not — a log that only
complains is not evidence. Do not hand over `~/.claude/skills/tycho-syntax`; the
mistakes it lists are exactly what the probe exists to re-discover.

## Which model to use

Measured 2026-08-20 by running all four probes twice, once with Claude
subagents and once with `mimo-v2.5` through the `pi` CLI:
[probe-model-comparison-2026-08-20.md](probe-model-comparison-2026-08-20.md).
The weaker model did not behave more like a newcomer — it mostly produced
expectation mismatches against Python and C, and one of its four runs died at
`repetition_truncation` without writing a log at all. **A probe's value came
from where it was aimed, not from which model held the pen.**

## Launching it

`sh scripts/probe_run.sh <probe-dir> "<prompt>" [model]` runs the agent under a
watchdog and says why it ended. Do not just background `pi -p` and wait: it does
not reliably exit. The 2026-08-20 core-surface probe wrote its last file at 09:36,
printed its closing summary, and was still resident at 10:01 at 0.0% CPU.

From outside, "still working" and "finished but will not exit" look identical.
From inside they do not — an agent thinking, or waiting on an API round trip,
writes no files but still accrues CPU time; a wedged one accrues none. So the
runner requires **both** signals before killing: zero CPU-time delta *and* no
write under the probe directory, sustained for `QUIET` seconds (default 300),
with `MAX` (default 3600) as a hard cap.

```
QUIET=240 MAX=3000 sh scripts/probe_run.sh /home/igzo/probe-foo \
    "Read BRIEF.md in this directory and carry it out completely." mimo-v2.5
```

**Do not replace this with a plain timeout.** The most productive probe of
2026-08-20 ran 993 s and looked stuck at the five-minute mark while four siblings
had already finished; it was busy throughout and returned five findings, one a
real compiler defect. `--selfcheck` runs both controls — a wedged process must be
caught, a working one must be left alone.

Across 31 probes the quiescence path has never actually fired; every one exited
on its own. Its evidence is those two controls, not field use.

## Reading the result

**Build and run the agent's code under the sanitizers yourself.** The 2026-08-19
run's most valuable finding was not in its log at all: its C shim returned a
pointer into a directory stream it had already closed, and the plain build
printed correct output. A probe's report is not its whole yield.

Then check each finding against `main` before acting — some will already be
fixed, and some are documented behaviour the agent simply met for the first
time. Both are worth knowing apart.

## Filing it

If the program is worth keeping, it goes in `tools/` with a
`FRICTION-OUTSIDE.md` beside it. If it is not — and usually it is not, the
record being the artifact — write `docs/internals/probe-<surface>-<date>.md`
instead and throw the program away.
