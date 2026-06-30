<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="branding/tycho-logo-dark.svg">
    <img src="branding/tycho-logo.svg" alt="Tycho" width="128">
  </picture>
</p>

# Tycho

Tycho is an experimental systems language built to test one idea: **implicit
hierarchical arenas under value semantics**. Each scope owns a memory arena,
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

Python- and Nim-inspired syntax; Go- and Odin-like semantics; the value-semantics
core comes from **[Hylo](https://www.hylo-lang.org/)**.

> **Status: experimental.** Tycho is a proof-of-concept, not production software,
> and I won't pretend otherwise. Single implementation, no stability guarantee,
> the surface still moves. What it *does* have: it self-hosts, it's fuzzed and benchmarked
> against C/Rust/Go/Koka, and every feature ships in both transpilers or not at
> all. Questions, corrections, and experiments are welcome.

## The idea

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

## See it work

The programs behind these numbers run in CI on both transpilers, their output
checked byte-for-byte against a golden — so the code is verified correct. The
figures themselves are measured and machine-specific, so the cross-language
*ratios* are the claim, not the absolute times.

**A real program at flat memory.** [`examples/json.ty`](examples/json.ty) is a
220-line recursive-descent JSON parser over a recursive `Json` sum type — real
recursion, zero `malloc`/`free`/refcount/GC in the source. Parsing **5,000,000**
documents in a loop holds at a **flat 10 MB**: each document's tree is reclaimed
when its loop iteration's arena resets. Clean under ASan + LeakSanitizer.

**Head-to-head, five languages** ([bench/prongB/RESULTS.md](bench/prongB/RESULTS.md),
peak RSS / best-of-3 wall time, all binaries print identical output; `tycho` here
is the *self-hosted* transpiler):

| workload         |   tycho |        C |     Rust |   Go (GC) | Koka (Perceus) |
| ---------------- | ------: | -------: | -------: | --------: | -------------: |
| binary-trees     | 13 MB/107 ms | 33/765 ms | 34/855 ms | 32/1756 ms |   15/269 ms |
| tree-rewrite     |  6 MB/89 ms  | 13/556 ms | 10/404 ms |  21/837 ms |    8/178 ms |
| array-pipeline   |  5 MB/30 ms  |  3/22 ms |  3/23 ms |   6/53 ms |   18/372 ms |
| string-pipeline  |  2 MB/1 ms   |  1/1 ms  |  2/2 ms  |   4/5 ms  |    2/17 ms |

On the allocation-heavy tree workloads Tycho is competitive with all four. On
binary-trees it has the lowest memory of the five and the lowest wall time, with
no GC and no reference counting — only lexical arenas and value semantics. It
trails C and Rust on array-pipeline time (per-element bounds checks, not the
memory model). Toolchains and both reference machines (AMD Ryzen 7 7735HS x86-64
Linux; Apple Silicon arm64 macOS) are in RESULTS.md.

**A realistic workload** ([bench/site/](bench/site), `make bench-site`): render
*N* Markdown pages to HTML in Tycho / C / Go, with an FNV checksum over every
rendered byte confirming all three do identical work.

| pages  |  tycho  |   C    |  Go (GC) |
| -----: | -----: | -----: | -------: |
|  1,000 | 1.5 MB | 1.5 MB |   6.9 MB |
| 20,000 | 1.5 MB | 1.6 MB |   8.5 MB |

Peak RSS is **flat ~1.5 MB across a 20× scale** — each page renders in a loop-body
arena reclaimed every iteration, so the working set is one page regardless of *N*,
matching C with no `free` and no GC. The honest trade: here Tycho is ~2× C on wall
time (its string rebuild vs C's `realloc` buffer). On this workload the win is
memory, not speed.

## Self-hosting

The strongest evidence for the model is that Tycho compiles itself on it. Besides
the C reference transpiler (`src/tychoc.c`), there is a second transpiler written
in Tycho — `compiler/tychoc0.ty` — and its codegen runs on the same implicit
arenas it emits. A compiler is the hostile case for any allocator: thousands of
small, short-lived, deeply-recursive AST nodes. It manages its own memory with no
GC and no leaks.

`make fixpoint` is the proof and the discipline. It builds `tychoc0` three ways
and asserts the last two emit **byte-identical** C, and that the self-hosted build
produces the same output as the C transpiler across every test and example. That
parity is enforced, not hoped for: **every feature lands in both transpilers or
not at all** — concurrency, generics, closures, FFI, packages all exist twice,
and a differential fuzzer (`fuzz/`) cross-checks them under ASan/UBSan.

## Getting started

**Prerequisites:** a C compiler (`cc` — GCC or Clang) and `make`. That's all; the
transpiler is one dependency-free C file. *(Optional: `pkg-config` + a library for
FFI-backed corelib modules like `core:http`; a Go toolchain for the cross-language
benchmarks. Both skip cleanly when absent.)*

```
$ git clone https://github.com/StefanVonRanda/tycho
$ cd tycho
$ make                                  # builds ./tychoc
$ ./tychoc examples/hello.ty && ./examples/hello
what is your name: Ada
hello Ada
```

`./tychoc f.ty` transpiles `f.ty` to `f.c` and compiles it to a native binary `f`;
`-o name` names the output, `--emit-c` stops at the C.

**Standard library.** `corelib/` is Tycho's stdlib, imported as `core:<name>`. The
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

## Building

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

**Platform notes.** Builds and self-hosts on any unix-like OS — tested on Debian,
Arch, and macOS (Apple Silicon and Intel). No native Windows, but WSL is fine. On
macOS, `xcode-select --install`; Apple's AddressSanitizer ships no LeakSanitizer,
so that half of the sanitizer build is skipped there (the rest still runs).

## What it costs

Value semantics is not free, and the costs are structural.

**No shared mutable references.** Every binding is an independent copy; there are
no pointers, and recursive struct types are rejected (recursive *enums*, like the
`Json` tree above, are fine — they nest through a boxed payload, not by value). You
cannot build a shared-mutable **graph**, **doubly-linked list**, or **observer**
the way a pointer language does. The idiom is a **flat node pool**: hold all nodes in one
`[Node]` and link them by integer index, not reference (a generational index
gives use-after-free detection). The whole structure becomes one value with one
arena lifetime — and the layout is cache-friendly, the same data-oriented pattern
high-performance engines choose on purpose. See
[docs/arrays-structs.md](docs/arrays-structs.md) §2.

**Pointer-shaped data costs more, measured.** Storing children by value, a
recursive trie is ~3.2× C's memory and a fixed-capacity LRU ~5× (no sharing). The
flat-pool idiom above brings the graph analog back to ~1.3× C. Arenas reclaim at
*scope exit*, not incrementally, so a long-lived scope holds its transients until
it returns — scope them in an inner function. The full loss column, with the idiom
for each case, is in
[docs/internals/value-semantics-limits.md](docs/internals/value-semantics-limits.md).

**Generics are monomorphized over a fixed constraint set.** Generic functions,
structs, and enums (including recursive, e.g. `enum Tree($T)`) take `$T`, but the
only constraints are the built-in predicates (`numeric` / `comparable` /
`has_str`) and type sets (`where T: int | float`). No user-defined traits, no
higher-kinded types, no variance. See [docs/generics.md](docs/generics.md).

## Documentation

- **[Language reference](docs/reference/index.md)** — every construct, by topic.
  The source of truth; every example compiles on both transpilers.
- **[The thesis](docs/thesis.md)** — why value semantics makes implicit arenas
  work, and where it doesn't, with measured numbers.
- **Design notes** (`docs/*.md`) — the rationale behind each subsystem (memory
  model, concurrency, FFI, generics, maps). The reference says *what*; these say
  *why*.
- **[STATUS.md](STATUS.md)** — architecture, verification gates, what's shipped.
  Start here to evaluate or contribute.

## FAQ

**"It just transpiles to C."** Emitting C is a backend choice — C is the
*assembler* here, giving portability and decades of optimization for free. The
transpiler still does the real work: bidirectional type inference, the
implicit-arena escape analysis, monomorphization, and the static reuse analysis
(move-on-last-use, in-place append, buffer recycling). Output is self-contained;
the benchmarks above are against C/Rust/Go/Koka *binaries*.

**"Deep-copying every value must be slow."** "Copied on assignment" is the
*semantic model*, not the generated code. The transpiler drops the copy wherever a
value is provably un-aliased — returns build in the caller's arena, `acc = acc + x`
grows in place, `b := a` becomes a move when `a` is dead. A copy happens only when
a value genuinely escapes to two live owners — exactly when a GC or refcount would
also work. Measured, not asserted: see the tables above.

**"No GC and no borrow checker — how is it memory-safe?"** There is no reference
type, so a dangling pointer is *inexpressible* — the bug that escape analysis
exists to prevent can't be written. Memory frees per scope; values that outlive their scope
are copied up. `Option` removes null, `Result` removes exceptions, indexing is
bounds-checked, and copy-in/copy-out concurrency removes data races by
construction (for pure Tycho values; across the FFI you're under C's rules). Every
test runs under ASan + UBSan, plus LeakSanitizer and ThreadSanitizer.

**"Is it production-ready?"** No. It's experimental, proof-of-concept software with
a single implementation and a moving surface. Use it to learn, experiment, and
give feedback — not to ship a service.

**"Where's the package manager?"** There isn't one, on purpose. A package is a
directory of `.ty` files you import by path; the stdlib lives under `core:`. Adding
third-party code is a deliberate manual act — vendor the source — never a one-line
command that pulls a transitive graph you've never read.

## License

Tycho is licensed under the **[MIT License](LICENSE)** — do whatever you want with
it. AI was used in building this proof-of-concept. It's experimental software
provided "as is", without warranty; security notes are in
[SECURITY.md](SECURITY.md), and how to build, test, or contribute is in
[CONTRIBUTING.md](CONTRIBUTING.md).
