<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="branding/tycho-logo-dark.svg">
    <img src="branding/tycho-logo.svg" alt="Tycho" width="128">
  </picture>
</p>

# Tycho

**A data-oriented systems language with automatic memory management from
lexical scope.** Tycho began as an experiment testing one idea — implicit
hierarchical arenas under value semantics — and that idea now holds. It is built
for **programs that allocate hard and cannot afford a GC pause** — parsers,
interpreters, solvers, batch CLIs and long-running services — and its value
semantics steer data into the flat, index-addressed layouts that data-oriented C
reaches for on purpose. Every
scope owns a memory arena, freed when the scope exits; with no reference type in
the language, the compiler sees every value's lifetime from the syntax alone and
inserts every allocation and free itself. No garbage collector, no manual
`free`, no borrow checker. It transpiles to C and builds with `cc` and `make`.

[Docs](docs/README.md) · [Tutorial](docs/tutorial.md) ·
[Reference](docs/reference/index.md) · [Thesis](docs/thesis.md) ·
[Spec](docs/spec/) · [Performance](docs/performance.md) ·
[How it is tested](docs/controls.md) · [Status](STATUS.md)

```tycho
fn evens(limit: int) -> [int]:
    xs := []int                    # no size, no malloc, no owner to track
    for i := 0; i < limit; i += 1:
        if i % 2 == 0:
            push(xs, i)            # grows in place
    return xs                      # it escapes, so it was built in the CALLER's arena

fn main():
    xs := evens(10)
    println(str(len(xs)) + " evens, last " + str(xs[4]))
    # xs dies when main's scope does. No free() here, and none in the emitted C.
```

That last comment is the whole language, and the compiler decides it from the
shape of the code. `evens` returns `xs`, so there is nothing to free at its
scope exit and the array is built in the caller's arena from the start:

```c
TychoArrInt h_evens(Arena *_parent, tycho_int limit) {
    TychoArrInt h_xs = tycho_arr_int_with_cap(_parent, 0);   /* caller's arena */
```

Change one line — `return len(xs)` instead of `return xs`, so nothing escapes —
and the same function emits a scope of its own, released on the way out:

```c
tycho_int h_count_evens(Arena *_parent, tycho_int limit) {
    Arena _scope = arena_child(_parent);                     /* its own arena */
    ...
    { tycho_int _ret = h_xs.len; arena_free(&_scope); return _ret; }
```

No annotation chose that, and no runtime worked it out. The lifetime is visible
in the syntax, so the compiler places every allocation and every free itself.

## What you give up, before anything else

**There is no reference type.** That is what makes the paragraph above work, and
it is the part that will cost you a weekend if you meet it on page 40 instead of
here. A shared mutable graph, a doubly-linked list, an observer holding a
pointer back at its subject — none of them can be written in Tycho at all. Not
"discouraged": inexpressible. If your mental model is C's, the first complex
data structure you reach for will not compile, and the compiler will look broken
when it is the model that changed.

**There is also no interface value.** A trait object is a pointer to someone
else's value plus a vtable, and the pointer is exactly the aliasing this model
forbids — so a Go or Java `interface` held in a struct field, and a
heterogeneous list of implementors, can never exist here. That is a boundary of
the model, not a feature still to come
([architecture](docs/architecture.md#decided-non-goals)). Generics cover the
static half: a generic body type-checks after substitution, so one function
works over many types — it just cannot hold two different ones at the same time
under one name.

**The idiom that replaces them is a flat node pool.** Hold every node in one
array; an edge is an integer index into it, not an address. Sharing is two
indices naming one slot:

```tycho
struct Node:
    kids: [int: int]           # next byte -> node INDEX, not a Node by value
    word: bool

fn insert(pool: inout [Node], s: string):
    cur := 0
    for i := 0; i < len(s); i += 1:
        c := s[i]
        if not (c in pool[cur].kids):
            push(pool, Node([]int: int, false))
            pool[cur].kids[c] = len(pool) - 1
        cur = pool[cur].kids[c]
    pool[cur].word = true

fn main():
    pool := [Node([]int: int, false)]
    insert(&pool, "ab")
    insert(&pool, "ac")
    println(str(len(pool)) + " nodes, root fanout " + str(len(pool[0].kids)))
```

```output
4 nodes, root fanout 1
```

That is the same layout a data-oriented C engine reaches for on purpose — one
contiguous array, 8-byte indices instead of scattered pointers, traversal that
walks cache lines instead of chasing them. Value semantics make it the default
rather than the optimization you remember to apply.

**And it has a measured price.** Pointer-shaped data stored by value costs more
RAM than C: a recursive trie **~1.55×**, a fixed-capacity LRU **~2.8×**; the
flat-pool idiom brings the graph analog to **~1.3×**. Arenas also reclaim at
scope exit, not incrementally, so a long-lived scope holds its transients until
it returns. The full loss column, with the measurements and the workloads that
want a different tool, is
**[docs/internals/value-semantics-limits.md](docs/internals/value-semantics-limits.md)**;
[from `malloc` to arenas](docs/from-c-to-arenas.md) is the same ground from C.

> **Status: 0.8.5 — pre-1.0. No stability guarantees yet.** The thesis is
> proven and the work now is shipping a complete language, not defending an
> idea. It is pre-1.0 because 1.0 is a promise not to break people, and nobody
> outside this repo has written enough Tycho to know what that promise costs —
> that, and an external security review, are the two remaining conditions, and
> neither is engineering. The
> engineering is not the open question — see
> [Architecture](docs/architecture.md) for what each gate proves.
>
> Until 1.0, expect the language surface and the corelib API to change with a
> changelog entry and no deprecation window. What is already dependable in
> practice: the [spec](docs/spec/) is normative and the implementation is gated
> against it. What is explicitly not: performance tuning and the benches,
> internal implementation details (the emitted C shape, arena sizes), and the
> areas the spec or [SECURITY.md](SECURITY.md) mark as sharp edges.
> [SUPPORT.md](SUPPORT.md) states this as policy — versions, deprecations and
> which platforms carry a promise.
> [ROADMAP.md](ROADMAP.md#what-production-ready-requires) lists what production-ready
> requires; the 1.0 conditions are in the section above it.
> Versioning is `tychoc --version` + [CHANGELOG.md](CHANGELOG.md).

## Key features

- **No GC, no manual `free`.** A value can leave a scope in exactly two ways,
  both visible in the source — *down*, passed to a callee, or *up*, returned to
  the caller — so the compiler places every allocation from the syntax alone.
  The payoff is automatic memory management from lexical scope, not a runtime.
- **Value semantics.** `b := a` copies; there is no shared mutable storage, so
  data races are inexpressible inside Tycho (copy-in/copy-out concurrency, no
  annotations).
- **One dependency-free C file.** The transpiler is `src/tychoc.c`; the only
  toolchain is `cc` and `make`.
- **Concurrency, generics, closures, FFI.** `spawn`/`wait` tasks, bounded
  channels, `parallel for`, monomorphized generics, closures, UFCS, and a
  C-interop boundary — all shipping.
- **Heavily tested.** Every example is built twice — native and sanitized — and
  checked against a committed golden; a fuzzer applies the same differential to
  random programs under ASan/UBSan and feeds malformed input to prove the
  compiler fails closed.   seven full programs (below) ran on it with zero compiler or runtime defects.

## Quick start

**You need `make` and a C11 compiler — gcc, or clang 15 or newer.** That is the
whole toolchain.

```
$ git clone https://github.com/StefanVonRanda/tycho
$ cd tycho
$ make                                  # builds ./tychoc
$ ./tychoc examples/hello.ty && ./examples/hello
what is your name: Ada
hello Ada
```

New here? The **[tutorial](docs/tutorial.md)** goes from this to a real program
in about an hour, and **[from `malloc` to arenas](docs/from-c-to-arenas.md)**
explains the memory model from C you already know. Full build details are under
[Trying it](#trying-it). The syntax is Python/Nim-flavored and the semantics
Go/Odin-like; the value-semantics core comes from
**[Hylo](https://www.hylo-lang.org/)**.

(clang 14 is the one exclusion, and it is measured: it builds the compiler and
then miscompiles what it emits. Which compilers have been run against the whole
corpus, and what each scored, is the
[toolchain matrix](docs/platforms.md#c-toolchains).)

## Trying it

`./tychoc f.ty` transpiles `f.ty` to C and compiles it to a native binary `f`,
removing the intermediate `f.c` once `cc` succeeds (it is kept when `cc` fails,
as the evidence); `-o name` names the output, `--emit-c` stops at the C (writing
it to stdout unless `-o` names a file) — that is how you keep the C. The
transpiler is one dependency-free C file. The only optional extras are
`pkg-config` plus a library for the FFI-backed corelib modules (like
`core:http`) and a Go toolchain for the cross-language benchmarks — both skip
cleanly when absent.

**Core library.** `corelib/` is Tycho's core library, imported as
`core:<name>`. The transpiler finds it beside its own binary, so there's no
setup (`TYCHO_CORELIB` overrides). A file with an `import` is a *package* —
give it its own directory:

`mysite/main.ty`:

```tycho
package main
import "core:strings"

fn main():
    println(strings.to_upper("hello"))     # HELLO
```

Every corelib module has a runnable example under
[`examples/corelib/`](examples/corelib); two larger programs compose several
end-to-end — [`examples/fetch`](examples/fetch) (HTTP client) and
[`examples/site`](examples/site) (static-site generator). `make ci` builds and
verifies the whole tree.

### Building

| Command | What it does |
| --- | --- |
| `make` | Build the `./tychoc` transpiler. |
| `./tychoc f.ty` | Transpile to C, compile to native `f`; the intermediate `f.c` is removed on success, kept on a `cc` failure. |
| `./tychoc f.ty --emit-c` / `-o name` | Stop at the C (to stdout; `-o name` writes `name.c`) / name the output. |
| `make test` | Run the authoritative suite in parallel (`TYCHO_THREADS=N` tunes the worker count). Needs the sanitizer runtimes — see [CONTRIBUTING](CONTRIBUTING.md#what-the-gate-needs-beyond-that). |
| `make bench` | Run the performance guard (below). |
| `make fuzz` | Differential + ASan/UBSan soundness fuzzer. |
| `make corelib` | Build + validate the standard library against its goldens. |
| `make ci` | The full local gate; independent lanes run in parallel — no cloud CI. |
| `make release-check` | Build and smoke-test the current-version tarball twice; require byte-identical archives. |
| `make clean` | Remove build artifacts. |

`make test` builds every `examples/*.ty` and `tests/*.ty` twice — native `-O2`
and `-fsanitize=address,undefined` — runs both on the same stdin, and asserts:
exit 0, no sanitizer report, **byte-identical output** between the builds, and a
match against the committed golden `tests/<name>.out`. Byte-identity catches UB
the optimizer and sanitizer disagree on; the golden catches a miscompile that's
self-consistently wrong. LeakSanitizer is on — every scope frees its arena at
exit, so a leak means a real missing free. Goldens are rewritten only by `make
test-update`, never by a normal run.

`make bench` guards the *performance* claims the way `make test` guards
correctness: each `bench/*.ty` asserts one metric against a generous bound.

**Platforms.** Every row below is *gated* — `make ci` or `make test` green on
that platform, executed there, not cross-compiled and hoped for.

| platform | status |
| --- | --- |
| **Linux x86-64** | gated — the development host, and the only one that scores leaks |
| **Linux arm64** | gated since 2026-09-19 — `make test` 1065/1065 on Ubuntu 26.04 / aarch64 |
| **macOS / Apple Silicon** | gated since 2026-09-19 — `xcode-select --install` is the whole setup |
| **Windows x86-64** | gated natively under MSYS2 + mingw-w64; **WSL2** needs no setup and behaves exactly like Linux |
| **Windows arm64** | executed by `make platform-check`; a mingw target |

Three caveats are worth knowing before you build, and each is one line to fix or
one consequence to accept:

- **On a minimal Linux image, generate a comma-decimal locale first:**
  `sudo locale-gen da_DK.UTF-8` (or `de_DE.UTF-8` / `fr_FR.UTF-8`). Two
  float-formatting fixtures need a locale whose decimal point is not `.`, and a
  stock container ships only `C`/`POSIX`/`en_US`.
- **macOS scores no leaks.** Apple's AddressSanitizer ships no LeakSanitizer, so
  the leak lanes do not run there. ASan, UBSan and TSan do; Linux is the only
  host that scores leaks.
- **A native Windows server winds down slower** — within its idle timeout rather
  than within a millisecond, because MSYS2's `kill` terminates a native program
  instead of signalling it. Nothing is lost or corrupted. Sanitizer coverage is
  partial there too; [SECURITY.md](SECURITY.md) carries both measurements.

The optional FFI-backed corelib packages need their dev libraries (`zlib1g-dev`,
`libssl-dev`, `libcurl4-openssl-dev`, `libpng-dev`, `libsqlite3-dev`,
`pkg-config`), or `make shim-warn` refuses rather than passing vacuously.

Which lanes skip on which host and why each one does, the C toolchains that have
been measured against the whole corpus, and the per-platform friction entries are
the inventory in **[docs/platforms.md](docs/platforms.md)**. MSVC is not a
supported C target. Release tarballs are built per platform by
`scripts/release.sh`.

## Documentation

New to Tycho? **Start with the [tutorial](docs/tutorial.md)** — a guided first
hour that ends with a small real program and the one idea that makes the
language tick. [`docs/`](docs/README.md) is the full index; the map:

- **[Tutorial](docs/tutorial.md)** — learn the language by writing and running code.
- **[From `malloc` to implicit arenas](docs/from-c-to-arenas.md)** — the memory
  model in five steps, starting from C you already know. The gentlest way in.
- **[Language reference](docs/reference/index.md)** — every construct, by topic.
  The source of truth; every example compiles.
- **[Quiet results](docs/quiet-results.md)** — the complete register of
  operations that answer instead of refusing (bytes vs characters, clamping
  slices, wraparound, the lax parsers). Each is deliberate; each has a
  fail-closed sibling. The page to read before trusting a parse.
- **[The thesis](docs/thesis.md)** — why value semantics makes implicit arenas
  work, and where it doesn't, with measured numbers.
- **[Performance](docs/performance.md)** — the measurements behind the claims.
- **[The memory model](docs/memory-model.md)** — why value semantics makes
  implicit arenas work in practice, and what it costs.
- **[Architecture & status](docs/architecture.md)** — how it's built, what each
  verification gate proves, what's shipped, and the decided non-goals.

That is everything needed to *use* Tycho. What follows is the case that it works: why the memory model holds, the programs that prove it, the numbers, and the questions a skeptic asks first.

## Why arenas and value semantics

The arena is an old idea, and a fast one: a bump allocator hands out memory by
incrementing a pointer, and frees everything at once when its scope ends. The
catch has always been knowing *when* a value may outlive its arena — in a
language with pointers, that needs whole-program alias analysis, which is the
hard part. Value semantics removes the question: no reference type means a value
escapes only by being passed down or returned up, so every allocation can be
placed from the syntax alone. Two optimizations keep it from being slow — a
returned value is built in the caller's arena (a move, not a copy), and
`acc = acc + x` in a loop grows one buffer in place instead of reallocating
each step — both sound because the value is provably un-aliased. The full
argument, with the measurements and the places it costs, is
**[docs/thesis.md](docs/thesis.md)**.

## The testing campaign

The tools under [`tools/`](tools/) are not demos — they are full programs
written against the language, each with a ground-truth differential that a bug
cannot pass. The campaign ran all of them on the shipped compiler, and the
language held: no compiler or runtime defect was filed by any of them.

| program | what it stresses | its ground-truth gate |
| --- | --- | --- |
| `tycho-scheme` | a Scheme interpreter *and* a bytecode compiler (defunctionalized closures) | the same six programs run byte-identically on both; `make scheme-check` |
| `tycho-vm` | a bytecode assembler/disassembler/VM | `dis` round-trips `asm` byte-for-byte; 10 runtime traps; `make vm-check` |
| `tycho-kv` | a persistent B+ tree store | every command script byte-identical against a naive map backend; `make kv-check` |
| `tycho-chess` | bitboards, perft, alpha-beta search | published perft totals (start, Kiwipete, Position 3); `make chess-check` |
| `tycho-kvsrv` | a concurrent HTTP key-value server | a daemon gate: 4 parallel clients, every write intact; `make kvsrv-check` |
| `tycho-sat` | a DPLL/CDCL SAT solver | the pigeonhole theorem (PHP(2..9) unsat) and planted instances whose models the runner verifies clause by clause; `make sat-check` |

### Rigor and limits, in one place

A memory model that argues with decades of practice carries an astronomical
burden of proof, so the successes and the gaps belong on the same page rather
than one at the top and the other in a build note. Everything below is measured
on this tree; nothing is aspirational.

| What is proved | How | What is NOT covered, and where |
| --- | --- | --- |
| The model survives hard allocation patterns | a Scheme interpreter, a DPLL/CDCL SAT solver and a B+ tree store all run differentially against a ground truth a bug cannot pass — zero compiler or runtime defects filed | these are programs by this project's author; no outside program has stressed generics, `subscript` or `bounded[N]` yet ([ROADMAP §1](ROADMAP.md#1-someone-other-than-the-author-has-written-a-real-program)) |
| Emitted code is free of UB the optimizer and sanitizer disagree on | every fixture built twice, native `-O2` and `-fsanitize=address,undefined`, **byte-identical output required** between them, plus a golden | **macOS ships no LeakSanitizer**, so the leak lane and two sanitizer legs do not run there; Linux is the only host that scores leaks |
| The compiler fails closed on malformed input | a differential fuzzer over random programs, plus a reject corpus of 621 fixtures each pinning one refusal | |
| Five platforms execute, not just compile | `make platform-check` runs linux-x86_64, linux-arm64, macos-arm64, windows-x86_64 and windows-arm64 — 5 of 5 pass, 0 skipped, 0 uncovered | aarch64 Windows is a mingw target; a subnormal-literal defect there was a toolchain bug found by that lane and fixed |
| Locale-dependent float formatting is correct | `tests/float_lit_locale` and `float_str_locale` | they need a comma-decimal locale, so a **minimal Linux image must generate one** before `make test` — the tests are precise enough to catch a C library variation most suites never look at |
| Concurrency is sound | copy-in/copy-out semantics make data races inexpressible inside Tycho; TSan runs | a concurrent FFI call can still race — the FFI boundary is unsafe by design and **has had no external audit** ([SECURITY.md](SECURITY.md)) |
| Native Windows works | MSYS2 + mingw-w64, gated | MSYS2's `kill` terminates rather than signals, so a server's **wind-down is slower** there; nothing is lost or corrupted |

## Performance

On the allocation-heavy tree workloads Tycho uses the least memory of five
languages measured head-to-head — 40% of C's on binary-trees, half on
tree-rewrite — with no GC and no reference counting, only lexical arenas and
value semantics. A 220-line recursive JSON parser holds a flat 10 MB across
5,000,000 documents in a loop. The tables, the measurements, and the honest
costs are in **[docs/performance.md](docs/performance.md)**.

## FAQ

**"No GC and no borrow checker — how is it memory-safe?"** There is no
reference type, so a dangling pointer is *inexpressible* — the bug that escape
analysis exists to prevent can't be written. Memory frees per scope; values that
outlive their scope are copied up. `Option` removes null, `Result` removes
exceptions, indexing is bounds-checked, and copy-in/copy-out concurrency removes
data races inside Tycho (concurrent FFI calls can still race). Every test runs
under ASan + UBSan, plus ThreadSanitizer; LeakSanitizer too **on Linux**, which
is the only host that has it — Apple's ASan ships none, so macOS scores no
leaks. See the rigor-and-limits table above.

**"What does value semantics cost?"** No shared mutable references: you can't
build a shared-mutable graph, doubly-linked list, or observer the pointer way —
the idiom is a flat node pool (all nodes in one array, linked by integer index),
which is also the cache-friendly layout data-oriented engines choose on purpose.
Pointer-shaped data costs more measured — a recursive trie ~1.55× C's memory, a
fixed-capacity LRU ~2.8×; the flat-pool idiom brings the graph analog to ~1.3×
C. Arenas reclaim at scope exit, not incrementally. The full loss column is in
[docs/internals/value-semantics-limits.md](docs/internals/value-semantics-limits.md).

**"Deep-copying every value must be slow."** "Copied on assignment" is the
*semantic model*, not the generated code. The transpiler drops the copy wherever
a value is provably un-aliased — returns build in the caller's arena,
`acc = acc + x` grows in place, `b := a` becomes a move when `a` is dead. A copy
happens only when a value genuinely escapes to two live owners — exactly when a
GC or refcount would also work. Measured, not asserted: see the performance
tables.

**"Where's the package manager?"** There isn't one, on purpose. A package is a
directory of `.ty` files you import by path; the corelib lives under `core:`.
Adding third-party code is a deliberate manual act — vendor the source — never a
one-line command that pulls a transitive graph you've never read.

## License

Tycho is licensed under the **[MIT License](LICENSE)** — do whatever you want
with it. AI was used in building this language. It is provided "as is",
without warranty; security notes are in [SECURITY.md](SECURITY.md), and how
to build, test, or contribute is in [CONTRIBUTING.md](CONTRIBUTING.md).
