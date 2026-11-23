<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="branding/tycho-logo-dark.svg">
    <img src="branding/tycho-logo.svg" alt="Tycho" width="128">
  </picture>
</p>

# Tycho

**A general-purpose systems language where you write as if memory were managed
and it runs like hand-placed arenas.** It is memory-safe by construction outside
FFI: there is no reference type, so a dangling pointer cannot be written. Every
scope owns a memory arena, freed when the scope exits, and the compiler sees
every value's lifetime from the syntax alone and inserts every allocation and
free itself. No garbage collector, no manual `free`, no borrow checker.

The syntax is terse, and the idioms that replace references — pools, flat
arrays, closed enums — are the flat, index-addressed layouts that data-oriented
C reaches for on purpose, so the natural way to write Tycho is also the fast
one. That holds from parsers, interpreters and CLIs to long-running services
and 3D games. It transpiles to C and builds with `cc` and `make`.

**Not for you if** you need shared-mutable graphs, interface values, or 1.0
stability — see [what you give up](#what-you-give-up) below and the version
policy in [SUPPORT.md](SUPPORT.md).

[Docs](docs/README.md) · [Quickstart](docs/quickstart.md) ·
[Tutorial](docs/tutorial.md) · [Reference](docs/reference/index.md) · [Thesis](docs/thesis.md) ·
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

That last comment is the whole language. `evens` returns `xs`, so there is
nothing to free at its scope exit and the array is built in the caller's arena
from the start (the grey boxes are the emitted C; the sentences carry the point):

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

No annotation chose that, and no runtime worked it out.

## Quick start

**You need `make` and a C11 compiler — gcc, or clang 15 or newer.**

```
$ git clone https://github.com/StefanVonRanda/tycho
$ cd tycho
$ make                                  # builds ./tychoc
$ ./tychoc examples/hello.ty && ./examples/hello
what is your name: Ada
hello Ada
```

clang 14 is excluded: it builds the compiler and then miscompiles what it
emits ([toolchain matrix](docs/platforms.md#c-toolchains)). Full build details
are under [Trying it](#trying-it). The syntax is Python/Nim-flavored and the
semantics Go/Odin-like; the value-semantics core comes from
**[Hylo](https://www.hylo-lang.org/)**.

## What you give up

Writing as if memory were managed, and getting arena performance for it, has a
price. Here it is up front rather than on page 40.

**No reference type.** That is what makes the demo above work, and it is also
why a shared mutable graph, a doubly-linked list, a scene graph whose children
point back at their parent, or an observer holding a pointer to its subject
cannot be written at all. They are not discouraged; there is no way to write
them. If your mental model is C's, the first complex data structure you reach
for will not compile, and the compiler will look broken when it is the model
that changed.

**No interface values.** A trait object is a pointer to someone else's value
plus a vtable, and that pointer is exactly the aliasing the model forbids. A
Go or Java `interface` in a struct field, or a heterogeneous list of
implementors, cannot exist here. That is a boundary of the model, not a feature
still to come ([architecture](docs/architecture.md#decided-non-goals)).
Generics cover the static half: one function works over many types, but it
cannot hold two different ones under one name at the same time.

**What you write instead** is in [Thinking in Tycho](docs/idioms.md): a flat
pool for graphs (every node in one array, each edge an integer index into it),
channels for observers, closed enums for heterogeneity. Each idiom is also the
faster spelling, the layout a data-oriented C engine reaches for on purpose,
made the default.

**Memory safety stops at FFI.** `extern fn` calls into C, and C can do anything
([SECURITY.md](SECURITY.md)). That includes the hand-written C behind corelib
packages such as `gfx`, `sqlite`, `tls` and `http`.

**It costs some RAM.** Value storage uses more peak memory than C on some rows:
a sliding int window **~1.44×**, a fixed-capacity LRU **~1.22×**, JSON
**~1.14×** ([bench/RESULTS.md](bench/RESULTS.md#relative-peak-rss-at-the-large-size)).
The trie and the graph, written as flat pools, land at or under C. Arenas also
reclaim at scope exit, not incrementally, so a long-lived scope holds its
transient values until it returns. The full list of losses, a worked pool
sample, and the workloads that want a different tool are in
**[docs/internals/value-semantics-limits.md](docs/internals/value-semantics-limits.md)**.

> **Status: 0.9.2 — pre-1.0. Expect breaking changes.** Until 1.0, the
> language surface and the corelib API can change with a changelog entry
> and no deprecation window. The [spec](docs/spec/) describes the current
> behavior. Performance numbers, emitted C shape, and arena sizes are
> explicitly unstable, as are areas the spec or [SECURITY.md](SECURITY.md)
> mark as sharp edges. See [SUPPORT.md](SUPPORT.md) for the version policy
> and [ROADMAP.md](ROADMAP.md#what-10-requires) for what 1.0
> requires. Versioning is `tychoc --version` + [CHANGELOG.md](CHANGELOG.md).

## Key features

- **No GC, no manual `free`.** A value can leave a scope in exactly two ways,
  both visible in the source — *down*, passed to a callee, or *up*, returned to
  the caller — so the compiler places every allocation from the syntax alone.
- **Value semantics.** `b := a` copies; there is no shared mutable storage, so
  data races are inexpressible inside Tycho (copy-in/copy-out concurrency, no
  annotations) — concurrent FFI calls can still race ([SECURITY.md](SECURITY.md)).
- **Compact fixed-size enums.** The compiler stores a user enum with scalar
  payloads as a packed tag-plus-union value; recursive and heap-bearing enums
  remain boxed. `TYCHOC_NO_INLINE_ENUM=1` is the layout control.
- **One maintained compiler.** The compiler is written in Tycho and builds
  itself. `bootstrap/tycho_compiler.c` is a generated cold-start artifact; the
  only toolchain is `cc` and `make`.
- **Concurrency, generics, closures, FFI.** `spawn`/`wait` tasks, bounded
  channels, `parallel for`, monomorphized generics, closures, UFCS, and a
  C-interop boundary.
- **Heavily tested.** Every example is built twice — native and sanitized — and
  checked against a committed golden; a fuzzer applies the same differential to
  random programs under ASan/UBSan. Six full programs (below) run against
  ground-truth differentials.

## Trying it

`./tychoc f.ty` transpiles `f.ty` to C and compiles it to a native binary `f`
(`./tychoc --help` lists every flag). The only optional extras are `pkg-config`
plus a library for the FFI-backed corelib modules (like `core:http`) and a Go
toolchain for the cross-language benchmarks — both skip cleanly when absent.

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
[`examples/site`](examples/site) (static-site generator).

### Building

| Command | What it does |
| --- | --- |
| `make` | Build the `./tychoc` transpiler. |
| `./tychoc f.ty` | Transpile to C, compile to native `f`; the intermediate `f.c` is removed on success, kept on a `cc` failure. |
| `./tychoc f.ty --emit-c` / `-o name` | Stop at the C (to stdout; `-o name` writes `name.c`) / name the output. |
| `make check-fast` | The per-change gate: worker-pool tests, parser/typechecker sweep, links, corelib, debugger, surface and doc counts, plus a 20-seed fuzz smoke (~5 min). |
| `make test` | The authoritative suite, in parallel. Use it for compiler, emitter or bootstrap changes. Needs the sanitizer runtimes — see [CONTRIBUTING](CONTRIBUTING.md#what-the-gate-needs-beyond-that). |
| `make bench` | Run the performance guard (below). |
| `make fuzz` | Differential + ASan/UBSan soundness fuzzer, 200 seeds (nightly depth). `make fuzz-quick` is the 20-seed change smoke. |
| `make corelib` | Build + validate the corelib against its goldens. |
| `make ci` | The release gate: exhaustive local sweep for nightly, pre-merge and release — not per change. No cloud CI. |
| `make ci-release` | The release depth: `make ci` with at least 1000 fuzz seeds (refuses less). |
| `make release-check` | Build and smoke-test the current-version tarball twice; require byte-identical archives. |
| `make clean` | Remove build artifacts. |

`make test` builds every `examples/*.ty` and `tests/*.ty` twice — native `-O2`
and `-fsanitize=address,undefined` — runs both on the same stdin, and asserts:
exit 0, no sanitizer report, **byte-identical output** between the builds, and a
match against the committed golden `tests/<name>.out`. Goldens are rewritten
only by `make test-update`. What each check proves is in
[Architecture](docs/architecture.md).

`make bench` guards the *performance* claims the way `make test` guards
correctness: each `bench/*.ty` asserts one metric against a generous bound.

**Platforms.** Every row below is *gated* — `make ci` or `make test` green on
that platform, executed there, not cross-compiled and hoped for.

| platform | status |
| --- | --- |
| **Linux x86-64** | gated — the supported platform ([SUPPORT.md](SUPPORT.md)), and the only one that scores leaks |
| **Linux arm64** | gated — Ubuntu 26.04 / aarch64 |
| **macOS / Apple Silicon** | gated — `xcode-select --install` is the whole setup; no LeakSanitizer |
| **Windows x86-64** | gated natively under MSYS2 + mingw-w64; **WSL2** needs no setup and behaves exactly like Linux |
| **Windows arm64** | executed by `make platform-check`; a mingw target |

**On a minimal Linux image, generate a comma-decimal locale first:**
`sudo locale-gen da_DK.UTF-8` (or `de_DE.UTF-8` / `fr_FR.UTF-8`). Two
float-formatting fixtures need a locale whose decimal point is not `.`, and a
stock container ships only `C`/`POSIX`/`en_US`.

The optional FFI-backed corelib packages need their dev libraries (`zlib1g-dev`,
`libssl-dev`, `libcurl4-openssl-dev`, `libpng-dev`, `libsqlite3-dev`,
`pkg-config`), without which `make shim-warn` refuses to run those checks
instead of passing without testing anything.

Which lanes skip on which host and why, the measured C toolchains, and the
Windows differences are in **[docs/platforms.md](docs/platforms.md)**. MSVC is
not a supported C target.

## Documentation

[`docs/`](docs/README.md) is the full index; the map:

- **[Quickstart](docs/quickstart.md)** — clone, build, run, ten minutes.
- **[Tutorial](docs/tutorial.md)** — the guided first hour, ending in a small real program.
- **[From `malloc` to implicit arenas](docs/from-c-to-arenas.md)** — the memory
  model in five steps. The gentlest way in, with or without C.
- **[Language reference](docs/reference/index.md)** — every construct, by topic.
  The source of truth; every example compiles.
- **[Quiet results](docs/quiet-results.md)** — the seven places the language
  answers instead of refusing (bytes vs characters, clamping
  slices, wraparound, the lax parsers). Each has a stricter sibling. Read it
  before trusting a parse.
- **[The thesis](docs/thesis.md)** — the argument: why value semantics makes
  implicit arenas work, and where it doesn't, with measured numbers.
- **[The memory model](docs/memory-model.md)** — the practitioner's view: what
  the compiler emits and what to expect in practice.
- **[Performance](docs/performance.md)** — the measurements behind the claims.
- **[Architecture & status](docs/architecture.md)** — how it's built, what each
  verification gate proves, what's shipped, and the decided non-goals.

That is everything needed to *use* Tycho. What follows is the case that it works.

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

The tools under [`tools/`](tools/) are full programs written against the
language, each with a ground-truth differential that a bug cannot pass. All six
below were written by the project's author; none filed a compiler or runtime
defect.

| program | what it stresses | its ground-truth gate |
| --- | --- | --- |
| `tycho-scheme` | a Scheme interpreter *and* a bytecode compiler (defunctionalized closures) | the same six programs run byte-identically on both; `make scheme-check` |
| `tycho-vm` | a bytecode assembler/disassembler/VM | `dis` round-trips `asm` byte-for-byte; 10 runtime traps; `make vm-check` |
| `tycho-kv` | a persistent B+ tree store | every command script byte-identical against a naive map backend; `make kv-check` |
| `tycho-chess` | bitboards, perft, alpha-beta search | published perft totals (start, Kiwipete, Position 3); `make chess-check` |
| `tycho-kvsrv` | a concurrent HTTP key-value server | a daemon gate: 4 parallel clients, every write intact; `make kvsrv-check` |
| `tycho-sat` | a DPLL/CDCL SAT solver | the pigeonhole theorem (PHP(2..9) unsat) and planted instances whose models the runner verifies clause by clause; `make sat-check` |

### Rigor and limits, in one place

Successes and gaps, together. Everything below is measured on this tree.

| What is proved | How | What is NOT covered, and where |
| --- | --- | --- |
| The model survives hard allocation patterns | a Scheme interpreter, a DPLL/CDCL SAT solver and a B+ tree store all run differentially against a ground truth a bug cannot pass — zero compiler or runtime defects filed | these are programs by this project's author; no outside program has stressed generics, `subscript` or `bounded[N]` yet ([ROADMAP §1](ROADMAP.md#1-someone-other-than-the-author-has-written-a-real-program)) |
| Emitted code is free of UB the optimizer and sanitizer disagree on | every fixture built twice, native `-O2` and `-fsanitize=address,undefined`, **byte-identical output required** between them, plus a golden | **macOS ships no LeakSanitizer**, so the leak lane and two sanitizer legs do not run there; Linux is the only host that scores leaks |
| The compiler fails closed on malformed input | a differential fuzzer over random programs, plus a reject corpus of over 650 fixtures each pinning one refusal | |
| Five platforms execute, not just compile | `make platform-check` runs linux-x86_64, linux-arm64, macos-arm64, windows-x86_64 and windows-arm64 — 5 of 5 pass, 0 skipped, 0 uncovered | aarch64 Windows is a mingw target; a subnormal-literal defect there was a toolchain bug found by that lane and fixed |
| Locale-dependent float formatting is correct | `tests/float_lit_locale` and `float_str_locale` | they need a comma-decimal locale, so a **minimal Linux image must generate one** before `make test` |
| Concurrency is sound | copy-in/copy-out semantics make data races inexpressible inside Tycho; TSan runs | a concurrent FFI call can still race — the FFI boundary is unsafe by design and **has had no external audit** ([SECURITY.md](SECURITY.md)) |
| Native Windows works | MSYS2 + mingw-w64, gated | MSYS2's `kill` terminates rather than signals, so a server's **wind-down is slower** there; nothing is lost or corrupted |

## Performance

On the allocation-heavy tree workloads Tycho uses the least peak memory of the
five languages in [STATUS](STATUS.md#evidence-for-the-arena-model) — 40% of
C's on binary-trees, half on tree-rewrite — with no GC and no reference
counting. Across the generated suite, Tycho's service-category time aggregate
is 0.71× C, 0.50× Go, 0.48× Java and 0.35× Swift; the rows where it is slower
are named under the tables, and against expert C/Rust arena controls the tree
advantage shrinks, so read the row-level table rather than a headline. Parsing
5,000,000 JSON documents in a loop holds a flat ~10 MB, and big workloads hold
their live set within 0.1–1.3% of what they reserve from the OS. The suite is
**[bench/RESULTS.md](bench/RESULTS.md)**; the analysis and costs are in
**[docs/performance.md](docs/performance.md)**.

## FAQ

**"No GC and no borrow checker — how is it memory-safe?"** There is no
reference type, so a dangling pointer is *inexpressible* — the bug that escape
analysis exists to prevent can't be written. Memory frees per scope; values that
outlive their scope are copied up. `Option` removes null, `Result` removes
exceptions, indexing is bounds-checked, and copy-in/copy-out concurrency removes
data races inside Tycho (concurrent FFI calls can still race). Every test runs
under ASan + UBSan and ThreadSanitizer, plus LeakSanitizer on Linux. What value
semantics costs is [what you give up](#what-you-give-up), above.

**"Deep-copying every value must be slow."** "Copied on assignment" is the
*semantic model*, not the generated code. The transpiler drops the copy wherever
a value is provably un-aliased — returns build in the caller's arena,
`acc = acc + x` grows in place, `b := a` becomes a move when `a` is dead. A copy
happens only when a value genuinely escapes to two live owners — exactly when a
GC or refcount would also work.

**"Where's the package manager?"** There isn't one, on purpose. A package is a
directory of `.ty` files you import by path; the corelib lives under `core:`.
Adding third-party code is a manual act — vendor the source — never a
one-line command that pulls a transitive graph you've never read.

## License

Tycho is licensed under the **[MIT License](LICENSE)** — do whatever you want
with it. AI was used in building this language. It is provided "as is",
without warranty; security notes are in [SECURITY.md](SECURITY.md), and how
to build, test, or contribute is in [CONTRIBUTING.md](CONTRIBUTING.md).
