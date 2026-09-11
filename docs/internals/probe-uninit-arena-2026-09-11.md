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
exercises — a codegen path nothing reaches is as untested here as anywhere.
**`tools/` is still outside it**: those 30 programs mostly need arguments or
input files to do anything, so a bare invocation would exercise almost nothing,
and driving each one properly is a bigger job than this probe. And memcheck
reports the **read**, not the missing **write**, so a real finding would still
need tracing back to the construct that failed to initialise.

## Why no lane was added

It found nothing, and a lane costs time on every run forever. The method is
cheap to re-run from this record when there is a reason — after codegen work
that touches initialisation, or if a golden ever differs in a way that smells
like stale bytes. Recorded rather than institutionalised.
