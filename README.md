<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="branding/tycho-logo-dark.svg">
    <img src="branding/tycho-logo.svg" alt="Tycho" width="128">
  </picture>
</p>

# Tycho

Tycho is a proof-of-concept systems language built to test one specific idea:
managing memory with **implicit hierarchical arenas and value semantics**, so
you get memory safety with no GC and no manual `free`. The transpiler is written
in C, turns Tycho source into C, and hands that C to your own C compiler to make
a native binary.

Each scope has its own memory arena — and loops get a scratch arena that resets
every iteration — cleared when the scope exits. Values move between arenas only
two ways: passed *down* into a sub-arena as an argument, or *promoted up* by
being returned. Under strict value semantics those values are deep-copied, never
aliased by reference, and that's what keeps them memory-safe. You never see any
of this: you declare and use values as if the language were dynamically managed,
and the transpiler inserts every allocation, promotion, and reclamation for you.
Behind the scenes it also applies a handful of low-level optimisations that cut
the performance tax usually charged for deep-copying values.

> **Status: experimental.** The aim of Tycho is *solely* to test the "implicit
> hierarchical arenas + value semantics" thesis — I don't recommend writing
> production software in it, and I won't pretend otherwise. There are **no
> stability guarantees** and the language may still change. That said, it
> self-hosts, it's fuzzed and benchmarked, and it builds with nothing but `cc` +
> `make`. The pillars — value semantics, implicit arenas, concurrency, generics,
> closures, UFCS, FFI, `sink` — all ship in both transpilers, so what's left now
> is ergonomics polish and bug reports, not new features. Experiments, questions,
> corrections, and feedback are all welcome — see
> [Getting started](#getting-started), [CONTRIBUTING](CONTRIBUTING.md), and the
> honest [Known limitations](#known-limitations-proof-of-concept).

Tycho is a statically typed, procedural systems language. The syntax is heavily
inspired by **Python** and **Nim**; the semantics sit closer to **Go** and
**Odin**. Most of the value-semantics ideas come from
**[Hylo](https://www.hylo-lang.org/)** — a huge shout-out to that project.

```
fn greet(name: string) -> string:
    return "hello " + name + "\n"

fn main():
    print("what is your name: ")
    name := input()
    print(greet(name))
```

Python-looking syntax; static types, value semantics, and explicit returns;
arena memory underneath. The same idea carries the concurrency story:
`spawn`/`wait`, `parallel for`, and channels are just the ordinary
copy-in/copy-out call convention run on threads — race-free by construction
*for pure Tycho values*, with no locks or lifetime rules in the language (see
[Concurrency](docs/reference/concurrency.md)). That guarantee covers the values
the transpiler owns; the moment you cross into C through the FFI you're back
under C's threading rules (see [FFI](docs/reference/ffi.md)). Types are inferred
bidirectionally (Pierce–Turner local inference): locals from their
initializers, literals/lambdas/bare empties from their destination — with every
type ground at its own line (see
[Type inference](docs/reference/types.md#type-inference-bidirectional)).

Why this works — value semantics is what lets the arenas be *implicit* — and
where it doesn't, with measured numbers, is written up in
[docs/thesis.md](docs/thesis.md).

```
$ make
$ ./tychoc examples/hello.ty
$ ./examples/hello
what is your name: Ada
hello Ada
```

## Getting started

**Prerequisites:** a C compiler (`cc` — GCC or Clang) and `make`. That's all —
the transpiler is a single dependency-free C file. *(Optional: `pkg-config` + a
library for the FFI-backed corelib modules such as `core:http` over libcurl, and
a Go toolchain for the cross-language benchmarks; both are skipped cleanly when
absent.)*

```
$ git clone https://github.com/StefanVonRanda/tycho
$ cd tycho
$ make                          # builds ./tychoc (the compiler)
```

**Run a program.** `./tychoc f.ty` transpiles `f.ty` to a `f.c` and compiles it to
a native binary `f`; `-o name` names the output, `--emit-c` stops at the C.

```
$ ./tychoc examples/hello.ty && ./examples/hello
what is your name: Ada
hello Ada
```

**Write your own** — save as `hi.ty`, then `./tychoc hi.ty && ./hi`:

```
fn main():
    for i in range(1, 4):
        println("line " + str(i))
```

**Use the standard library.** `corelib/` is tycho's stdlib, imported as
`core:<name>`. The transpiler finds it next to its own binary by default (`./tychoc`
sits beside `./corelib`), so there's no setup; set `TYCHO_CORELIB` to point at a
different corelib if you want to override that. A file with an `import` is a
*package*, so give it its own directory:

```
mysite/main.ty:
    package main
    import "core:strings"
    fn main():
        println(strings.to_upper("hello"))     # HELLO
```
```
$ ./tychoc mysite/main.ty && ./mysite/main
HELLO
```

Every corelib module has a runnable usage example under
[`examples/corelib/<name>/`](examples/corelib), and two larger programs *compose*
several modules end-to-end: [`examples/fetch`](examples/fetch) (an HTTP client —
GET, JSON-parse, hash, cache) and [`examples/site`](examples/site) (a static-site
generator). Build and verify the whole tree — transpiler, self-host fixpoint, tests,
corelib, fuzzer — with `make ci`.

**Where to go next:** the [language reference](docs/reference/index.md) describes every
construct; [docs/corelib.md](docs/corelib.md) catalogs the standard library; and
[docs/thesis.md](docs/thesis.md) explains the arena memory model underneath it all.

## Building

| Command | What it does |
| --- | --- |
| `make` | Build the `./tychoc` transpiler (native host). |
| `./tychoc f.ty` | Transpile `f.ty` → `f.c`, then compile to native `f` with `cc`. |
| `./tychoc f.ty --emit-c` | Only write `f.c`. |
| `./tychoc f.ty -o name` | Write `name.c` and binary `name`. |
| `make demo` | Build and run `examples/hello.ty`. |
| `make test` | Run the test suite (see below). |
| `make test-update` | Re-record the expected-output goldens (review the diff). |
| `make bench` | Run the performance guard (see below). |
| `make bootstrap` | Build the self-hosted transpiler `compiler/tychoc0.ty` with `./tychoc` and validate it on its fixtures. |
| `make fixpoint` | Self-host check: assert the self-hosted transpiler reproduces its own emitted C byte-for-byte, and produces the same program output as the C transpiler. |
| `make fuzz` | Differential + ASan/UBSan soundness fuzzer: generate random well-typed programs, compile with both transpilers, assert byte-identical output and no sanitizer fault. |
| `make corelib` | Build + validate the standard library (`corelib/`) three ways (the C transpiler vs the self-hosted `tychoc0`, against goldens), with usage examples and the `site` dogfood. |
| `make ci` | The full local gate (no cloud CI): build, test, fixpoint, corelib + examples, concurrency, FFI, the three fuzz lanes, tooling, and the perf guard. |
| `make clean` | Remove build artifacts. |

`make test` builds every `examples/*.ty` and `tests/*.ty` program twice — a
native `-O2` binary and one under `-fsanitize=address,undefined` — runs both on
the same stdin, and asserts four things: exit 0, no sanitizer report,
**byte-identical output** between the two builds, and that the output matches
the committed golden `tests/<name>.out`. The byte-identical check catches
undefined behaviour the optimizer and the sanitizer disagree on; the golden
check catches the rest — a miscompile that produces the *same* wrong output in
both builds (it agrees with itself, just wrongly) only shows up against the
recorded correct output. Leak detection is on too: every scope frees its arena
at exit (including `main`'s), so a LeakSanitizer report means a real missing
free (e.g. an early `return` that skipped a loop's scratch arena). Goldens are
rewritten only by `make test-update` — never by a normal run — so a regression
can't silently rebake itself into the expected files. This is the standard
described in [docs/thesis.md](docs/thesis.md) §3, now wired as a target.

`make bench` guards the *performance* claims the way `make test` guards
correctness. Each `bench/*.ty` program exercises one optimization and asserts a
single metric against a deliberately generous bound — peak RSS for the
memory-shape claims (in-place string append, loop scratch reset, the map
accumulator, and move-on-last-use) and wall time for the `mut` memo. The
bounds catch order-of-magnitude regressions, not jitter: the in-place append
holds ~1.5 MB where the un-optimized path is ~825 MB at the same N, so the 32 MB
bound sits firmly between a working and a broken optimization; likewise the
`move` bench holds ~126 MB where deep-copying the dead local would be ~187 MB.

**Platform notes.** All you really need is a working C compiler (`cc` — GCC or
Clang) and `make`. Tycho builds and self-hosts on any unix-like OS — I've tested
it on **Debian, Arch, and macOS** (Apple Silicon and Intel). I didn't bother
with native Windows, but WSL should work fine. On macOS, install the Xcode
Command Line Tools (`xcode-select --install`); one difference worth knowing is
that Apple's AddressSanitizer ships no LeakSanitizer, so the leak-detection half
of the sanitizer builds is skipped there — the correctness, UBSan, and
byte-identical checks all still run.

## Documentation

- **[Language reference](docs/reference/index.md)** — the reference for every
  construct, organized by topic (types, aggregates, enums, functions, generics, concurrency,
  FFI, packages, builtins). It's the source of truth for how the language behaves; every
  example in it compiles on both transpilers.
- **[The thesis](docs/thesis.md)** — why value semantics makes implicit arenas work, and where
  it doesn't, with measured numbers. The argument the language exists to make.
- **Design notes** (`docs/*.md`: the arena memory model, concurrency, FFI, generics, maps, …) —
  the *rationale* behind each subsystem. The reference links to the relevant note from each page;
  these explain why, the reference explains what.
- **Internals & status** — [`STATUS.md`](STATUS.md) maps the architecture, the verification
  gates, and what's shipped; `docs/internals/` holds the design records. Start here to evaluate
  or contribute.

**Where to start, by goal.** *Learn the language* → the learning guide, then the reference.
*Understand the design* → the thesis and the design notes. *Evaluate or contribute* →
[`STATUS.md`](STATUS.md) and [CONTRIBUTING](CONTRIBUTING.md).

## Self-hosting

Besides the C reference transpiler (`src/tychoc.c`), Tycho has a second
transpiler **written in Tycho itself**: `compiler/tychoc0.ty`. It started as a
subset of the language just large enough to compile its own source, and it
**self-hosts** — `make fixpoint` builds it three ways (the C transpiler builds
it, then that build rebuilds it, then that rebuild rebuilds it again) and
asserts two things: the last two emit *byte-identical* C — so the self-hosted
transpiler reproduces its own output exactly — and the self-hosted build
produces the same program output as the C transpiler across every `tests/` and
`examples/` program. Run `make bootstrap` to build the self-hosted transpiler
and `make fixpoint` to check it.

The fixpoint is also the language's parity discipline: **every feature lands in
both transpilers or not at all** — concurrency, bidirectional inference,
packages, closures, FFI all exist twice (the two transpilers emit different C
dialects with identical semantics), and shared test fixtures run through both
via the fixpoint differential, so the two can't drift apart.

Since reaching the fixpoint, I've migrated `tychoc0`'s codegen from naive
malloc/leak C to the same implicit-arena memory model the C transpiler uses —
one type family at a time (strings, arrays, maps, structs/tuples/boxes, every
array element type, enum node trees, mutable containers, per-variable block
scoping, transient placement, move-on-last-use, and finally heap-payload
option/result elements), all now arena-managed and freed per scope. It's
documented in [docs/memory-model.md](docs/memory-model.md). As far as I can
tell `tychoc0` now matches the C transpiler's memory behaviour across every
element type, common and rare, with no known gap, and has full codegen-feature
parity.

The language `tychoc0` compiles is no longer a strict subset: its program output
matches the C transpiler's byte-for-byte across all `tests/` + `examples/`
programs, including the additive `char` type, `float`, `Result`, newtypes,
slices, and SOA. A differential + sanitizer **fuzzer** (`fuzz/`, type-directed
random programs compiled by both transpilers under ASan/UBSan) backs this up,
with coverage spanning those types.

**Self-compile speed.** When `tychoc0` compiles its own source, the dominant
remaining cost is the value-semantic string-building in its codegen: each
function deep-copies its result string on return. That is the floor set by the
memory model, not a logic-level inefficiency, and no algorithmic change removes
it; it is measured in [docs/perf.md](docs/perf.md). The repository includes a
small sampling profiler (`tools/prof/`) used for this analysis, since `perf` is
sandbox-blocked and `gprof` mis-attributes time on tiny, frequently-called
functions.

**Head-to-head (`bench/prongB/`, [RESULTS.md](bench/prongB/RESULTS.md)).** The
same program in five languages, built optimized, peak RSS + best-of-3 wall time;
every binary prints byte-identical output. `tycho (tychoc0)` is the self-hosted
transpiler after the migration:

| workload         | tycho (tychoc0) |        C |     Rust |   Go (GC) | Koka (Perceus) |
| ---------------- | ------------: | -------: | -------: | --------: | -------------: |
| binary-trees     |  13 MB/107 ms | 33/765 ms | 34/855 ms | 32/1756 ms |      15/269 ms |
| tree-rewrite     |   6 MB/89 ms  | 13/556 ms | 10/404 ms |  21/837 ms |       8/178 ms |
| array-pipeline   |    5 MB/30 ms |  3/22 ms |  3/23 ms |   6/53 ms |      18/372 ms |
| string-pipeline  |    2 MB/1 ms  |   1/1 ms |   2/2 ms |    4/5 ms |       2/17 ms |

(Numbers are the standard-optimization measurement recorded in RESULTS.md — that
file is authoritative; regenerate with `sh bench/fair_full.sh`. Absolute times are
machine-specific, so the cross-language *ratios and directions* are the claim,
not the millisecond counts: the reference toolchains are in RESULTS.md (gcc 15.2
/ rustc 1.93 / go 1.26 / koka 3.2.3), and the directions reproduce independently
on an **AMD Ryzen 7 7735HS, x86-64 Linux** box and an **Apple Silicon arm64,
macOS** box — the latter ~3–4× slower in wall time.) On the allocation-heavy
tree workloads the
self-hosted transpiler is competitive with all four reference languages on this
machine. On binary-trees it has the lowest memory of the five (13 MB, below
Koka's 15) and the lowest wall time (107 ms), at equal memory to the mature
`tychoc` — with no GC and no reference counting, only lexical arenas and value
semantics. On tree-rewrite it is the fastest of the five and lightest on x86-64,
though Rust edges its memory by ~2 MB on arm64 (the allocator and target shift
that margin; tycho leads on time on both). With the additive `char` type, the
string-pipeline reaches C's 1 ms: `s = s + ('0' + d)` is an in-place one-byte
append — the same byte-write C, Rust, and Go do — instead of allocating a
one-char string per digit. On memory it is lowest of the five on binary-trees and
within ~1–2 MB of the leader on the other workloads, trailing C and Rust only on
array-pipeline time (per-element bounds-checking, not the memory model). The
transpiler-vs-generated-code analysis is in [docs/perf.md](docs/perf.md).

**Realistic workload (`bench/site/`, [RESULTS.md](bench/site/RESULTS.md), `make
bench-site`).** The same static-site generator — render *N* Markdown pages to
HTML — written in tycho / C / Go, an FNV checksum of every rendered byte gating
that all three do identical work. It scales the `examples/site` dogfood down to
its allocation-heavy core, so it measures the memory model on a real composing
workload rather than a micro-benchmark:

| pages  |  tycho  |   C    |  Go (GC) |
| -----: | -----: | -----: | -------: |
|  1,000 | 1.5 MB | 1.5 MB |   6.9 MB |
|  5,000 | 1.5 MB | 1.3 MB |   7.8 MB |
| 20,000 | 1.5 MB | 1.6 MB |   8.5 MB |

tycho's peak RSS is **flat ~1.5 MB across a 20× scale** — each page is rendered in
a loop-body arena reclaimed every iteration, so the working set is one page
regardless of *N*, matching C with no manual `free` and no GC, where Go grows to
~5× (runtime floor + GC headroom). The honest trade-off: on this workload tycho is
~2× C on wall-clock (its string-rebuild vs C's `realloc` buffer; Go between) — the
arena's win here is memory, not raw speed.

## Memory model

This isn't only a benchmark story. `examples/json.ty` is a full recursive-descent
JSON parser + serializer (~220 lines): a recursive `Json` sum type, parsed by
recursive descent and walked to serialize and query — real systems code with real
recursion and **zero** `malloc`/`free`/refcount/GC in the source. It runs clean
under AddressSanitizer + LeakSanitizer, and parsing **5,000,000** documents in a
loop holds at a flat **10 MB** — each document's tree is reclaimed when its loop
iteration's arena resets. That is the model below, on a real workload:

Every scope — each proc, each `if`/`else` block, each loop body — gets its
own **arena** with its own backing storage. Arenas form a hierarchy via
`arena_child`. Data moves between arenas exactly two ways, and the
transpiler arranges both for you:

1. **Down**, as a function argument: a pointer is passed to a callee whose
   arena is a child, so it stays valid for the call. No copy.
2. **Up**, by being returned: the value is allocated in the caller's
   (parent) arena, so it survives the callee's arena being freed.

Assigning a value to a variable that lives in an outer scope is handled
the same way — the transpiler allocates the value in *that variable's*
arena, so it never dangles. This is what makes string accumulation across
a loop safe:

```
total := ""
for i in range(1, 6):
    total = total + str(i)   # allocated in main's arena, survives the
                             # loop's scratch arena being reset
```

- A proc's arena is freed when it returns.
- An `if`/`else` block's arena is freed when the block ends.
- A loop's **scratch arena is reset every iteration**, so per-iteration
  temporaries (e.g. `print(str(i) + " ")`) keep loop memory bounded —
  a million such iterations run in constant memory.

Two optimizations keep this cheap without making the model visible. A **return-slot
move**: a value built locally and returned is allocated in the caller's arena from the
start, so the `return` is a move, not an O(n) copy — and it composes across frames (a
value returned up several levels is built once, in the final consumer's arena). And
**in-place append**: a self-append `acc = acc + e` in a loop grows `acc`'s buffer in place,
turning the textbook O(n²) accumulator into O(n) (measured at n=40 000: ~828 MB → under
4 MB). Both are sound *because* value semantics already proves the value is uniquely owned
at that point — no analysis, no annotation. The full argument, with the numbers and the
honest account of where the model trades — and how to shape data so it stays fast — is
[the thesis](docs/thesis.md). None of it appears in Tycho source.

### Known limitations (proof-of-concept)

- **No shared mutable references** — value semantics makes every binding an
  independent deep copy; there are no pointers or references, and recursive
  struct types are rejected. So you cannot build shared-mutable **graphs**,
  **doubly-linked lists**, or **observer patterns** the way a pointer language
  does. The Tycho idiom is a **flat node pool**: hold all nodes in one `[Node]`
  array and link them by *integer index*, not by reference (optionally a
  generational index for use-after-free detection). The whole structure is then
  one value with one arena lifetime — see
  [docs/arrays-structs.md](docs/arrays-structs.md) §2 and the learning guide's
  "Graphs and Linked Structures".
- **Operational costs of the model, measured** — because value semantics stores
  children by value, pointer-shaped structures cost more than their pointer
  representation: a by-value recursive trie is **~3.2× C's memory**, a
  fixed-capacity LRU ~5× (no sharing). The flat-pool idiom above brings the
  graph analog to ~1.3× C. Arenas reclaim at *scope exit*, not incrementally,
  so a long-lived scope holds transients until it returns — scope them in an
  inner function. The full measured loss column, with the idiom and decision
  guide for each case, is in
  [docs/internals/value-semantics-limits.md](docs/internals/value-semantics-limits.md).
- **Generic constraints are a fixed, built-in set** — generic *functions*,
  *structs*, and *enums* (all including recursive types, e.g. `enum Tree($T)`)
  take `$T` and are monomorphized, but the only constraints are the built-in
  predicates (`numeric` / `comparable` / `has_str`) and type sets
  (`where T: int | float`). There are no user-defined traits/type-classes, no
  higher-kinded types, and no variance — see [docs/generics.md](docs/generics.md).

## Repository layout

```
src/tychoc.c        the C reference transpiler (lexer, parser, type resolver, C codegen)
runtime/tycho_rt.c  the arena runtime, embedded verbatim into every output
compiler/tychoc0.ty the self-hosted transpiler, written in Tycho (see make fixpoint)
compiler/run.sh, fixpoint.sh   bootstrap + self-host fixpoint harnesses
build/             generated embed header (make artifact)
examples/          hello, demo, accumulate, accumulate_big, arrays,
                   array_fns, structs, strings, words, wordcount, records,
                   inout, memo, collect, context, generics_tour, optimize, json,
                   raytrace, grep, invindex, triepool (.ty) — 22 programs (generics_tour.ty
                   tours `$T` functions/structs/enums, incl. a recursive
                   `Tree($T)`; json.ty is a full recursive-descent JSON parser +
                   serializer; raytrace.ty a float-math PPM renderer; grep.ty a
                   CLI text tool over args()/read_file; invindex.ty an
                   inverted-index text search engine)
examples/corelib/  a runnable usage example for every corelib module (make corelib-examples)
examples/fetch/    composing dogfood — an HTTP client (http + json + sha256 + io + path)
examples/site/     composing dogfood — a static-site generator (8 corelib modules)
corelib/           the standard library, imported as `import "core:<name>"` (make corelib)
tests/run.sh       test harness (native -O2 vs ASan/UBSan, + golden output)
tests/*.ty         dedicated regression programs, one per feature/bug (+ tests/pkg/ package fixtures, + optional <name>.in stdin)
tests/*.out        recorded expected output (goldens) for every test program
tests/conc/        concurrency suite: spawn/parallel-for/channels/select under
                   native + ASan/LSan + TSan, tychoc0 parity differential,
                   reject + abort fixtures (make conc; part of make ci)
bench/conc/        concurrency head-to-head vs C/Go/Rust (make bench-conc)
bench/run.sh       performance guard (peak RSS / time bounds per optimization)
bench/*.ty         one benchmark program per optimization (17); bench/peakrss.c helper
bench/prongB/      cross-language benchmark suite (Tycho vs C, Go, Rust, Koka) + RESULTS.md
bench/site/        realistic-workload head-to-head: static-site render, Tycho vs C vs Go (make bench-site)
fuzz/              differential + ASan/UBSan soundness fuzzer (gen.py + run.py; make fuzz)
tools/prof/        dependency-free sampling CPU profiler for tycho-compiled binaries
docs/thesis.md     why value semantics makes implicit arenas work (+ limits)
docs/arrays-structs.md   the original aggregates design pressure-test
docs/memory-model.md   how the self-hosted transpiler runs on the implicit-arena model
docs/perf.md       transpiler + generated-code performance, incl. the cross-language benchmark suite
```

The runtime is turned into a C string literal at build time (`make`
generates `build/tycho_rt_embed.h` from `runtime/tycho_rt.c`) and prepended
to every generated `.c`, so output files are self-contained.

## FAQ

**"It just transpiles to C — that's not a real compiler."** Emitting C is a
deliberate backend choice, not a shortcut. It makes Tycho portable to any target
with a C compiler and lets it inherit decades of mature optimization and tooling
for free — C is the *assembler* here. The transpiler still does all the real
work: lexing, parsing, bidirectional type inference, the implicit-arena escape
analysis, monomorphization, and the static reuse analysis (move-on-last-use,
in-place append, FBIP-style buffer recycling) — none of which C does for you. The
output is self-contained (the runtime is prepended to every file), so there's no
hidden dependency. It's the same strategy several production languages use, and
the benchmarks above are against C, Rust, Go, and Koka *binaries*, not against
other transpilers.

**"Deep-copying every value must be slow."** "Everything is a value, copied on
assignment" is the *semantic model*, not what the generated code actually does at
runtime. The transpiler proves, statically, when a copy is unnecessary and drops
it: returns are built directly in the caller's arena (no copy out), `acc = acc + e`
grows one buffer in place, `b := a` becomes a move when `a` is dead, `match` arms
borrow the scrutinee's payload, and loop-carried rebuilds hand off their buffer.
A copy only happens when a value genuinely escapes to two live owners — which is
exactly when a GC or refcount would also do work. And it's measured, not just
asserted: on the allocation-heavy tree workloads it has the lowest memory and
time of the five languages benchmarked, it matches C-pthreads on the parallel
reduction, and it has the fastest channel pipeline of the four — see
[bench/](bench/) and [docs/thesis.md](docs/thesis.md).

**"No GC and no borrow checker — how is it memory-safe?"** There's no reference
type in the language at all, so a dangling pointer is *inexpressible* — the bug
other region systems ship escape analysis to prevent simply can't be written.
Memory is freed per scope by the arena hierarchy; values that outlive their scope
are copied up. `Option` removes null, `Result` removes exceptions, array and slice
access is bounds-checked, and copy-in/copy-out concurrency removes data races by
construction. Every test runs under AddressSanitizer + UndefinedBehaviorSanitizer
(plus LeakSanitizer and, for concurrency, ThreadSanitizer), and a differential
fuzzer cross-checks both transpilers.

**"Is it production-ready?"** No. Tycho is **experimental, proof-of-concept**
software exploring one idea (implicit hierarchical arenas under value semantics).
It has a single implementation, the language surface is still moving, and there
is no stability guarantee. Use it to learn, experiment, and give feedback — not
to ship a service.

**"Where is the package manager?"** There isn't one, on purpose — Tycho does not
automate dependency hell. A package is a directory of `.ty` files you import by
relative path, and the standard library lives under `core:`. Adding third-party
code is a deliberate, manual act — vendor the source into your tree — never a
one-line command that pulls in a transitive graph you have never read. System C
libraries are linked explicitly through your own build via the FFI.

## License

Tycho is licensed under the **[MIT License](LICENSE)** — do whatever you want
with the code. AI was used in building this proof-of-concept, so the whole repo
goes out under MIT. It's experimental software provided "as is", without
warranty — security notes are in [SECURITY.md](SECURITY.md), and how to build,
test, contribute, or report a bug is in [CONTRIBUTING.md](CONTRIBUTING.md).
