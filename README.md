# Tycho

A tiny AOT-compiled, statically typed systems language. The compiler is
written in C, transpiles Tycho to C, and the C is compiled to a native
binary.

> **Status: experimental.** Tycho is a proof-of-concept exploring one idea —
> implicit arenas + value semantics, no GC and no manual `free` — not a
> production language. It self-hosts, is fuzzed and benchmarked, and builds with
> just `cc` + `make`; but there are **no stability guarantees** and the language
> may still change. It is **feature-complete for that proof-of-concept** — the
> pillars (value semantics, implicit arenas, concurrency, generics, closures,
> UFCS, FFI, `sink`) all ship in both compilers — so new work now is **ergonomics
> polish and bug reports, not new pillars** ([CONTRIBUTING](CONTRIBUTING.md) says
> where). **Experiments and feedback are very welcome** — see
> [Getting started](#getting-started), [CONTRIBUTING](CONTRIBUTING.md), and open
> an issue. (Honest scope and limits are in
> [Known limitations](#known-limitations-proof-of-concept).)

Tycho's defining idea: **memory is managed by implicit hierarchical
arenas**, one per scope, with scratch arenas for loops. The programmer
never sees an arena — you declare and use values as if the language were
dynamically managed. The compiler inserts all allocation, promotion, and
reclamation.

```
fn greet(name: string) -> string:
    return "hello " + name + "\n"

fn main():
    print("what is your name: ")
    name := input()
    print(greet(name))
```

Python-looking syntax; static types, value semantics, and explicit returns;
arena memory underneath. The same idea carries the
concurrency story: `spawn`/`wait`, `parallel for`, and channels are the
ordinary copy-in/copy-out call convention run on threads — race-free by
construction *for pure Tycho values*, no locks or lifetime rules in the
language (see [Concurrency](#concurrency-spawn-parallel-for-channels)). The
guarantee is over the values the compiler owns; crossing into C via the FFI
re-enters C's threading rules (see [FFI](#ffi-calling-c)). Types are inferred
bidirectionally (Pierce–Turner local inference): locals
from their initializers, literals/lambdas/bare empties from their destination
— with every type ground at its own line (see
[Type inference](#type-inference-bidirectional)).

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

**Prerequisites:** a C compiler (`cc` — GCC or Clang) and `make`. That is all —
the compiler is a single dependency-free C file. *(Optional: `pkg-config` + a
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
        print("line " + str(i) + "\n")
```

**Use the standard library.** `corelib/` is tycho's stdlib, imported as
`core:<name>`. The compiler finds it next to its own binary by default (`./tychoc`
sits beside `./corelib`), so no setup is needed; set `TYCHO_CORELIB` to point at a
different corelib if you want to override that. A file with an `import` is a
*package*, so give it its own directory:

```
mysite/main.ty:
    package main
    import "core:strings"
    fn main():
        print(strings.to_upper("hello") + "\n")     # HELLO
```
```
$ ./tychoc mysite/main.ty && ./mysite/main
HELLO
```

Every corelib module has a runnable usage example under
[`examples/corelib/<name>/`](examples/corelib), and two larger programs *compose*
several modules end-to-end: [`examples/fetch`](examples/fetch) (an HTTP client —
GET, JSON-parse, hash, cache) and [`examples/site`](examples/site) (a static-site
generator). Build and verify the whole tree — compiler, self-host fixpoint, tests,
corelib, fuzzer — with `make ci`.

**Where to go next:** the [Language](#language) section below is the full
reference; [docs/corelib.md](docs/corelib.md) catalogs the standard library; and
[docs/thesis.md](docs/thesis.md) explains the arena memory model underneath it all.

## Building

| Command | What it does |
| --- | --- |
| `make` | Build the `./tychoc` compiler (native host). |
| `./tychoc f.ty` | Transpile `f.ty` → `f.c`, then compile to native `f` with `cc`. |
| `./tychoc f.ty --emit-c` | Only write `f.c`. |
| `./tychoc f.ty -o name` | Write `name.c` and binary `name`. |
| `make demo` | Build and run `examples/hello.ty`. |
| `make test` | Run the test suite (see below). |
| `make test-update` | Re-record the expected-output goldens (review the diff). |
| `make bench` | Run the performance guard (see below). |
| `make bootstrap` | Build the self-hosted compiler `compiler/tychoc0.ty` with `./tychoc` and validate it on its fixtures. |
| `make fixpoint` | Self-host check: assert the Tycho-built compiler reproduces itself byte-identically and matches the C compiler's output. |
| `make fuzz` | Differential + ASan/UBSan soundness fuzzer: generate random well-typed programs, compile with both compilers, assert byte-identical output and no sanitizer fault. |
| `make corelib` | Build + validate the standard library (`corelib/`) three ways (C compiler vs the self-hosted `tychoc0`, against goldens), with usage examples and the `site` dogfood. |
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

**Platform notes.** Tycho builds and self-hosts on Linux and macOS (Apple Silicon
and Intel) with `cc`/`clang` and `make`. On macOS, install the Xcode Command
Line Tools (`xcode-select --install`). One difference worth knowing: Apple's
AddressSanitizer ships no LeakSanitizer, so the leak-detection half of the
sanitizer builds is skipped there — the correctness, UBSan, and byte-identical
checks all still run.

## Documentation

| doc | what it covers |
| --- | --- |
| [docs/thesis.md](docs/thesis.md) | why value semantics makes implicit arenas work — and where it doesn't, with measured numbers |
| [docs/arrays-structs.md](docs/arrays-structs.md) | the aggregates design pressure-test: arrays and structs under value semantics + implicit arenas, and why dropping pointers makes it sound |
| [docs/memory-model.md](docs/memory-model.md) | the arena model's design and how the self-hosted compiler runs on it |
| [docs/concurrency.md](docs/concurrency.md) | spawn/wait, parallel for, channels, select — design, implementation map, measured results |
| [docs/generics.md](docs/generics.md) | `$T` generics, monomorphized over the existing container machinery — functions, structs, enums (incl. recursive, e.g. `Tree($T)`), structured + map patterns, `where` constraints, and explicit type args (all shipped, both compilers) |
| [docs/packages.md](docs/packages.md) | multi-file packages: `import`, qualified names, the corelib hook |
| [docs/ffi.md](docs/ffi.md) | calling C: `extern fn` over scalars/strings/`bytes`/opaque `ptr`/typed `handle`s, nullable `-> Option(string)` returns, linking, shims |
| [docs/corelib.md](docs/corelib.md) | the standard library (`import "core:..."`) and its three-way gating |
| [docs/map-values.md](docs/map-values.md) | maps with arbitrary value types (`[string: Point]`, `[int: [int]]`) without generics, and the heap-value lifetimes |
| [docs/map-mutation.md](docs/map-mutation.md) | `m[k]` as a place: in-place map-value mutation (`push(m[k], v)`, `m[k] += 1`) and why it stays sound |
| [docs/perf.md](docs/perf.md) | self-hosted-compiler performance history; cross-language numbers live in `bench/` |
| [bench/README.md](bench/README.md) | the benchmark suite's map, incl. what is deliberately *not* measured and why |

**New to the language?** The [learning guide](docs/learning-guide.md) is a
hands-on, project-driven introduction for programmers coming from Python,
JavaScript, or Ruby; [docs/learning-platform.html](docs/learning-platform.html)
is the same material as a self-contained interactive tutorial (open it in a
browser).

**Where to start, by goal.** *Learning the language* → the learning guide, then
this README's language reference below. *Understanding the design* →
`docs/thesis.md` and the `docs/*.md` references above. *Compiler internals* →
`docs/internals/` (design records for shipped features — historical truth, e.g.
how generics monomorphization or the hash-flood mitigation landed). *Proposed,
not-yet-shipped work* → `docs/rfc/` (design discussions and prototypes; **nothing
in `docs/rfc/` is a guarantee about what the compiler does today**). The rule:
if a doc is under `docs/` or `docs/internals/`, it describes shipped behaviour;
if it is under `docs/rfc/`, it is a conversation about the future.

## Self-hosting

Besides the C reference compiler (`src/tychoc.c`), Tycho has a second compiler
**written in Tycho itself**: `compiler/tychoc0.ty`. It compiles a subset of the
language large enough to compile its own source, and it **self-hosts** — `make
fixpoint` builds it three ways (the C compiler builds it, then that build
rebuilds it, then that rebuild rebuilds it again) and asserts the last two
emissions are byte-identical and that the Tycho-built compiler reproduces
the C compiler's output across every `tests/` and `examples/` program. Run
`make bootstrap` to build the self-hosted compiler and `make fixpoint` to check
the byte-identical reproduction.

The fixpoint is also the language's parity discipline: **every feature lands
in both compilers or not at all** — concurrency, bidirectional inference,
packages, closures, FFI all exist twice (the two compilers emit different C
dialects with identical semantics), and shared test fixtures run through both
via the fixpoint differential, so the compilers cannot drift.

Since reaching the fixpoint, `tychoc0`'s codegen has been migrated from naive
malloc/leak C to the same implicit-arena memory model the C compiler uses — one
type family at a time (strings, arrays, maps, structs/tuples/boxes, every array
element type, enum node trees, mutable containers, per-variable block scoping,
transient placement, move-on-last-use, and finally heap-payload option/result
elements), all now arena-managed and freed per scope. It is documented in
[docs/memory-model.md](docs/memory-model.md). `tychoc0` reproduces the C
compiler's memory behaviour across every element type, common and rare, with
no known gap, and has full codegen-feature parity.

The language `tychoc0` compiles is no longer a strict subset: it reproduces the C
compiler's output byte-for-byte across all `tests/` + `examples/` programs,
including the additive `char` type, `float`, `Result`, newtypes, slices, and SOA.
A differential + sanitizer **fuzzer** (`fuzz/`, type-directed random programs
compiled by both compilers under ASan/UBSan) backs this up, with coverage
spanning those types.

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
compiler after the migration:

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
self-hosted compiler is competitive with all four reference languages on this
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
compiler-vs-generated-code analysis is in [docs/perf.md](docs/perf.md).

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

## Language

Deliberately small — one way to do each thing.

### Procedures

```
fn add(a: int, b: int) -> int:
    return a + b

fn main():               # entry point: must be `fn main():`, no return
    print(str(add(2, 3)) + "\n")
```

A `fn` with no `-> type` returns nothing. Blocks are indentation-based
(tabs or spaces — just don't mix the two within one line's indentation) and
every block header ends with `:`, like Python. `#` starts a comment.

By default a parameter is a copy (or, for arrays, a read-only borrow). An
`mut` parameter is mutated in place — the callee writes back into the
caller's variable, marked with `&` at the call site:

```
fn incr(n: mut int):
    n = n + 1

fn main():
    x := 41
    incr(&x)             # x is 42 afterwards
```

This is copy-in copy-out (equivalent to `x = incr(x)`), so it preserves
value semantics: the `&` argument must name a mutable variable, and the same
variable can't be passed to two `mut` parameters of one call (that would
be overlapping mutable access). `mut` covers `int`, `bool`, pure-value
structs, and the heap aggregates `[int]`/`[string]` and heap-bearing structs —
including `push`/growth and element/field mutation through the borrow.
`mut string` works too: the string value itself stays immutable, but
reassignment through the borrow (`s = s + "."`) reaches the caller, with the
new bytes built in the caller's arena (`tests/inout_string.ty`).

### Types

`int` (64-bit), `float` (64-bit IEEE double), `bool`, `string`, `char` (one
byte — `'x'` literals with `\n \t \r \0 \\ \'` escapes; `char ± int → char`;
`string + char` appends in place without allocating, so `s = s + ('0' + d)` is a
zero-alloc byte append), `bytes` (an immutable binary buffer — interior NUL
bytes survive, unlike `string`; `to_bytes`/`to_str` bridge the two), arrays
(`[int]`, `[float]`, `[string]`, `[Struct]`, `[[T]]`), maps
(`[string: V]` / `[int: V]` for any value type `V`, newtype keys too), `Option(T)` (a value or nothing — the
no-`null` story), `Result(T, E)` (`Ok(value)` or `Err(error)` — the
no-exceptions error story), tuples `(T1, ..., Tn)` (anonymous products — the
multiple-return-values story), user-defined `struct`s, user-defined `enum`s
(sum types / tagged unions, including recursive ones — ASTs), and `type`
newtypes (distinct, zero-cost wrappers over `int`/`float`/`string`/`bool` or
an array/map/struct).

**String interpolation:** an `f"..."` string interpolates `{expr}` holes —
`f"point=({p.x},{p.y}) sum={a + b}"` — desugaring to `"point=(" + str(p.x) + ...`.
`{{`/`}}` are literal braces; a plain `"..."` is never interpolated (so literal
braces need no escaping there). A hole may hold any expression — including one
with its own string literals (`f"{wrap(", ")}"`) or a nested f-string
(`f"({f"x={n}"})"`) — and must evaluate to an `int`, `float`, `bool`
(prints `true`/`false`), or `string`.

### Structs

```
struct Point:
    x: int
    y: int

struct Rect:
    lo: Point
    hi: Point

fn area(r: Rect) -> int:
    return (r.hi.x - r.lo.x) * (r.hi.y - r.lo.y)

fn main():
    a := Point(1, 2)        # positional construction, fields in order
    r := Rect(a, Point(4, 6))
    print(str(area(r)) + "\n")
    b := a                  # value semantics: b is an independent copy
    b.x = 99                # mutating b never touches a
    r.lo.x = 100            # nested field write, in place
```

Fields may be `int`, `float`, `bool`, `string`, an array (including an array of
structs — even of the struct being defined, so `children: [Node]` builds a
recursive tree), an `Option` (a nullable field, e.g. `age: Option(int)` or
`home: Option(Point)`), or another struct. A field that would make a struct
infinitely large by value (`next: Option(Node)` inside `Node`) is a compile
error — use indirection (`[Node]` or `Option([Node])`). Structs are values:
assignment, parameters,
and returns all copy the whole value — and the copy is **deep**, so a field
that owns heap bytes (`string`/array, at any nesting depth) is duplicated too —
copying a tree copies the whole tree. Two struct
variables never share storage:

```
struct Person:
    name: string
    tags: [string]

a := Person("Ada", ["x"])
b := a               # deep copy: b.name and b.tags are independent
b.name = "Alan"      # never touches a
```

Fields are read with `p.x` and written with `p.x = v` (including nested,
`r.lo.x = v`, and a struct's array-field element, `p.tags[0] = v`).
Construction is positional in declaration order. A struct must be declared
before it is used as a type. Structs compare by value with `==`/`!=`
(field-wise, recursing into nested structs/arrays/strings) — `a == b` is true
exactly when `b` is an independent copy of `a`.

### Arrays (`[int]`, `[string]`)

```
xs := [10, 20, 30]      # literal
ys := []int             # empty (element type required)
push(xs, 40)            # append in place
last := pop(xs)         # remove + return the last element (dies if empty)
len(xs)                 # length -> int
xs[0]                   # index read (bounds-checked)
xs[0] = 99              # index write (bounds-checked)
zs := xs                # value semantics: zs is an independent deep copy

names := ["ada", "alan"]  # [string] works the same way
push(names, "grace")
names[0] = "Ada"          # element-set copies the string into the array's arena
```

Element types are `int`, `float`, `string`, a **struct** (`[Point]`), or another
**array** (`[[int]]`, `[[string]]`) — arbitrarily nested. The element ops for
struct and nested-array elements are monomorphized (one generated array type per
distinct element type used). Arrays are values: assigning one copies it, and the
copy is **deep** — a `[string]`/`[Point]`/`[[int]]` copy duplicates element bytes,
nested structs, and inner buffers too — so mutating the copy never touches the
original. Out-of-bounds access aborts with a message.

```
ps := [Point(1, 2), Point(3, 4)]   # array of structs
push(ps, Point(5, 6))
ps[0] = Point(9, 9)                # element-set deep-copies the struct in
total := ps[1].x + ps[1].y         # index, then read a field

grid := [][int]                    # array of arrays
push(grid, [1, 2, 3])
cell := grid[0][2]                 # 3
```

A composite-array element is also a **mutable place** — you can write through it
in place instead of rebuilding the whole element (a *projection*: the compiler
yields the element's slot in the backing buffer, bounds-checked, with no pointer
ever exposed in Tycho):

```
ps[0].x = 10                       # field of an element
push(ps[0].tags, "extra")          # grow an element's array field in place
grid[1][2] = 60                    # nested-array element
bump(&ps[1].x)                     # an element field as a `mut` argument
```

Value semantics still holds: after `qs := ps`, mutating `ps[0].x` leaves `qs`
untouched (each owns its buffer). The element's owning array must be a mutable
variable or field — you cannot project through a read-only borrowed parameter.

Arrays cross function boundaries too:

```
fn make_squares(n: int) -> [int]:   # returned arrays are promoted into
    r := []int                      # the caller's arena (no dangling)
    for i in range(n):
        push(r, i * i)
    return r

fn sum(a: [int]) -> int:            # a parameter is a read-only borrow:
    total := 0                      # you may read it but not push/index-set
    for i in range(len(a)):         # it (that needs a copy, or a mut param)
        total = total + a[i]
    return total
```

An array parameter is passed as a borrow (no copy); mutating it in place is
a compile error — copy it first (`b := a`) to get a mutable local, or take
it `mut`.

### Slices (`xs[a:b]`)

`xs[a:b]` is a sub-range of an array — `xs[a:]` runs to the end, `xs[:b]` from
the start, `xs[:]` is the whole thing (all bounds-checked: `0 ≤ a ≤ b ≤ len`).
A slice behaves exactly like any array value, so its cost depends on what you do
with it:

```
xs := [10, 20, 30, 40, 50]
print(str(sum(xs[1:4])))      # passed to a read-only param: a ZERO-COPY view
                              # (the descriptor aliases xs's buffer) -> 90
mid := xs[1:4]                # stored: a deep copy, owning its own buffer
```

Passing a slice to a function that only reads its parameter costs nothing — the
descriptor `{ data + a, b - a }` points into `xs`'s buffer, the same borrow an
ordinary array argument already is. But the moment you **store** a slice
(`mid := xs[1:4]`), **return** it, or **push** it somewhere, it deep-copies into
an owning array — so value semantics still holds: mutating `xs` afterwards never
touches `mid`. This keeps the view *non-storable* (it can never outlive or alias
the buffer it came from) without any borrow checker. Slices work on every array
type (`[int]`, `[string]`, `[Point]`, `[[int]]`) and compose (`xs[1:5][1:3]`).
The one rule: you cannot pass a slice of `xs` and a `mut` of `xs` to the same
call (the `mut` could reallocate the buffer the slice views). Strings use
`substr(s, a, b)` (a copy), not slice syntax.

### Maps (`[string: V]`, `[int: V]`)

A map has `string` or `int` keys; with either key kind the value may be *any*
type — `int`, `float`, `string`, a struct, an array, … (`[string: Point]`,
`[string: [int]]`, `[int: [int]]`); the value is deep-copied in and out like any
other heap value. The types follow from
the literal or annotation (`["a": 1]` is a `[string: int]`, `["a": "b"]` a
`[string: string]`, `[1: 2]` an `[int: int]`). `keys(m)` returns `[int]` or
`[string]` to match the key type:

```
counts := ["ada": 1, "alan": 2]   # literal: "key": value pairs
empty := []string: int            # empty map (or bare `[]` where context supplies
                                  # the type -- see Type inference)

map_get(counts, "ada", 0)         # value for "ada", or 0 if absent -> int
"ada" in counts                   # membership -> bool
len(counts)                       # entry count -> int

counts["grace"] = 5               # add/overwrite a key
delete counts["alan"]             # remove a key (a no-op if absent)

ks := keys(counts)                     # iterate: keys(m) -> [string]
for i in range(len(ks)):
    k := ks[i]
    print(k + "=" + str(map_get(counts, k, 0)) + "\n")
```

`keys(m)` returns the live keys as a `[string]` (or `[int]` for an int-keyed
map) in unspecified order; iterate it with `for k in keys(m):` (or index it) to
walk the map — `k in m` tests membership, it does not iterate. `delete m[k]`
removes a key (a no-op if absent). See [`examples/wordcount.ty`](examples/wordcount.ty).

**`m[k]` is a place** you can write through — `m[k] = v`, `m[k] += 1`,
`push(m[k], v)`, `m[k].field = x` — inserting the value type's zero for a missing
key, so `cnt[w] += 1` counts in one line and `push(idx[term], doc)` grows a
`[string: [int]]` value in place. The canonical counter idiom is therefore one
line:

```
counts[w] = map_get(counts, w, 0) + 1
```

As an rvalue, `m[k]` reads the value out **by copy** — a scalar returns the
value type's zero on a missing key (and never inserts); a composite reads a deep
copy. For a non-zero default on a missing key, `map_get(m, k, default)` is the
one surviving map *function* (everything else is operator/keyword syntax). The
full rules are in [docs/map-mutation.md](docs/map-mutation.md), and the arbitrary
value-type design in [docs/map-values.md](docs/map-values.md).

Maps are values, like everything else: assigning one (`b := counts`) is a
deep copy, so mutating the copy never touches the original, and `==`/`!=`
compare entry-wise (`a == b` is true exactly when `b` is an independent copy
of `a`). They cross function boundaries the same way arrays do — a `[string: int]`
parameter is a read-only borrow (mutating it is a compile error; copy it first
or take it `mut`), and returned maps are promoted into the caller's arena. An
`mut [string: int]` lets a callee share and mutate the caller's map in place
(a counter threaded through calls), exactly like a `mut` array.

The accumulator `counts[w] = map_get(counts, w, 0) + 1` looks O(n) per step
(insert into a fresh map each time), but because value semantics proves `counts`
is uniquely owned at that point, the compiler mutates it in place — the loop is
O(n) total, the same trick as in-place string append (see
[Memory model](#memory-model)).

All the map operations (`m[k] = v` / `map_get` / `k in m` / `delete m[k]` /
`keys` / `len`, `==` deep value equality, the in-place accumulator, `mut`) work
the same regardless of key or value type; `map_get`'s default and the stored
value take the map's value type, and the key takes its key type. A key may also
be a newtype over `string`/`int`, a fieldless enum, a **struct**, or a **tuple** of
hashable fields (`[Point: int]`, `[Person: V]`, `[(int, string): V]`, nested structs
and tuples too) — or an **array** (`[[int]: V]`, hashed order-sensitively). All are
stored by value and compared/hashed deeply, so `m[Point(1, 2)]`, `m[(1, "a")]`, and
`m[[1, 2, 3]]` name the same entry every time; *not yet:* a map as the key.

### Option and `match`

`Option(T)` is a value that is either `Some(x)` or `None` — the no-`null`
story. You build one with `Some(value)` or `None`, and you take it apart with
an **exhaustive `match`** (both arms required):

```
fn index_of(xs: [int], target: int) -> Option(int):
    for i in range(len(xs)):
        if xs[i] == target:
            return Some(i)
    return None                 # None's type comes from the return type

fn main():
    match index_of([10, 20, 30], 20):
        Some(i):                # i is bound to the value (an int here)
            print("found at " + str(i) + "\n")
        None:
            print("not found\n")
```

`None` has no type of its own, so it is only allowed where the expected type is
known — a return type, a declaration annotation (`box : Option(string) = None`),
an assignment target, or a call argument; a bare `x := None` is a compile error.
`T` may be any type (`Option(string)`, `Option(Point)`, `Option([int])`, even
`Option(Option(int))`); each is monomorphized to a tagged value and deep-copied
by value like everything else, and may be a struct field (`age: Option(int)`)
or an array element (`[Option(int)]` — a list of optionals; a `None` element
takes its type from the others, so the first cannot be a bare `None`). *Not
yet:* comparing two options with `==` (match on them instead — though structs
that *contain* options compare fine).

### Result and `match`

`Result(T, E)` is a value that is either `Ok(value)` or `Err(error)` — the
no-exceptions error story. A function that can fail returns one, and the caller
takes it apart with the same **exhaustive `match`** (both arms required):

```
fn checked_div(a: int, b: int) -> Result(int, string):
    if b == 0:
        return Err("divide by zero")
    return Ok(a / b)

fn main():
    match checked_div(10, 0):
        Ok(v):                  # v is the success value (an int here)
            print("= " + str(v) + "\n")
        Err(e):                 # e is the error value (a string here)
            print("error: " + e + "\n")
```

Like `None`, a bare `Ok(v)`/`Err(e)` only fixes *one* of the two type
parameters, so the other comes from context — a return type, a declaration
annotation (`x : Result(int, string) = Err("nope")`), an assignment target, or
a call argument; a bare `x := Ok(1)` is a compile error. Both `T` and `E` may be
any type, including heap ones (`Result([int], string)`); the value is
monomorphized and deep-copied by value like everything else. Two `Result` values
compare with `==` (structural deep equality, recursing through `T` and `E`).

`or_return` propagates errors without the `match` boilerplate. Writing
`v := expr or_return` unwraps an `Ok` (binding `v` to the value) or, if `expr`
is an `Err`, immediately returns that `Err` from the enclosing function — which
must itself return a `Result(_, E)` with the *same* error type `E`:

```
fn add_two(a: string, b: string) -> Result(int, string):
    x := parse_digit(a) or_return    # Ok -> bind x; Err -> return it from add_two
    y := parse_digit(b) or_return
    return Ok(x + y)
```

`or_return` is a postfix operator (it binds tighter than any arithmetic), valid
anywhere the unwrapped value is wanted — `foo(parse(s) or_return)`,
`return Ok(parse(s) or_return + 1)`, and inside nested blocks. The propagated
`Err`'s payload is promoted into the caller's arena, so it outlives the
short-circuit. `or_return` also works on `Option`: in a function that returns
`Option(T)`, `v := opt or_return` binds `v` on `Some(v)` and returns `None` on
`None` (see `tests/or_return_option.ty`).

### Tuples and multiple return values

A tuple `(T1, ..., Tn)` (2–8 elements) is an anonymous product value — the way
a function returns more than one thing. `return a, b` builds one, and you can
**destructure** it into fresh locals at the call:

```
fn divmod(a: int, b: int) -> (int, int):
    q := a / b
    return q, a - q * b           # builds the tuple (q, remainder)

fn main():
    quot, rem := divmod(17, 5)     # destructure -> quot = 3, rem = 2
    print(str(quot) + " " + str(rem) + "\n")
```

But tuples are **first-class values**, not just a return convention: you can
store one whole (`t := divmod(17, 5)`), index it by position (`t.0`, `t.1`),
write a tuple literal (`p := (10, 20)`), pass it as an argument or a struct
field, and compare two with `==` (element-wise). Any element type works,
including heap ones (`(string, [int])`); a tuple is a value, deep-copied on
bind like everything else, so `b := a` leaves the two independent. The
destructuring comes in both forms — `a, b := f()` declares fresh locals, `a, b = f()`
assigns a tuple's elements to existing variables. Tuple elements are read-only
(no `t.0 = v`).

### Enums (sum types)

An `enum` is a value that is exactly one of several named **variants**, each
with a payload tuple of zero or more types — Tycho's tagged union / algebraic
data type. `Option(T)` is the built-in special case; an `enum` is the
user-defined general one, and `match` works on both. Variants may be recursive
(an enum carrying itself), which makes it an AST:

```
enum Expr:
    Num(float)
    Add(Expr, Expr)      # recursive: a variant carrying the enum itself
    Mul(Expr, Expr)
    Neg(Expr)

fn eval(e: Expr) -> float:
    match e:              # exhaustive — every variant must have an arm
        Num(v):
            return v
        Add(l, r):
            return eval(l) + eval(r)
        Mul(l, r):
            return eval(l) * eval(r)
        Neg(x):
            return -eval(x)

fn main():
    e := Mul(Add(Num(2.0), Num(3.0)), Neg(Num(4.0)))   # -20.0
    print(str(eval(e)) + "\n")
```

You build a value by naming the variant (`Num(3.0)`, or a bare `Red` for a
payload-less variant) and take it apart with an **exhaustive `match`** — every
variant needs an arm, and each arm binds the payload (`Add(l, r)`). An enum
value is a small value-semantic descriptor whose payload is arena-allocated, so
even a recursive enum is finite (no infinite type) and copying one is a **deep
copy of the whole tree**; `==` compares structurally. Variant names are global
(no `Enum.Variant` qualification). An enum may be a struct field, an array
element (`[Expr]`), and so on. A user enum may also be **generic** —
`enum Tree($T)` — monomorphized like a generic struct, including recursive
payloads that name the enum itself (`Node(Tree($T), $T, Tree($T))`, the
generic-AST case); see [docs/generics.md](docs/generics.md).

### Distinct newtypes (`type`)

`type Meters = float` declares a **distinct** type: it has the same runtime
representation as its underlying type (zero cost — a `Meters` *is* a `double` in
the generated C) but is type-incompatible with `float` and with every other
newtype. This is exactly Odin's `Meters :: distinct f32` — Tycho has no
*transparent* alias, so `type` always means distinct (no keyword needed to say
so). The underlying type is `int`, `float`, `string`, `bool`, an array, a map,
or a struct.

```
type Meters = float
type Seconds = float

fn area(w: Meters, h: Meters) -> Meters:
    return w * h            # arithmetic stays in Meters

fn main():
    w := Meters(3.0)        # wrap a float into a Meters
    a := area(w, Meters(4.0))
    print(str(a))           # 12 -- str sees the underlying float
    # area(3.0, 4.0)        -> error: a float is not a Meters
    # w + Seconds(1.0)      -> error: can't mix two newtypes
```

A newtype value supports its base type's **arithmetic, ordering, `==`, and
`str`** — but only between two values of the *same* newtype, so `Meters` and
`Seconds` (or `Meters` and a bare `float`) never mix by accident. That is the
whole point: zero-cost unit/ID safety. Construct one with `Meters(x)`; get the
raw number back with `to_int(x)` / `to_float(x)` (`to_str(x)` / `to_bool(x)`
for string/bool underlying). A newtype works anywhere a type does — parameter,
return, struct field, array element.

**Aggregate underlying** works the same way: `type Ids = [int]`,
`type Env = [string: int]`, `type Pos = Pt` (a struct). Wrap with `Ids(v)` (a
bare `[]` literal grounds to the underlying type), unwrap with the generic
`to_under(x)` — zero-cost, works on *any* newtype — and operate on the
unwrapped value; `==` stays deep between two values of the same newtype, and
copies keep the underlying's value semantics (`tests/newtype_agg.ty`).

A newtype over `string` or `int` is also a valid **map key**
(`[UserId: int]`): the map type carries the declared key, so a raw base
value is rejected at the door, while storage and hashing are the base's —
`keys()` returns `[UserId]`, still wrapped (`tests/newtype_key.ty`).

A **fieldless enum** keys a map the same way (`[Color: int]`): the key is
stored and hashed as its tag — deterministic, never pointer-dependent — and
`keys()` rebuilds the wrapped values from the variant singletons, so they
round-trip through `match` (`tests/enum_key.ty`). An enum with a payload
variant is rejected as a key: equal tags would not mean equal values.

A **struct, tuple, or array** keys a map too (`[Point: int]`, `[(int,int): V]`,
`[[int]: V]`): the key is stored inline and hashed deeply over its fields (a
generated `tycho_hash_<K>` beside the existing deep `==`/copy), so equal values
are equal keys — `tests/mapstructkey.ty`, `tests/maptuplekey.ty`,
`tests/maparraykey.ty`. Any key type whose leaves are all hashable works; a key
carrying a non-hashable leaf (a function value, a handle) is rejected.

### Type inference (bidirectional)

Locals infer forward from their initializer (`x := e`). In the other
direction, every position with a known destination type — declarations,
assignments, call arguments, `return`, stores, literal elements — *checks*
the expression against it (Pierce–Turner local inference), which lets these
elide:

```
xs : [int] = []          # bare [] takes the expected array/map type
counts(em, [])           # ...in argument position too
f := 1.5
g := f + 2               # an int LITERAL adapts to a float context
iter.map(xs, fn(x): x * 2)   # lambda params + return from the expected fn type

ys := []                 # even a bare decl works -- it stays pending
push(ys, 3)              # ...until its first grounding use in the block types it
o := None                # (same for None; a use that NEEDS the type first,
o = Some(5)              #  or a block ending with it pending, is a local error)
```

No type variables, no unification: every expression is typed at its own
line (synthesized or checked), so errors stay local and the memory model's
type-driven decisions are untouched. Function signatures stay explicit —
they are the module interface and the inference seeds.

### Declarations and assignment

```
x := 41          # inferred
y : int = 1      # explicit type
x = x + 1        # assignment (variable must already exist)
x += 1           # compound assignment: x op= e desugars to x = x op e
```

Compound assignment works for every binary operator
(`+= -= *= /= %= &= |= ^= <<= >>=`) and on any assignable place — a variable, an
array element (`a[i] += 1`), or a struct field (`p.x *= 2`).

### Expressions

- Arithmetic on `int`: `+ - * / %` (`/` truncates, `%` is the remainder), unary
  `-`. Bitwise/shift on `int`: `& | ^ << >>` and unary `~` (integer-only). These
  follow Go precedence — `% << >> &` bind at the multiplicative level and `| ^`
  at the additive level, so every bitwise op binds *tighter* than a comparison
  (no C `a & b == c` surprise: it parses as `(a & b) == c`).
- Arithmetic on `float`: `+ - * /` (`/` is true division), unary `-`. A float
  literal is `digits.digits`, a leading-dot `.digits`, or any of these with an
  `e`/`E` exponent (e.g. `3.14`, `4.0`, `.5`, `1e10`, `1.5e-3`, `.25e3`, `2E8`).
  After a value (`t.0`, `x.field`) a `.` is field/tuple access, not a float.
  `int` and `float` never mix implicitly: convert with `to_float(n)` /
  `to_int(x)` (the latter truncates toward zero).
- `+` on `string` concatenates.
- Comparisons `== != < > <= >=` produce `bool`. `==`/`!=` work on any
  matching pair (including `string`); ordering (`< > <= >=`) works on two
  `int`s or two `string`s (strings compare lexicographically by byte).
- `s[i]` on a `string` reads the byte at index `i` as an `int` (0..255),
  bounds-checked. Strings are immutable — `s[i] = v` is a compile error.
- Logical `and`, `or`, `not` on `bool`s, producing `bool`. `and`/`or`
  short-circuit (the right operand is not evaluated when the left already
  decides it). Precedence, tightest first: comparisons, then `not`, `and`,
  `or` — so `a < b and not done` is `(a < b) and (not done)`.
- Calls: `f(a, b)`.

### Control flow

There is exactly one loop keyword, `for`, in three shapes (condition, counting,
and foreach) — it does everything a `while` would (cf. Wren):

```
if cond:
    ...
elif other:                 # zero or more elif branches
    ...
else:
    ...

for cond:                   # condition form: repeat while cond is true
    ...

for i in range(n):          # counting form; i goes 0 .. n-1
    ...
for i in range(a, b):       # a .. b-1
    ...
for i in range(a, b, step): # step may be negative
    ...
for x in xs:                # foreach: x is each element of an array (or each
    ...                     # byte/char of a string), in order

break                       # exit the nearest enclosing loop
continue                    # skip to its next iteration
```

In the counting form the loop variable is an `int` scoped to the loop; the
foreach form binds each element of an array (any `[T]`) or each byte of a string
(the collection is evaluated once). The condition form takes any `bool`
expression. `break` and `continue` work in every loop shape, and are an error
outside a loop.

### First-class function values

A top-level function can be used as a **value**: bind it, pass it, return it, and
call it indirectly. The type is `fn(P1, ..., Pn) -> R` (drop the `-> R` for a
`void` return).

```
fn dbl(x: int) -> int:
    return x * 2

fn apply(g: fn(int) -> int, x: int) -> int:   # higher-order: takes a function
    return g(x)

fn main():
    f := dbl                 # f : fn(int) -> int
    print(str(f(5)))         # 10  (indirect call)
    print(str(apply(dbl, 21)))   # 42
```

A function value can be a **reference** to a named function (it captures nothing,
so it is just a code pointer — zero-cost and immortal) or a **closure** (see
below). This is what lets you write generic-feeling helpers over concrete
function arguments (`map`/`filter`/`reduce`-style) without generics. Builtins
(`len`, `push`, …) and functions with `mut` parameters can't be taken as
values, but a function value *is* a first-class value — storable in a struct
field, array, map value, or tuple (see below).

### Closures (lambdas)

A **lambda** is an anonymous function written inline; its body is a single
expression (an implicit return). Parameter and return types are elidable
wherever an expected fn type supplies them — `apply(fn(x): x * 2, 21)` — see
[Type inference](#type-inference-bidirectional); the examples below write
them out:

```
fn apply(f: fn(int) -> int, x: int) -> int:
    return f(x)

fn main():
    n := 10
    addn := fn(x: int) -> int: x + n     # a closure: captures `n`
    print(str(apply(addn, 5)))           # 15

    xs := [1, 2, 3, 4]
    print(str(apply(fn(x: int) -> int: x * x, 3)))   # 9 — inline, no capture
```

Closures **capture by value**: the captured variable is *deep-copied* into the
closure when it is created, so the closure is independent of any later change to
the original — this keeps the value-semantic memory model intact (a closure is a
plain value, no shared references).

```
a := [10, 20]
get_len := fn() -> int: len(a)
push(a, 30)                  # mutate the original after capture
print(str(get_len()))        # 2 — the closure kept its own copy (not 3)
```

A closure can also **escape** — be returned from the function that created it:

```
fn make_adder(n: int) -> fn(int) -> int:
    return fn(x: int) -> int: x + n      # captures n, then escapes

fn main():
    add5 := make_adder(5)
    print(str(add5(100)))                # 105
```

This stays sound with **no lifetime annotations**: on return, the closure's
captured environment is deep-copied (re-homed) into the caller's arena, exactly
like every other heap value that escapes a function. The closure carries its own
env-copy routine, so the move is automatic. This is the value-semantic memory
model's payoff — the captured state behaves like a plain value at every step.

Function values are also full members of the data model: one can be stored in a
**container** (struct field, array, map value, tuple) and called once stored, and
a returned closure can be **applied inline**:

```
struct Handler:
    cb: fn(int) -> int

fn make_adder(n: int) -> fn(int) -> int:
    return fn(x: int) -> int: x + n

fn main():
    h := Handler(make_adder(10))   # closure in a struct field
    print(str(h.cb(5)))            # 15

    ops := [make_adder(1), make_adder(100)]   # array of closures
    print(str(ops[1](5)))         # 105  (call an array element)

    print(str(make_adder(7)(3)))  # 10   (apply a returned closure inline)
```

When a container of closures escapes its scope (returned, or stored somewhere
longer-lived), each closure's captured environment re-homes along with it — the
same value-semantic deep copy that applies to every other heap value. The common
higher-order patterns (`map`/`filter`/`reduce`, predicates, comparators, factory
functions, dispatch tables) are all covered — see
[`corelib/iter`](corelib/iter/iter.ty).

### Methods (UFCS)

`x.foo(a)` is sugar for `foo(x, a)` — a "method" is just a free function whose
first parameter is the receiver. No classes, no inheritance, no `self`: dispatch
is static (on the receiver's compile-time type), the receiver is passed by value
like any argument, and any type can be a receiver (structs, `int`, …). Calls
chain, including on call results:

```
fn add(a: Vec, b: Vec) -> Vec: ...
fn norm1(v: Vec) -> int: ...
fn doubled(n: int) -> int:
    return n * 2

a.add(b).norm1()        # == norm1(add(a, b))
n := 21
n.doubled()             # == doubled(n) — int receiver
```

If the receiver's struct has a *fn-typed field* with the same name, the field
wins: `b.doubled(7)` calls the stored function value, not the free `doubled`.
See `tests/methods.ty`.

### Concurrency (spawn, parallel for, channels)

Tycho's call convention — arguments deep-copied in, result copied out, a
private arena per call — is already a sound thread boundary, so concurrency
is that same convention run on another thread. After the copy-in, a task
shares zero bytes with its spawner: race freedom falls out of value
semantics, with no `Sendable`, no lifetimes, no locks in the language.

**Scope of the guarantee.** "Race-free by construction" holds for *pure Tycho
values* — the world the compiler owns and deep-copies. It does **not** extend
across the FFI: if a task calls a C function that touches process-global or
`static` C state (many crypto/DB libraries do), Tycho cannot see that sharing,
and two tasks calling it race exactly as they would in C. Isolate such state
per thread (e.g. thread-local storage, the pattern the `core:crypto` shim
uses), or serialize the calls. See [FFI](#ffi-calling-c) and
`docs/rfc/ffi-threading-design-review.md`.

```
fn count(path: string) -> int: ...

t1 := spawn count("a.txt")     # args deep-copied into the task's own arena
t2 := spawn count("b.txt")     # runs in parallel
total := wait(t1) + t2.wait()  # blocks; result deep-copied back out
```

`spawn f(args)` takes a direct call — the argument list *is* the capture
list, explicit and by value — and yields a `Task(T)` handle. Tasks are
**affine and structured**: a handle can't be copied, reassigned, stored in a
container, captured by a closure, or discarded; it is waited at most once
(a second `wait` dies loudly at runtime), and any task still running when
its variable's scope exits — block end, `break`/`continue`, early `return`,
`or_return` — is implicitly joined there. A function can never return while
its tasks run, and an un-waited task can never leak or detach.

```
total := 0
parallel for i in range(1000000):   # K = ncpu chunk tasks (TYCHO_THREADS overrides)
    total += score(i)               # reduction: chunk-local partials, folded at join
```

`parallel for` (over a range or a collection) lifts the body into a chunk
procedure: captures deep-copy into each task, reduction accumulators (`+`
or `*` on int/float) start from the op's identity per chunk and fold at the
in-order join — so integer results are identical for any thread count. Any
other write to an outer variable is a compile error: there is nothing to
race on. On the compute-bound reduction benchmark this matches C-pthreads here
(same wall, same peak RSS — see `bench/conc/`).

```
ch := channel(string, 256)          # bounded; the creating scope frees it
w := spawn consumer(ch)             # fn consumer(ch: Channel(string)) -> int
ch.send("item-" + str(i))           # deep copy in (blocks when full)
match recv(ch):                     # deep copy out; None = closed and drained
    Some(s): ...
    None:    ...
close(ch)
n := wait(w)
```

A channel is the one deliberately shared object — a bounded **lock-free**
queue (a Vyukov MPMC ring: one CAS claims a cell, the deep copy runs with no
lock held, a sequence store publishes; the uncontended path does zero
syscalls), so value semantics outside it are untouched. `send` deep-copies
the payload into the claimed cell's arena (channel memory is bounded by
capacity × payload, for *every* element type) and `recv` copies out into the
receiver's arena, returning `Option(T)` with `None` meaning
closed-and-drained. On the 1M-message pipeline benchmark this runs in 73 ms
(Go 91 ms; about 9× faster than a hand-written C mutex ring) while still
paying the two copies the others don't (the queue's hot
counters and cells are cache-line padded, Vyukov's layout; see
`bench/conc/`). Channels can't be returned, stored, or
captured; the creating scope frees the channel after the implicit joins
above it, so the handle can never dangle.

`select` waits on several channels at once — recv arms, plus optional
`default` (non-blocking) and `closed` (every listed channel closed and
drained) arms:

```
for true:
    select:
        recv(jobs, j):
            handle(j)
        recv(events, e):
            note(e)
        closed:
            break
```

The price is stated, not hidden: every value crossing a thread boundary is
a deep copy — the same rule as an ordinary call. `bench/conc/` measures it
against C, Go, and Rust; design and staging in
[docs/concurrency.md](docs/concurrency.md), tests in `tests/conc/` (golden
output under ASan/LSan **and** ThreadSanitizer, in both compilers — the
suite is part of `make ci`).

### FFI (calling C)

`extern fn` declares a C function tycho can call — bodyless, direct C symbol,
optionally naming the library to link:

```
extern fn getpid() -> int                      # libc
extern "m" fn cos(x: float) -> float           # links -lm
extern fn sx_col_text(stmt: ptr, i: int) -> string   # C string in, tycho string out
```

The boundary covers scalars, `string`, `bytes` (binary buffers with interior
NULs), the opaque foreign-handle type `ptr` (a `void*` tycho never dereferences;
`null` literal, `is_null(p)`), and typed `handle` resources — a `handle Name:
free: c_fn` declares a `void*` whose C destructor runs automatically at scope
exit (RAII), so a foreign resource cannot leak or be used after close. A
C-returned string is **copied into the caller's arena** at the call site (and
`-> Option(string)` carries a nullable C return), so tycho never holds a pointer
into C-owned memory. `mut` scalar/string out-parameters cross too; only composite
aggregates (arrays/maps/structs) are rejected (fail closed). Linking ergonomics on the `tychoc` line: `--link`,
`--pkg` (pkg-config), `--shim` (companion `.c`). A real binding lives at
`examples/sqlite/` (in-memory SQLite); design and full rules in
[docs/ffi.md](docs/ffi.md).

### Packages

Packages: a directory of `.ty` files sharing one namespace, named by
a `package` declaration, pulled in with `import` and used via qualified
`pkg.symbol` names (functions, types, enum variants); an alias renames the
prefix:

```
package main
import g "geom"          # alias; plain `import "geom"` uses the package name

fn main():
    r := g.add(g.Point(3, 4), g.Point(1, 2))
```

`./tychoc pkg/main.ty` follows the imports and emits one binary; `import
"core:strings"` resolves the corelib found next to the compiler binary (or at
`TYCHO_CORELIB` if set). No privacy — everything in a package is visible.
Fixtures in `tests/pkg/`; details in [docs/packages.md](docs/packages.md).

### Builtins

| Builtin | Type | Notes |
| --- | --- | --- |
| `print(s)` | `string -> void` | No implicit newline; use `"\n"`. |
| `println(s)` | `string -> void` | `print(s)` plus a trailing newline; string-only like `print`, so `println(str(x))` for non-strings. |
| `input()` | `-> string` | Reads one line from stdin (newline stripped). |
| `str(x)` | `int -> string` / `float -> string` / `bool -> string` | Value to string (a float prints with up to 15 significant digits, always with a `.`; a `bool` prints `true`/`false`). |
| `to_float(n)` | `int -> float` | Widen an int to a float. |
| `to_int(x)` | `float -> int` | Truncate a float toward zero. |
| `to_bytes(s)` / `to_str(b)` | `string -> bytes` / `bytes -> string` | Bridge between `string` and `bytes` (same byte buffer; `bytes` may carry interior NULs). |
| `len(x)` | `string -> int` / `[T] -> int` / `[string: int] -> int` | Byte length of a string, element count of an array, or entry count of a map. |
| `substr(s, a, b)` | `(string, int, int) -> string` | Substring `[a, b)`; a fresh copy. Out-of-range bounds are clamped (no error). |
| `find(s, sub)` | `(string, string) -> int` | Byte index of the first occurrence of `sub`, or `-1` if absent. |
| `split(s, sep)` | `(string, string) -> [string]` | Split on a non-empty separator; `n` separators yield `n+1` fields (an empty `s` yields one empty field). Empty separator aborts. |
| `m[k] = v` | place store | Insert/overwrite key `k` (a place: also `m[k] += 1`, `push(m[k], v)`, `m[k].f = x`). Inserts the value type's zero for a missing key; grows in place when `m` is uniquely owned. |
| `map_get(m, k, d)` | `([string: int], string, int) -> int` | Value for key `k`, or default `d` if absent. The one surviving map *function* — everything else is operator/keyword syntax. |
| `k in m` | membership `-> bool` | Whether key `k` is present. |
| `delete m[k]` | statement | Remove key `k` (a no-op if absent). |
| `keys(m)` | `map -> [string]` / `[int]` | The live keys as an array of the map's key type (unspecified order); iterate it to walk the map. |
| `reserve(arr, n)` | `([T], int) -> void` | Capacity hint: preallocate room for `n` elements (`[int]`/`[float]`/`[string]`; a map place `m[k]` works too — count-then-fill builds size each list once). A hint, not a length: contents and `len` are unchanged, pushing past `n` still grows. A capacity that can't be allocated aborts at runtime. |
| `getenv(name)` | `string -> string` | The environment variable's value, or `""` if unset. |
| `wait(t)` | `Task(T) -> T` | Join a spawned task; the result deep-copies into the waiting scope and the task's arena tree frees. Exactly once per task (a second wait dies loudly); see [Concurrency](#concurrency-spawn-parallel-for-channels). |
| `ncpu()` | `-> int` | The `parallel for` fan-out width (online CPU count; overridable with `TYCHO_THREADS`). Also the worker count for `parallel for x in ch:`. |
| `channel(T, cap)` | `-> Channel(T)` | Create a bounded lock-free queue (capacity rounds up to a power of two). Only legal as a declaration's direct RHS — the creating scope frees it. |
| `send(ch, v)` | `(Channel(T), T) -> void` | Deep-copy `v` into the channel; blocks when full; dies if the channel is closed. |
| `recv(ch)` | `Channel(T) -> Option(T)` | Blocking receive, deep-copied out; `None` means closed **and** drained. |
| `close(ch)` | `Channel(T) -> void` | Receivers drain then see `None`; further sends (and a second close) die loudly. |
| `read_file(path)` | `string -> string` | The whole file as a string, or `""` if it can't be opened. |
| `write_file(path, s)` | `(string, string) -> bool` | Write `s`'s exact bytes to `path` (truncating); `true` on success, `false` if it can't be opened. |
| `read_all()` | `-> string` | All of stdin as one string. |
| `list_dir(path)` | `string -> [string]` | Directory entries excluding `.`/`..` (filesystem order — sort if needed); empty if it can't be opened. |
| `args()` | `-> [string]` | The process's command-line arguments; `args()[0]` is the program name. |
| `chr(n)` | `int -> string` | The one-byte string for byte value `n` (0..255). |
| `clock()` | `-> int` | Monotonic nanoseconds since an arbitrary epoch — for measuring elapsed time (differences are meaningful; the absolute value is not). |
| `now()` | `-> int` | Wall-clock seconds since the UNIX epoch — for timestamps. |
| `die(msg)` | `string -> void` | Print `msg` to stderr and exit with status 1. |
| `sqrt/pow/floor/fabs` | `float… -> float` | libm float math: `sqrt(x)`, `pow(x, y)`, `floor(x)`, `fabs(x)`. |

String escapes: `\n \t \\ \"`. Strings are byte buffers; `len`, `s[i]`,
`substr`, and `find` are all byte-oriented (not Unicode-aware).

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
compiler arranges both for you:

1. **Down**, as a function argument: a pointer is passed to a callee whose
   arena is a child, so it stays valid for the call. No copy.
2. **Up**, by being returned: the value is allocated in the caller's
   (parent) arena, so it survives the callee's arena being freed.

Assigning a value to a variable that lives in an outer scope is handled
the same way — the compiler allocates the value in *that variable's*
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

**Return-slot optimization (move, not copy).** Returning a value normally
means allocating it in the caller's arena. When the returned value is a
local built up in the function (the common `r := []int; … ; return r`
pattern), the compiler proves it escapes and allocates it in the caller's
arena *from the start* — so the `return` is a move, not an O(n) deep copy.
This composes across call frames: a value returned up several levels is
built once, in the final consumer's arena, with zero copies along the way.
It is invisible — same source, same value semantics, same bounded memory (a
value returned into a caller's loop is still reclaimed by that loop's
scratch reset). The analysis only promotes function-top-level locals, so a
loop-scratch local is never lifted to a longer lifetime.

**In-place append (build a string in a loop, cheaply).** The accumulator
pattern `acc = acc + e` repeated in a loop is the textbook O(n²) trap —
naively each step copies the whole accumulator. Tycho compiles a *self-append*
(`acc` on the left of `+`, reassigned to `acc`) to grow `acc`'s buffer in
place with geometric capacity, like an array's `push`, so the loop is O(n)
time and O(n) memory. This is sound because value semantics already
guarantees `acc` is uniquely owned at that point — a `b := acc` elsewhere
took its own deep copy, so growing `acc` in place is invisible to everyone
else. Measured: accumulating an n-char string went from ~836 MB at n=40 000
(quadratic) to a flat ~3 MB (linear). Like the others, it changes nothing in
the source — `acc = acc + e` is still just value-semantic concatenation.

None of this appears in Tycho source.

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
- **Generic constraints are a fixed, compiler-known set** — generic *functions*,
  *structs*, and *enums* (all including recursive types, e.g. `enum Tree($T)`)
  take `$T` and are monomorphized, but the only constraints are the built-in
  predicates (`numeric` / `comparable` / `has_str`) and type sets
  (`where T: int | float`). There are no user-defined traits/type-classes, no
  higher-kinded types, and no variance — see [docs/generics.md](docs/generics.md).

## Repository layout

```
src/tychoc.c        the C reference compiler (lexer, parser, type resolver, C codegen)
runtime/tycho_rt.c  the arena runtime, embedded verbatim into every output
compiler/tychoc0.ty the self-hosted compiler, written in Tycho (see make fixpoint)
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
docs/memory-model.md   how the self-hosted compiler runs on the implicit-arena model
docs/perf.md       compiler + generated-code performance, incl. the cross-language benchmark suite
```

The runtime is turned into a C string literal at build time (`make`
generates `build/tycho_rt_embed.h` from `runtime/tycho_rt.c`) and prepended
to every generated `.c`, so output files are self-contained.

## FAQ

**"It just transpiles to C — that's not a real compiler."** Emitting C is a
deliberate backend choice, not a shortcut: it makes Tycho portable to any target
with a C compiler and inherits decades of mature optimization and tooling for
free. C is the *assembler* here. The compiler still does all the real work —
lexing, parsing, bidirectional type inference, the implicit-arena escape
analysis, monomorphization, and the static reuse analysis (move-on-last-use,
in-place append, FBIP-style buffer recycling) — none of which C does for you. The
output is self-contained (the runtime is prepended to every file), so there is no
hidden dependency. It is the same strategy several production languages use, and
the benchmarks below are against C, Rust, Go, and Koka *binaries*, not against
other transpilers.

**"Deep-copying every value must be slow."** "Everything is a value, copied on
assignment" is the *semantic model*, not what the generated code does at runtime.
The compiler proves, statically, when a copy is unnecessary and elides it:
returns are built directly in the caller's arena (no copy out), `acc = acc + e`
grows one buffer in place, `b := a` becomes a move when `a` is dead, `match` arms
borrow the scrutinee's payload, and loop-carried rebuilds hand off their buffer.
A copy happens only when a value genuinely escapes to two live owners — which is
exactly when a GC or refcount would also do work. The result is measured, not
asserted: on the allocation-heavy tree workloads it has the lowest memory and
time of the five languages benchmarked, it matches C-pthreads on the parallel
reduction, and it has the fastest channel pipeline of the four — see
[bench/](bench/) and [docs/thesis.md](docs/thesis.md).

**"No GC and no borrow checker — how is it memory-safe?"** There is no reference
type in the language, so a dangling pointer is *inexpressible* — the bug other
region systems ship escape analysis to prevent simply cannot be written. Memory
is freed per scope by the arena hierarchy; values that outlive their scope are
copied up. `Option` removes null, `Result` removes exceptions, array and slice
access is bounds-checked, and copy-in/copy-out concurrency removes data races by
construction. Every test runs under AddressSanitizer + UndefinedBehaviorSanitizer
(plus LeakSanitizer and, for concurrency, ThreadSanitizer), and a differential
fuzzer cross-checks both compilers.

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

Tycho is licensed under the **[Apache License 2.0](LICENSE)** (see
[`NOTICE`](NOTICE) for the copyright). It is experimental, proof-of-concept
software provided "as is", without warranty — security notes are in
[SECURITY.md](SECURITY.md), and how to build, test, contribute, or report a bug
is in [CONTRIBUTING.md](CONTRIBUTING.md).
