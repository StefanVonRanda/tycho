<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="branding/tycho-logo-dark.svg">
    <img src="branding/tycho-logo.svg" alt="Tycho" width="128">
  </picture>
</p>

# Tycho

> **Status: 0.7 — pre-1.0. No stability guarantees yet.** Tycho is an
> experiment testing one idea — implicit arenas under value semantics. It was
> labelled 1.0 on 2026-08-05 and **demoted to 0.5 on 2026-08-09**, before any
> release was ever tagged: 1.0 is a promise not to break people, and that
> promise had never been tested because nothing had shipped and nobody outside
> this repo had written a line of Tycho. The engineering is not what was in
> doubt — see [Architecture](docs/architecture.md) for what each gate proves —
> the *contract* was.
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

**An experimental systems language with automatic memory management from lexical
scope.** Tycho tests one idea: implicit hierarchical arenas under value
semantics. Every scope owns a memory arena, freed when the scope exits; with no
reference type in the language, the compiler sees every value's lifetime from
the syntax alone and inserts every allocation and free itself. No garbage
collector, no manual `free`, no borrow checker. It transpiles to C and builds
with `cc` and `make`.

[Docs](docs/README.md) · [Tutorial](docs/tutorial.md) ·
[Reference](docs/reference/index.md) · [Thesis](docs/thesis.md) ·
[Spec](docs/spec/) · [Performance](docs/performance.md)

```tycho
fn greet(name: string) -> string:
    return "hello " + name

fn main():
    print("what is your name: ")
    name := input()
    println(greet(name))
```

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
  compiler fails closed. The language compiled itself byte-for-byte once, and
  seven full programs (below) ran on it with zero compiler or runtime defects.

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

New here? The **[tutorial](docs/tutorial.md)** goes from this to a real program
in about an hour, and **[from `malloc` to arenas](docs/from-c-to-arenas.md)**
explains the memory model from C you already know. Full build details are under
[Trying it](#trying-it). The syntax is Python/Nim-flavored and the semantics
Go/Odin-like; the value-semantics core comes from
**[Hylo](https://www.hylo-lang.org/)**.

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
| `tycho-rsa` | RSA keygen/sign/verify on `core:bignum` | the textbook vector plus python `pow()` as the oracle; `make rsa-check` |
| `tycho-kvsrv` | a concurrent HTTP key-value server | a daemon gate: 4 parallel clients, every write intact; `make kvsrv-check` |
| `tycho-sat` | a DPLL/CDCL SAT solver | the pigeonhole theorem (PHP(2..9) unsat) and planted instances whose models the runner verifies clause by clause; `make sat-check` |

The differentials are the point: an engine with a move-generator bug, a solver
with an unsound analysis, or a store that drops a concurrent write cannot pass
them. The campaign produced one language change — hex integer literals, filed
by the chess engine's castling masks and shipped — and no other finding needed
one. **The strongest evidence is that Tycho compiles itself**: besides the C
reference transpiler there is a second transpiler written in Tycho,
`compiler/tychoc0.ty`, and its codegen runs on the same implicit arenas it
emits — built three ways, the last two emitted byte-identical C. That proof is
frozen, and as of 2026-08-09 no gate re-runs it: the `selfhost-check` lane was
retired, so nothing in the tree builds `tychoc0` any more. The result stands as
a fact about the commit that proved it, and `compiler/selfhost.sh` is kept —
unrun — so it can be re-checked by hand. See
`docs/internals/plan-repo-polish-DONE.md` for the campaign's record.

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
under ASan + UBSan, plus LeakSanitizer and ThreadSanitizer.

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
| `./tychoc f.ty` | Transpile to C, compile to native `f`; the intermediate `f.c` is removed on success, kept on a `cc` failure. |
| `./tychoc f.ty --emit-c` / `-o name` | Stop at the C (to stdout; `-o name` writes `name.c`) / name the output. |
| `make test` | Run the authoritative suite in parallel (`TYCHO_THREADS=N` tunes the worker count). |
| `make bench` | Run the performance guard (below). |
| `make fuzz` | Differential + ASan/UBSan soundness fuzzer. |
| `make corelib` | Build + validate the standard library three ways. |
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

**Platform notes.** Builds and self-hosts on any unix-like OS — developed and
gated on Debian (x86-64), and benchmarked on macOS (Apple Silicon). On macOS,
`xcode-select --install`; Apple's AddressSanitizer ships no LeakSanitizer, so
that half of the sanitizer build is skipped there (the rest still runs).

**Windows** has two supported paths. **WSL2** is the zero-setup one and behaves
exactly like Linux. **Native Windows is MSYS2 + mingw-w64** — MSVC is not a
supported C target. The compiler, the runtime, the corelib and the tools build
and run there, and `make ci` is **green on native x86-64 Windows 11 under
MSYS2 + mingw-w64 gcc** (2026-08-08; the run is recorded in
[CHANGELOG.md](CHANGELOG.md)).

Read that green with its scope. It is **one box, one toolchain**, and it
carries 49 Windows-specific skips, each printing its reason: the sanitizer
lanes (mingw ships no ASan/UBSan runtime, and gcc has no TSan for a Windows
target at all), the fuzzer, the 32-bit lane, the `LD_PRELOAD` locale lane, the
self-hosting fixed point (the frozen bootstrap compiler emits POSIX-only C),
the perf gate, and every lane that needs a POSIX signal delivered to a process
— `core:signal`'s test and the HTTP server's six shutdown cases — because
MSYS2's `kill` terminates a native Windows program instead of signalling it.
**Green there means nothing reddened, not that everything ran.**

**One measured behavioural difference, and it is a documented platform limit,
not a bug to be fixed.** A thread parked in `recv` on an accepted connection is
not released by the shutdown handler as it is on Linux, so a Windows server
winds down within its idle timeout rather than within a millisecond. Nothing is
lost or corrupted — the wind-down is slower, and only that. Decided 2026-08-10;
[SECURITY.md](SECURITY.md) carries the measurement.

The one hole worth naming: for a long time the Windows-only code paths were the
newest code in the tree and the only code with no memory-safety checking at
all. Installing MSYS2's **clang64** toolchain closes most of that — 60 corpus
fixtures under ASan+UBSan and the 13-program concurrency suite under UBSan all
come back clean (2026-08-08) — but ASan itself does not work on threaded
programs with that toolchain, so the channel and allocator paths have UBSan
coverage only. That is not parity with Linux, where everything runs under
ASan/UBSan/TSan and a fuzz campaign.

The behavioural gaps that survive the port — non-ASCII filenames, `TZ`
handling, `core:os` through `cmd.exe`, the debugger's Ctrl-C — are listed in
[SECURITY.md](SECURITY.md). Release tarballs are built per platform by
`scripts/release.sh` (`--mingw` cross-builds the Windows one).

## Documentation

New to Tycho? **Start with the [tutorial](docs/tutorial.md)** — a guided first
hour that ends with a small real program and the one idea that makes the
language tick. [`docs/`](docs/README.md) is the full index; the map:

- **[Tutorial](docs/tutorial.md)** — learn the language by writing and running code.
- **[From `malloc` to implicit arenas](docs/from-c-to-arenas.md)** — the memory
  model in five steps, starting from C you already know. The gentlest way in.
- **[Language reference](docs/reference/index.md)** — every construct, by topic.
  The source of truth; every example compiles.
- **[The thesis](docs/thesis.md)** — why value semantics makes implicit arenas
  work, and where it doesn't, with measured numbers.
- **[Performance](docs/performance.md)** — the measurements behind the claims.
- **Design notes** ([`docs/guides/`](docs/guides)) — the rationale behind each
  subsystem (memory model, concurrency, FFI, generics, maps). The reference says
  *what*; these say *why*.
- **[Architecture & status](docs/architecture.md)** — how it's built, what each
  verification gate proves, what's shipped, and the decided non-goals.

## License

Tycho is licensed under the **[MIT License](LICENSE)** — do whatever you want
with it. AI was used in building this language. It is provided "as is",
without warranty; security notes are in [SECURITY.md](SECURITY.md), and how
to build, test, or contribute is in [CONTRIBUTING.md](CONTRIBUTING.md).
