# Probe: uninitialised arena reads, 2026-09-11

> Status: probe record. The instrumented runtime was thrown away; this is the
> artifact. Nothing was committed to `runtime/tycho_rt.c`.

## Why

The four sanitizer lanes cover **addresses** (ASan), **undefined behaviour**
(UBSan), **leaks** (LSan) and **races** (TSan). None of them sees a **read of
memory that was never written**.

That gap matters more here than it would elsewhere. `arena_alloc` is a bump
pointer over a block the runtime got from `malloc`: it hands back memory that is
*not* zeroed and that ASan considers perfectly valid, because the enclosing
block is a live allocation. So a codegen path that reads a struct field or array
slot before writing it is invisible to every lane in the tree, and prints
whatever the arena last held. It is also a failure mode that tends to **pass a
golden** — a fresh page reads as zeros, so the output looks right until the
allocation pattern shifts.

Valgrind's memcheck is the instrument that closes that gap. This tree already
uses valgrind client requests for the constant-time crypto proof
(`docs/internals/ffi-review-2026-08-14.md`), so the technique is established; it
had simply never been pointed at the arena.

## What was run

Two sweeps over the 288 flat fixtures in `tests/*.ty`, each built and then run
under `valgrind --error-exitcode=42 --errors-for-leak-kinds=none`.

**1. Plain memcheck — 288 ran, 0 errors.** Weak by construction, and recorded as
such: valgrind sees the arena as one large `malloc` and cannot tell a slot
inside it from any other byte. This sweep therefore tests roughly what ASan
already tests.

**2. Arena-instrumented memcheck — 288 ran, 0 errors.** The real test. A copy of
`runtime/tycho_rt.c` was patched so every arena handout is marked undefined:

```c
static inline void *arena_alloc_vg(Arena *a, size_t n) {
    void *p = arena_alloc_i(a, n);
    if (p) VALGRIND_MAKE_MEM_UNDEFINED(p, n);
    return p;
}
#define arena_alloc(a, n) arena_alloc_vg((a), (n))
```

The **macro** is the important half: internal callers reach `arena_alloc_i`
through it, so patching only the exported `arena_alloc` function would have
instrumented almost nothing. Fixtures were built against the patched copy with
`./tychoc1 <f> --runtime <copy>`, which is why the repo needed no change.

## The control, because a clean sweep from an unproven instrument is worth nothing

A detector that cannot report is indistinguishable from a clean tree. So the
instrument was made to fire on purpose — a C program including the patched
runtime, allocating 64 bytes from a real `Arena` and branching on them without
writing:

```text
exit=42
==2128933== Conditional jump or move depends on uninitialised value(s)
```

It reports. The 288/0 above was measured against a working detector.

**3. Extended to corelib and the examples — 83 ran, 0 errors, 0 failed to
build.** All 46 packages under `corelib/test/` and all 37 under
`examples/corelib/`, same instrumented runtime. This is the stronger half of the
result: the flat fixtures are mostly minimal feature probes, while these are
real programs that allocate, grow and partially fill buffers — `bignum`,
`decimal`, `regex`, `json`, `compress`, `sha256`, `zip` — which is the shape
that produces a read of a slot nobody wrote. The libcurl-, libpng- and
sqlite-backed packages built and ran too, so nothing was silently skipped.

**371 programs total, 0 uninitialised arena reads.**

## What this does and does not establish

**Does:** across 371 programs — every flat fixture, every corelib test package
and every corelib example — no execution reads an arena slot it never wrote,
with an instrument demonstrated to catch exactly that.

**Does not:** prove the property. The corpus exercises the paths the corpus
exercises — a codegen path nothing reaches is as untested here as anywhere. And
memcheck reports the **read**, not the missing **write**, so a real finding would
still need tracing back to the construct that failed to initialise.

## 4. `tools/` — closed 2026-09-18, and it was not the job this record expected

This record said driving 30 programs properly was "a bigger job than this
probe". It was not, because **the arguments already existed**: 27 of the 30
tools ship their own `run.sh` gate that feeds them real inputs, and every one
resolves its compiler as `TYCHOC="${TYCHOC:-./tychoc1}"`. So nothing had to be
invented and no gate had to be edited — a wrapper standing in for `tychoc1` does
both halves:

1. appends `--runtime <instrumented copy>`, and
2. replaces the produced binary with a shim that runs it under `valgrind -q
   --log-file=...`.

**Findings go to a log, deliberately not to `--error-exitcode`.** The gates'
exit codes and stdout have to stay byte-identical or they stop testing what they
test, and a probe that quietly breaks the suite it is riding on is worse than no
probe.

**The instrument was proved in BOTH directions this time**, which the first pass
did only in one:

| control | expected | measured |
|---|---|---|
| read an arena slot nobody wrote | must fire | exit 42, `Conditional jump or move depends on uninitialised value(s)` |
| the same slot, written first | must stay silent | exit 0, nothing reported |

The negative half is not decoration: without it a scanner that matches anything
is indistinguishable from a working one.

**27 tools, 950 instrumented runs, 0 uninitialised arena reads.**

**Two of the gaps this record first reported were closed the same day, and the
first wording of both was wrong:**

- **`tycho-fh` — now covered.** It compiles its own binaries with `cc` after
  `--emit-c`, so the `tychoc1` wrapper alone never reached them. A second shim,
  on `cc`, wraps only *linked executables* and leaves `-c` objects alone — hand
  the linker a shell script and the gate fails for the wrong reason. Gate green,
  6 instrumented runs, 0 reads.
- **`tycho-debug` — mostly covered, and "void" overstated it.** Legs [1]–[6]
  pass under instrumentation — scripted session, `-b`, run-to-completion,
  Ctrl-C, fail-closed, the `tycho debug` wrapper — with 12 instrumented runs and
  0 reads. Only leg [7] fails, and it is the one leg that *cannot* survive this
  harness: it asserts how `tycho-debug` **locates a compiler** (beside the
  binary, via `TYCHOC`, via `PATH`), while the probe's whole method is replacing
  the compiler with a wrapper somewhere else. The leg is measuring the variable
  the probe changes.

**What is NOT covered, named rather than rounded up:**
- **`tycho-chess`** — 41 runs clean, then stopped. perft is pure compute and
  barely allocates, so it is the worst ratio of memcheck cost to arena coverage
  in the tree: minutes of memcheck per arena allocation it does not make.
  Partial, deliberately, and the cheapest of the remaining gaps to close for
  anyone willing to leave it running.
- **`tycho-debug` leg [7]** — see above; not closable by this method at all.
- **`tycho-fetch`, `prof`, `prunner`** and the single-file tools — no `run.sh`.

**One failure that is the INSTRUMENT, not the tree.** `tycho-flow` fails under
memcheck: *"the pool drained out of order only 77 of 200 runs"*. Isolated by
running the same gate with the same compiler and no valgrind, where it is green
and reorders on essentially all 200 — memcheck serialises threads, so an
assertion that needs real parallelism degrades. Recorded because the next person
to point valgrind at a concurrency gate will meet it again.

**Found on the way:** five of these gates silently discard the `TYCHOC`
override and run the C bootstrap instead of the shipped compiler — FRICTION 93.
That is why the sweep initially reported "no instrumented run" for them, and it
is a defect in its own right.

## Why no lane was added

It found nothing, and a lane costs time on every run forever. The method is
cheap to re-run from this record when there is a reason — after codegen work
that touches initialisation, or if a golden ever differs in a way that smells
like stale bytes. Recorded rather than institutionalised.
