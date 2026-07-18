<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="branding/tycho-logo-dark.svg">
    <img src="branding/tycho-logo.svg" alt="Tycho" width="128">
  </picture>
</p>

# Tycho

**Tycho is an experimental systems language built to test one idea: implicit
hierarchical arenas under value semantics.** Each scope owns a memory arena,
freed when the scope exits; with no reference type in the language, the compiler
sees every value's lifetime from the syntax alone and inserts every allocation
and free itself. The payoff is automatic memory management — no garbage collector,
no manual `free` — from lexical scope rather than a runtime. It transpiles to C
and builds with `cc` and `make`.

```
fn greet(name: string) -> string:
    return "hello " + name

fn main():
    print("what is your name: ")
    name := input()
    println(greet(name))
```

It's a research project — an experiment testing that one idea — but a heavily-checked
one, and that's the part most experiments this young skip. There are two compilers — a
reference in C and a second one *written in Tycho* that compiles itself — and `make
fixpoint` holds the
self-hosted one to reproducing its own emitted C **byte-for-byte** and to matching the
reference's output on every test. A differential fuzzer runs both on random programs
under ASan/UBSan; every example is built twice, native and sanitized, and checked against
a committed golden. No cloud CI to take on faith — `make ci` runs the whole gate locally.
Experimental in *scope*, not in rigor.

## Quick start

A C compiler (`cc`) and `make` — that's the whole toolchain.

```
$ git clone https://github.com/StefanVonRanda/tycho
$ cd tycho
$ make                                  # builds ./tychoc
$ ./tychoc examples/hello.ty && ./examples/hello
what is your name: Ada
hello Ada
```

New here? The **[tutorial](docs/tutorial.md)** goes from this to a real program in about
an hour, and **[from `malloc` to arenas](docs/from-c-to-arenas.md)** explains the memory
model from C you already know. Full build details are under [Trying it](#trying-it). The
syntax is Python/Nim-flavored and the semantics Go/Odin-like; the value-semantics core
comes from **[Hylo](https://www.hylo-lang.org/)**.

## The thesis

The arena is an old idea, and a fast one: a bump allocator hands out memory by
incrementing a pointer, and frees everything at once when its scope ends. Game engines, request
handlers, and region systems have used it for decades. The catch has always
been knowing *when* a value is allowed to outlive its arena — in a language with
pointers, that needs whole-program alias analysis, which is the hard part.

Value semantics removes the question. Tycho has no reference type: you cannot
store or return a pointer into another value's memory, and `b := a` copies. So a
value can leave a scope in exactly two ways, both visible in the source —
**down**, passed as an argument to a callee, or **up**, returned to the caller —
and the compiler can place every allocation from the syntax alone, with no
whole-program analysis and no annotations to write. That is the whole thesis, and
the rest of the language follows from it; the full argument, with the measurements and the places it
costs, is in **[docs/thesis.md](docs/thesis.md)**.

Two optimizations keep value semantics from being slow. A value built locally and
returned is allocated in the caller's arena from the start, so `return` is a move,
not a copy. And `acc = acc + x` in a loop grows one buffer in place instead of
reallocating each step — the textbook O(n²) string build becomes O(n). Both are
sound *because* the value is provably un-aliased; neither is visible in your code.

## The evidence

The programs behind these numbers run in CI on both transpilers, their output
checked byte-for-byte against a golden. The figures themselves are measured and machine-specific, so the cross-language
*ratios* are the claim, not the absolute times.

**Self-hosting proof.** The strongest evidence for the thesis is that Tycho
compiles itself on it. Besides the C reference transpiler (`src/tychoc.c`), there
is a second transpiler written in Tycho — `compiler/tychoc0.ty` — and its codegen
runs on the same implicit arenas it emits. `make fixpoint` is the discipline: it
builds `tychoc0` three ways and asserts the last two emit **byte-identical** C,
and that the self-hosted build produces the same output as the C transpiler across
every test and example. A compiler is the hostile case for any allocator:
thousands of small, short-lived, deeply-recursive AST nodes. It manages its own
memory with no GC and no leaks ([docs/guides/memory-model.md](docs/guides/memory-model.md)).

**A real program at flat memory.** [`examples/json.ty`](examples/json.ty) is a
220-line recursive-descent JSON parser over a recursive `Json` sum type — real
recursion, zero `malloc`/`free`/refcount/GC in the source. Parsing **5,000,000**
documents in a loop holds at a **flat 10 MB**: each document's tree is reclaimed
when its loop iteration's arena resets. Clean under ASan + LeakSanitizer.

**Head-to-head, five languages** ([bench/prongB/RESULTS.md](bench/prongB/RESULTS.md)).
Peak resident memory (MB) — every binary computes the same checksum; lower is better:

| workload         | tycho |  C | Rust | Go (GC) | Koka (Perceus) |
| ---------------- | ----: | -: | ---: | ------: | -------------: |
| binary-trees     |  **13** | 33 |   33 |      36 |             14 |
| tree-rewrite     |   **6** | 13 |    9 |      22 |              7 |
| array-pipeline   |     6 |  3 |    3 |       6 |             14 |
| string-pipeline  |     1 |  1 |    2 |       3 |              2 |

On the allocation-heavy tree workloads Tycho uses the least memory of the five —
40% of C's on binary-trees, half on tree-rewrite — with no GC and no reference
counting, only lexical arenas and value semantics. Memory is the thesis metric,
and it is reached with zero manual management.

**On speed and scope.** Tycho runs in C's class: it is faster than hand-written C
on the allocation-heavy tree workloads (binary-trees, tree-rewrite) and on the
JSON parser, and it trails C and Rust on the flat array-pipeline (per-element
bounds checks, not the memory model). It is *not* a bid to be the fastest language
— absolute wall times are machine-, governor-, and toolchain-specific, so the
cross-language *ratios* are the claim, not the times. Measured on an AMD Ryzen 7
7735HS (16 threads), Debian; full toolchains and per-workload timings are in
RESULTS.md.

## What it costs

Value semantics is not free, and the costs are structural.

**No shared mutable references.** Every binding is an independent copy; there are no
pointers, and recursive structs are rejected (recursive *enums* like the `Json` tree
above are fine — they nest through a boxed payload). You can't build a shared-mutable
**graph**, **doubly-linked list**, or **observer** the pointer way; the idiom is a
**flat node pool** — hold all nodes in one array and link them by integer index — which
is also the cache-friendly layout data-oriented engines choose on purpose. See
[docs/guides/arrays-structs.md](docs/guides/arrays-structs.md).

**Pointer-shaped data costs more, measured.** Storing children by value, a
recursive trie is ~1.55× C's memory (halved by the compact indexed-dict map
layout) and a fixed-capacity LRU ~2.8× (no sharing). The flat-pool idiom above
brings the graph analog to ~1.3× C. Arenas reclaim at
*scope exit*, not incrementally, so a long-lived scope holds its transients until
it returns — scope them in an inner function. The full loss column, with the idiom
for each case, is in
[docs/internals/value-semantics-limits.md](docs/internals/value-semantics-limits.md).

**Generics are monomorphized over a fixed constraint set.** Generic functions,
structs, and enums (including recursive, e.g. `enum Tree($T)`) take `$T`, but the
only constraints are the built-in predicates (`numeric` / `comparable` /
`has_str`) and type sets (`where T: int | float`). No user-defined traits, no
higher-kinded types, no variance. See [docs/guides/generics.md](docs/guides/generics.md).

## FAQ

**"No GC and no borrow checker — how is it memory-safe?"** There is no reference
type, so a dangling pointer is *inexpressible* — the bug that escape analysis
exists to prevent can't be written. Memory frees per scope; values that outlive their scope
are copied up. `Option` removes null, `Result` removes exceptions, indexing is
bounds-checked, and copy-in/copy-out concurrency removes data races inside
Tycho (concurrent FFI calls can still race). Every
test runs under ASan + UBSan, plus LeakSanitizer and ThreadSanitizer.

**"Where's the package manager?"** There isn't one, on purpose. A package is a
directory of `.ty` files you import by path; the corelib lives under `core:`. Adding
third-party code is a deliberate manual act — vendor the source — never a one-line
command that pulls a transitive graph you've never read.

**"Deep-copying every value must be slow."** "Copied on assignment" is the
*semantic model*, not the generated code. The transpiler drops the copy wherever a
value is provably un-aliased — returns build in the caller's arena, `acc = acc + x`
grows in place, `b := a` becomes a move when `a` is dead. A copy happens only when
a value genuinely escapes to two live owners — exactly when a GC or refcount would
also work. Measured, not asserted: see the tables above.

## Trying it

[Quick start](#quick-start) above is the 30-second version; here's the rest.

`./tychoc f.ty` transpiles `f.ty` to `f.c` and compiles it to a native binary `f`;
`-o name` names the output, `--emit-c` stops at the C. The transpiler is one
dependency-free C file. The only optional extras are `pkg-config` plus a library for the
FFI-backed corelib modules (like `core:http`) and a Go toolchain for the cross-language
benchmarks — both skip cleanly when absent.

**Core library.** `corelib/` is Tycho's core library, imported as `core:<name>`. The
transpiler finds it beside its own binary, so there's no setup (`TYCHO_CORELIB`
overrides). A file with an `import` is a *package* — give it its own directory:

```
mysite/main.ty:
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
| `./tychoc f.ty` | Transpile `f.ty` → `f.c`, compile to native `f`. |
| `./tychoc f.ty --emit-c` / `-o name` | Stop at the C / name the output. |
| `make test` | Run the test suite (below). |
| `make bench` | Run the performance guard (below). |
| `make bootstrap` / `make fixpoint` | Build / self-host-check `compiler/tychoc0.ty`. |
| `make fuzz` | Differential + ASan/UBSan soundness fuzzer. |
| `make corelib` | Build + validate the standard library three ways. |
| `make ci` | The full local gate — no cloud CI. |
| `make clean` | Remove build artifacts. |

`make test` builds every `examples/*.ty` and `tests/*.ty` twice — native `-O2` and
`-fsanitize=address,undefined` — runs both on the same stdin, and asserts: exit 0,
no sanitizer report, **byte-identical output** between the builds, and a match
against the committed golden `tests/<name>.out`. Byte-identity catches UB the
optimizer and sanitizer disagree on; the golden catches a miscompile that's
self-consistently wrong. LeakSanitizer is on — every scope frees its arena at
exit, so a leak means a real missing free. Goldens are rewritten only by `make
test-update`, never by a normal run.

`make bench` guards the *performance* claims the way `make test` guards
correctness: each `bench/*.ty` asserts one metric against a generous bound. The
in-place append holds ~1.5 MB where the un-optimized path is ~825 MB at the same
N, so a 32 MB bound sits firmly between working and broken.

**Platform notes.** Builds and self-hosts on any unix-like OS — developed and gated
on Debian (x86-64), and benchmarked on macOS (Apple Silicon). No native Windows, but
WSL is fine. On macOS, `xcode-select --install`; Apple's AddressSanitizer ships no LeakSanitizer,
so that half of the sanitizer build is skipped there (the rest still runs).

## Documentation

New to Tycho? **Start with the [tutorial](docs/tutorial.md)** — a guided first hour
that ends with a small real program and the one idea that makes the language tick.
[`docs/`](docs/README.md) is the full index; the map:

- **[Tutorial](docs/tutorial.md)** — learn the language by writing and running code.
- **[From `malloc` to implicit arenas](docs/from-c-to-arenas.md)** — the memory
  model in five steps, starting from C you already know. The gentlest way in.
- **[Language reference](docs/reference/index.md)** — every construct, by topic.
  The source of truth; every example compiles on both transpilers.
- **[The thesis](docs/thesis.md)** — why value semantics makes implicit arenas
  work, and where it doesn't, with measured numbers.
- **Design notes** ([`docs/guides/`](docs/guides)) — the rationale behind each
  subsystem (memory model, concurrency, FFI, generics, maps). The reference says
  *what*; these say *why*.
- **[Architecture & status](docs/architecture.md)** — how it's built, what each
  verification gate proves, what's shipped, and the decided non-goals.

## License

Tycho is licensed under the **[MIT License](LICENSE)** — do whatever you want with
it. AI was used in building this proof-of-concept. It's experimental software
provided "as is", without warranty; security notes are in
[SECURITY.md](SECURITY.md), and how to build, test, or contribute is in
[CONTRIBUTING.md](CONTRIBUTING.md).
