# Roadmap

Tycho is **0.8.9 — pre-1.0**, and this file is a direction, not a promise of
dates. Day-to-day work is tracked in [GitHub
issues](https://github.com/StefanVonRanda/tycho/issues); this file is the
high-level shape. What 1.0 now requires is [below](#what-10-requires).
Terms (`surface.lock`, `tychoc`/`tychoc1`, lanes, FRICTION) are defined in
[STATUS](STATUS.md).

## Where it is

The language and corelib are feature-complete for the thesis they exist to
prove; the strength is the correctness harness (fuzzer, sanitizer lanes,
golden-locked suite), green in `make ci`. See [STATUS](STATUS.md) for what
works and how it is known, and [docs/architecture.md](docs/architecture.md)
for what each gate proves.

Production readiness is [below](#what-production-ready-requires). What is left
is not engineering: an outside program and an external review, both needing
another person.

## The self-hosted compiler

`compiler/` is Tycho written in Tycho — about 21,000 lines in six
packages (`lex`, `parse`, `ast`, `types`, `emit`, `driver`), built as `tychoc1`.
The test, corelib, example and server runners default to it, so the gates run
on the self-hosted compiler; `src/tychoc.c` bootstraps it and scores it.

- **The bootstrap is two-stage and the fixpoint holds**, gated as
  `make fixpoint-check`.
- **Two gates carry it.** `make parse-check` scores its front end against
  `tychoc` verdicts with AST/resolution/type censuses; `make tychoc1-check`
  runs the real suites under it.
- **Decided 2026-09-11: `tychoc1` stays the second opinion.** Where the two
  compilers diverge it is a defect in one of them (FRICTION 87 found a worse
  diagnostic in the reference this way). `src/tychoc.c` is normative,
  `tychoc1` is the oracle; see
  [architecture](docs/architecture.md#which-compiler-is-right).

## The language surface moved once, deliberately

Since 2026-08-22 the keyword, builtin and corelib surface is locked by
`surface.lock` and gated by `make surface-check`, with no new language
features before 1.0. The lock broke once, on purpose in 0.8.5, for layout and
SIMD (`packed`, `align(N)`, `vector[N]T`, simultaneous assignment, swizzling);
it records additions rather than forbidding them.

Corelib may still gain functions; it may not lose one or change a signature.
Since 2026-09-03 a gained function must be recorded in the same commit.
To record deliberately: `python3 scripts/surface_lock.py --record`.

### Taken since the lock

All three have shipped; kept here as the record of what the freeze was broken for:

- **Alignment and packed layout** (shipped 2026-09-04/05):
  `packed struct` gives a byte-exact layout;
  `align(N) struct` raises alignment to `N` (max 8)
  ([spec 17.1a](docs/spec/12-aggregates.md#171a-packed-layout)).
- **Vectors** (shipped 2026-09-04): `vector[N]T` is a fixed array whose
  arithmetic is one machine vector operation
  ([spec 5.3.11](docs/spec/03-types.md#5311-vectornt)); `N` is a power of two, 2–64.
- **Groups** (shipped): simultaneous assignment of 2–8 places and swizzling
  2–8 components of one value
  ([spec 17.5](docs/spec/12-aggregates.md#175-destructuring)), with `.x/.y/.z/.w`
  lane names on vectors.

Nothing further is planned before 1.0. Not queued: compile-time execution
(deferred) and field promotion (`using`, refused).

## Near-term

Foundation before feature breadth. In rough priority:

1. **Demand-gated corelib / tooling.** New library and tooling work is built against a
   real program that needs it, never ahead of one. Requests belong in the issue tracker.
2. **Keeping the compiler honest.** New language features are adversarially fuzzed
   and gated against recorded goldens before shipping. Ongoing, not a milestone.
3. **Overloading — open, demand-gated.** A name may have exactly one definition.
   The trigger is a real program that needs it, not a design impulse.

## Friction phases from the `tycho-dupe` probe (2026-10-19)

> **Status, 2026-10-19: all seven phases are DONE and gated.** The probe record
> is [`tools/tycho-dupe/FRICTION-OUTSIDE.md`](tools/tycho-dupe/FRICTION-OUTSIDE.md);
> the gate is `make dupe-check` (CI step `[4c2/37]`).

The duplicate-finder probe in [`tools/tycho-dupe`](tools/tycho-dupe) found seven
places where a real CLI fought the language or the corelib. Each phase lands
with its own gate; the record is
[`FRICTION-OUTSIDE.md`](tools/tycho-dupe/FRICTION-OUTSIDE.md).

### Phase 1 — `core:io.stat` / `lstat` / `same_file`

**Status: DONE 2026-10-19.**

Add a `Stat` value with `FileKind`, size, mtime, inode and device, plus
`io.stat` (follows symlinks), `io.lstat` (does not) and `io.same_file(a, b)`
(device + inode). This removes the C shim a filesystem tool otherwise needs for
symlink, hard-link and special-file identity, and collapses three syscalls per
entry into one.

**Done when:** the shim functions exist on POSIX and Windows, the Tycho wrappers
and docs are in `surface.lock`, `corelib/test/io` covers a regular file, a
directory and a missing path plus `same_file` self/different, and the
`tycho-dupe` gate covers symlink, symlink-cycle and hard-link identity.

### Phase 2 — `path.real_checked`

**Status: DONE 2026-10-19.**

Add `real_checked(p) -> Result(string, PathErr)` with at least `NotFound`,
`Denied`, `TooLong` and `BadPath`; keep `real` as the fail-closed convenience.
Follow the `io.read_text` / `io.read` precedent so a tool can say why a path
could not be resolved.

**Done when:** the checked function is tested against a missing path, a
permission-denied path and a normal path, is in `surface.lock` and the corelib
catalogue, and `tools/tycho-dupe` uses it.

### Phase 3 — pin the read-only parameter copy boundary

**Status: DONE 2026-10-19.**

Make `reference/functions.md`, `reference/basics.md` and the normative
`spec/11-functions.md` say exactly which values are copied and which are
borrowed: scalars copy; heap-bearing values (`[T]`, maps, strings, structs
holding them) pass a read-only descriptor with shared backing storage; fixed
arrays `[N]T` and structs that inline them copy the whole value; `inout` and
`sink` are the explicit mutation/consume modes. Add a table with one example
per shape.

**Done when:** the three pages agree, the table is present in the spec, and the
fixed-array copy is named as a cost rather than hidden under "large values."

### Phase 4 — document `inout` forwarding

**Status: DONE 2026-10-19.**

Show that an `inout` parameter is forwarded with `&` at the second call site
(`fn twice(n: inout int): bump(&n)`), in both the reference and the spec. The
behavior already exists and is exercised by `tests/calc.ty`; the gap is that the
docs only show `&` from an ordinary variable.

**Done when:** the reference and spec examples compile and run under
`make docs-fences`.

### Phase 5 — `eprintln`

**Status: DONE 2026-10-19.**

Add `eprintln(s)` as a builtin in both compilers, with the same one-`string`
signature as `eprint` and a trailing newline. This removes `eprint(s + "\n")`
from every CLI error path.

**Done when:** both compilers emit it, `surface.lock` records it, the builtin
reference and spec list it, `tests/eprintln.ty` plus the tool's error-path golden
prove stdout/stderr separation, and `tools/tycho-dupe` uses it.

### Phase 6 — make the checked integer parser the default

**Status: DONE 2026-10-19.**

Rename `strings.parse_int` (lax, prefix-parsing) to `parse_int_prefix`, and
`parse_int_checked` (strict, `Result`) to `parse_int`. Update every call site,
test, example, tool and doc — including the `tycho-syntax` correction layer —
and keep the behavior of each site deliberate rather than mechanical.

**Done when:** the corelib tests cover both policies under the new names, every
call site compiles under both compilers, `surface.lock` is recorded, and the
docs no longer describe the dangerous name as the obvious one.

### Phase 7 — path quoting

**Status: DONE 2026-10-19.**

Add `path.quote(p)` (POSIX shell-safe single quoting, with an embedded quote
spelled `'\''`) so a text report can print an arbitrary path unambiguously.
Keep `--json` as the machine-readable form.

**Done when:** the helper is tested against spaces, quotes, tabs and newlines,
is in `surface.lock` and the corelib catalogue, and `tools/tycho-dupe` uses it
in its text report with a golden update.

## Compiler optimization phases: widening the domain (2026-10-19)

> **Release scope:** 0.8.9 stays open until **O1–O7** are all in it. O1
> (immortal allocation + literal lifetime elision), O2 (read-only
> pass-by-pointer in the reference compiler), O3 (last-use moves) and O4
> (borrowed slices) are done, with their before/after benchmarks. The packaged
> compiler already had O2's descriptor form; O2 closes the bootstrap gap.
> O5–O7 are next. Nothing in this section is claimed as shipped merely because
> it is written down.

The goal is to expand the domains where Tycho competes by exploiting what value
semantics already guarantees — uniqueness, read-only parameters, and the absence
of pointer identity — **without adding a reference type, an escape hatch, or a
new surface feature**. These are codegen/runtime phases; `surface.lock` is not
touched.

Every phase uses the same evidence rule:

- one fixture under `bench/opt/`, with byte-identical output before and after;
- peak RSS and wall time from `bench/peakrss.c`, 5 runs, median reported;
- a `TYCHOC_NO_<OPT>` control that disables the optimization in the same
  compiler, so the comparison is not a different tree or a different machine;
- both compilers (`tychoc` and `tychoc1`) must agree on output, and the full
  `make test`, `make corelib`, `make parse-check` and sanitizer lanes must stay
  green;
- a phase is not done until the benchmark shows a gain on at least one target
  workload and no regression on the others.

### O1 — Immortal allocation marking + literal lifetime elision

**Status: DONE 2026-10-20 — shipped in 0.8.9.** The numbers are in
[`bench/opt/RESULTS.md`](bench/opt/RESULTS.md).

A `[string]` push copies the bytes unless the source is a string literal. A
literal lives in `.rodata` for the whole process, so its header is now marked
**immortal** (a negative length); `tycho_str_len` masks the marker, `tycho_str_copy`
returns an immortal pointer unchanged, and `tycho_str_recycle` leaves it alone.
The compiler needed no special branch: every copy site inherits the elision.

On 6,000,000 literal pushes into a `[string]` field:

| | peak RSS (median of 5) | time (median of 5) |
|---|---:|---:|
| copy before | 207,460 KB | 61 ms |
| immortal pointer | **114,056 KB** | **28 ms** |

That is **45% less peak RSS and 54% less wall time**. Both compilers emit the
same output; `make test`, `make corelib`, `make parse-check` and the full gate
are green.

**Done when:** a string/bytes allocation can be marked immortal; copy, recycle,
equality and mutation paths respect it; the `bench/litarr_build.ty` before/after
shows the gain above; both compilers and the full gate are green. **All met.**

### O2 — Read-only pass-by-pointer for large value parameters

**Status: DONE 2026-10-20 — reference/bootstrap parity in 0.8.9.** The result is
in [`bench/opt/RESULTS.md`](bench/opt/RESULTS.md).

A default parameter is read-only, so a large fixed array can cross a direct call
as a `const T*` instead of a full C value. The callee cannot mutate it, and any
return/capture still copies, so the source semantics are unchanged. The
reference compiler applies this to non-heap fixed arrays of at least four
elements; `TYCHOC_NO_PTR_PARAMS` is the control. The packaged `tychoc1` already
lowers `[N]T` to a 24-byte dynamic-array descriptor, so it has no 2 KB inline
copy to elide.

On `bench/opt/bigparam` (5,000,000 calls, `-O2 -fno-inline` to isolate the ABI):
reference **140 ms → 66 ms** median, same RSS; packaged `tychoc1` **10 ms**.
O2 closes the reference gap to the representation the shipped compiler already
had, so the packaged binary's performance is unchanged.

**Done when:** the pointer ABI is used for direct calls, function values and
spawn trampolines bridge it, the fixture improves, and the existing
`[N]T`/function-value/spawn tests are byte-identical. **All met on the reference
compiler; `tychoc1`'s lowering already avoids the copy.**

### O3 — Last-use moves / auto-sink

**Status: DONE 2026-10-20 — shipped in 0.8.9.** The result is in
[`bench/opt/RESULTS.md`](bench/opt/RESULTS.md).

`push`, a direct `sink` call and a heap rebind can adopt a dead local instead
of deep-copying it. A loop-local is movable when it was declared at the current
loop depth: it is a fresh buffer each iteration, so its one textual read is one
dynamic read. A local declared outside the loop is still refused.

For `push`, the compiler also promotes a loop-local whose only read is the
`push` into the destination array's arena. That is what makes the adoption
possible: the old code built the value in the per-iteration scratch, then
copied it into the long-lived array. `TYCHOC_NO_MOVE_PUSH=1` restores the copy
path for measurement. A `push` into a caller-owned `sink` array remains a copy,
because the source scratch dies before the caller's array does.

On `bench/opt/movebuild` (500,000 loop-local four-string structs): `tychoc1`
**91 ms → 79 ms**, reference `./tychoc` **96 ms → 87 ms** (median of ten and
five respectively); RSS is unchanged because both builds retain the same final
array. The move path is covered by `tests/move_loop_local.ty`, including the
cross-arena `sink` copy case.

**Done when:** the move path is selected only when uniqueness is proven, the
fixture improves, and the copy case still copies. **All met.**

### O4 — Borrowed string/bytes slices

**Status: DONE 2026-10-20 — shipped in 0.8.9.** The result is in
[`bench/opt/RESULTS.md`](bench/opt/RESULTS.md).

`substr` and `s[a:b]` copy today. Strings are immutable, so a slice can be a
hidden `(pointer, length)` view while the parent is alive; if the slice escapes
its parent's arena, the existing `copy_into` seam materializes it. The view is
a 16-byte `TychoStrView` with the low pointer bit set, allocated in the call's
arena: no payload bytes are copied, and the view dies with that arena. Only
call-argument positions create views; a binding, return, `push`, map key or
other retention path goes through `tycho_str_copy`, which materializes a normal
string. FFI boundaries call `tycho_str_cstr`, which materializes a view because
a C `char*` must be NUL-terminated.

`TYCHOC_NO_SLICE_VIEW=1` restores the copy path for measurement. On
`bench/opt/sliceparse` (100 in-scope slices of a 12.8 MB string): `tychoc1`
**33 ms / 42.9 MB → 7 ms / 30.6 MB**; reference `./tychoc` **32 ms / 43.0 MB →
6 ms / 30.7 MB**. `tests/pkg/slice_view` covers views, `len`, FFI, binding,
return and `push` materialization. Byte output is identical.

**Done when:** in-scope slices copy no payload bytes, escaping slices still
copy, and the parser output/goldens are byte-identical. **All met.**

### O5 — Destination passing / automatic `inout`

**Status: planned; the largest phase.**

For `t = f(t, ...)` where `t` is uniquely owned and dead after the call,
specialize `f` with a destination/`inout` convention. This turns the
rebuild-per-operation wall (§4c) into in-place updates for ASTs, editors,
caches and server state. The existing `sink` and `inout` place machinery
(`&b.asks`, `&g.items(i)`) is the prototype; the missing piece is a sound
uniqueness proof and an interprocedural reuse summary.

**Before/after fixture:** `t = insert(t, k)` over a tree, and a long-lived
editor-buffer update. **Done when:** unique calls mutate in place, shared calls
still copy, and the fixture shows the path-copy/allocation drop with identical
output.

### O6 — Conservative shared-bit CoW / path copying

**Status: research; only if O1–O5 leave a measured gap.**

A single conservative “shared” bit per heap allocation would let `b := a` share
until a write, and nested trees share untouched subtrees (path copying). It is
not reference counting: a stale bit can over-copy, never under-copy. It is also
the one phase that conflicts with the project’s “no CoW/RC” non-goal as a
*model*; it is admissible only as a shape-gated implementation optimization.
The before/after fixture is `bench/trie` + a path-copying update; the gate must
show a real RSS/time gain and no mutation regression before this is considered.

### O7 — SROA and bounds-check elimination

**Status: planned; orthogonal to value semantics.**

Non-escaping small arrays/maps/structs can be scalar-replaced or stack-allocated
instead of arena-allocated, and monotone loop indices can drop redundant bounds
checks. These are not copy-elision phases, but they address the measured
`array-pipeline` gap that the model itself does not cause.

**Before/after fixture:** `bench/array-pipeline` and a tight small-map loop.
**Done when:** the checks/allocs disappear only where proven redundant, and the
fixture improves without changing any golden.

## What 1.0 requires

A 1.0 date was declared 2026-08-05 and withdrawn 2026-08-09 — there was no one
to make the promise to yet. What remains is §1 and §7, neither a task this list
can complete: an outside program and an external review, both needing another
person.

### 1. Someone other than the author has written a real program

The blocking one. Every ergonomic finding so far came from the author
dogfooding; only a stranger's mental model produces the rest.

Concretely: **three non-trivial programs written against the docs alone**,
each with its friction written down, each targeting a different half of the
surface.

Four exist across four halves — `tools/tycho-diff` (algorithms),
`tools/tycho-hash` (concurrency),
`tools/tycho-fold` (text and codepoints), and the 2026-08-19 FFI probe
([record](docs/internals/probe-ffi-2026-08-19.md)) — each with its
`FRICTION-OUTSIDE.md`. Left open deliberately: the probes keep finding what
the author could not, and generics, `subscript`, `bounded[N]`, `select` and
error paths still have no non-author program
([procedure](docs/internals/probe-procedure.md)).

### 2–6. ~~Papercuts, expressiveness, `core:net`, Windows, a shipped release~~ — **all CLOSED**

Closed between 2026-08-10 and 2026-08-15, in that order. The record is in
[`docs/internals/roadmap-closed-2026-08.md`](docs/internals/roadmap-closed-2026-08.md).

### 7. An external security review

[SECURITY.md](SECURITY.md) says plainly that there has been no third-party
audit. A reviewer packet exists
([audit-brief](docs/internals/audit-brief.md)); three internal passes found
5, 9 and 10 issues with zero overlap, which is the argument for this section.

### What is explicitly NOT required

More features, more corelib packages, more benchmarks, or a package manager.
The language is feature-complete for the thesis it exists to prove.

## What production-ready requires

Set 2026-08-15. **The thesis is proven**; the goal now is a complete,
production-ready language. "Feature-complete for a thesis" no longer settles
scope; the non-goals resting on it are open for re-decision by the owner.

### 1. It runs where developers are

Artifacts exist for `linux-x86_64`, `mingw64-x86_64` and **`darwin-arm64`**
(built 2026-09-19). None is published for macOS yet — `gh release create` is
the owner's call. ARM64 Linux runs `make test` green in a VM on Apple Silicon;
no Graviton artifact is built yet.

`make ci` is green on `darwin-arm64` with enumerated skips, one a real coverage
loss (no LeakSanitizer on the platform). The run found three defects, two
latent in shipped code (FRICTION 100–102). All three cross-compile targets have
had binaries run, including a float-determinism defect only two machines could
show (FRICTION 112–113). See [STATUS](STATUS.md) for the current platform scope.

Why no porting was needed:

- `runtime/tycho_rt.c` already carries macOS support (stack bounds,
  per-architecture stack-pointer extraction, `__aarch64__` branch).
- `src/tychoc.c` has zero platform conditionals.
- With `zig cc` as cross driver, the compiler and the emitted C build for
  `aarch64-macos`, `x86_64-macos` and `aarch64-linux` with no source change.

### 2. ~~A story for using other people's code~~ — **DECIDED 2026-08-15: vendoring, Odin-style**

Follow Odin and Go: vendor dependencies as directories you commit. No package
manager, no registry, no lockfile. A non-`core:` import resolves relative to
the importing file, so `vendor/` works by construction; `tests/pkg/vendor_deps/`
gates both shapes. The convention is in
[packages](docs/reference/packages.md#vendoring). Cost: transitive dependencies
by hand, no mechanical security-fix notice.

### 3. The promise, written down

The deprecation machinery is exercised end to end. [SUPPORT.md](SUPPORT.md)
states what may break in which release, that a deprecation survives until the
next major, that Linux x86_64 is the one supported platform, and that there is
no support window — with the plain note that 1.0-onward promises cannot yet be
relied on at 0.x.

### 4. Trust that did not come from here

§1 and §7 above survive the reframe unchanged. Shipping to strangers is where
an unaudited FFI boundary is a liability; [audit-brief](docs/internals/audit-brief.md)
is the packet for the second.

## Non-goals

Several things are out of scope — traits, a package manager, a
C-style ternary, Hindley-Milner inference, refcounting/GC, and hosted CI among them. The
full list is in [docs/architecture.md](docs/architecture.md#decided-non-goals).
New directions are welcome; re-proposals of these need new evidence.
Runtime-internal double-headed / single-block packing is being evaluated now,
without a surface change; scenario evidence and targeted opportunities are in
[plan-value-storage-places](docs/internals/plan-value-storage-places.md).
A measured local optimization need not wait for 1.0; no default allocator rewrite is approved.
The compiler follow-up removes a duplicate AST adapter and reuses an existing
append builder: about 27% lower capacity and 25% lower RSS on a frozen compiler
input, with unchanged emitted C; no demonstrated latency gain. Controls and
remaining limits are recorded in that same plan.

Traits were reopened and re-closed 2026-09-11; see
[architecture](docs/architecture.md#traits--typeclasses--reopened-2026-09-11-re-closed-the-same-day).
