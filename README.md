# Hier

A tiny AOT-compiled, statically typed systems language. The compiler is
written in C, transpiles Hier to C, and the C is compiled to a native or
fully static binary.

Hier's defining idea: **memory is managed by implicit hierarchical
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

Python-looking syntax, Go/Odin semantics (static types, value semantics,
explicit returns), arena memory underneath.

Why this works — value semantics is what lets the arenas be *implicit* — and
where it doesn't, with measured numbers, is written up in
[docs/thesis.md](docs/thesis.md).

```
$ make
$ ./hierc examples/hello.hi
$ ./examples/hello
what is your name: Ada
hello Ada
```

## Building

| Command | What it does |
| --- | --- |
| `make` | Build the `./hierc` compiler (native host). |
| `./hierc f.hi` | Transpile `f.hi` → `f.c`, then compile to native `f` with `cc`. |
| `./hierc f.hi --emit-c` | Only write `f.c`. |
| `./hierc f.hi -o name` | Write `name.c` and binary `name`. |
| `make image` | Build the Alpine/musl podman image used for static builds. |
| `make static HI=f.hi` | Produce a **fully static** Linux binary via podman. |
| `make demo` | Build and run `examples/hello.hi`. |
| `make test` | Run the test suite (see below). |
| `make test-update` | Re-record the expected-output goldens (review the diff). |
| `make bench` | Run the performance guard (see below). |
| `make clean` | Remove build artifacts. |

`make test` builds every `examples/*.hi` and `tests/*.hi` program twice — a
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
correctness. Each `bench/*.hi` program exercises one optimization and asserts a
single metric against a deliberately generous bound — peak RSS for the
memory-shape claims (in-place string append, loop scratch reset, the map
accumulator) and wall time for the `inout` memo. The bounds catch
order-of-magnitude regressions, not jitter: the in-place append holds ~1.5 MB
where the un-optimized path is ~825 MB at the same N, so the 32 MB bound sits
firmly between a working and a broken optimization.

Apple's clang cannot statically link libc on macOS, so `make static`
builds inside an Alpine container where `gcc -static` against musl yields
a dependency-free ELF (`ldd` reports "not a dynamic program").

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
(spaces only; tabs are an error) and every block header ends with `:`,
like Python. `#` starts a comment.

By default a parameter is a copy (or, for arrays, a read-only borrow). An
`inout` parameter is mutated in place — the callee writes back into the
caller's variable, marked with `&` at the call site:

```
fn incr(n: inout int):
    n = n + 1

fn main():
    x := 41
    incr(&x)             # x is 42 afterwards
```

This is copy-in copy-out (equivalent to `x = incr(x)`), so it preserves
value semantics: the `&` argument must name a mutable variable, and the same
variable can't be passed to two `inout` parameters of one call (that would
be overlapping mutable access). `inout` covers `int`, `bool`, pure-value
structs, and the heap aggregates `[int]`/`[string]` and heap-bearing structs —
including `push`/growth and element/field mutation through the borrow. Only
plain `inout string` is excluded (a string is immutable, so it buys nothing).

### Types

`int` (64-bit), `float` (64-bit IEEE double), `bool`, `string`, arrays
(`[int]`, `[float]`, `[string]`, `[Struct]`, `[[T]]`), string-keyed maps
(`[string: int]`, `[string: float]`), `Option(T)` (a value or nothing — the
no-`null` story), and user-defined `struct`s.

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
recursive tree), or another struct. Structs are values: assignment, parameters,
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

Arrays cross function boundaries too:

```
fn make_squares(n: int) -> [int]:   # returned arrays are promoted into
    r := []int                      # the caller's arena (no dangling)
    for i in range(n):
        push(r, i * i)
    return r

fn sum(a: [int]) -> int:            # a parameter is a read-only borrow:
    total := 0                      # you may read it but not push/index-set
    for i in range(len(a)):         # it (that needs a copy, or an inout param)
        total = total + a[i]
    return total
```

An array parameter is passed as a borrow (no copy); mutating it in place is
a compile error — copy it first (`b := a`) to get a mutable local, or take
it `inout`. *Not yet:* a struct **field** of array-of-struct/array type (e.g.
`struct Node: children: [Node]` — the recursive tree); use index references
(a `[Node]` plus `[int]` child indices) for now.

### Maps (`[string: int]`, `[string: float]`)

A map has `string` keys and either `int` or `float` values; the value type
follows from the literal or annotation (`["a": 1]` is a `[string: int]`,
`["a": 1.5]` a `[string: float]`):

```
counts := ["ada": 1, "alan": 2]   # literal: "key": value pairs
empty := []string: int            # empty map (key/value types required)

map_get(counts, "ada", 0)         # value for "ada", or 0 if absent -> int
map_has(counts, "ada")            # membership -> bool
len(counts)                       # entry count -> int

counts = map_set(counts, "grace", 5)   # add/overwrite -> a new map
counts = map_del(counts, "alan")       # remove a key -> a new map

ks := keys(counts)                     # iterate: keys(m) -> [string]
for i in range(len(ks)):
    k := ks[i]
    print(k + "=" + str(map_get(counts, k, 0)) + "\n")
```

`keys(m)` returns the live keys as a `[string]` (in unspecified order); index
it to walk the map — there is no dedicated `for k in m` form. `map_del(m, k)`
removes a key (a no-op if absent). See [`examples/wordcount.hi`](examples/wordcount.hi).

`map_set(m, k, v)` is a **pure** operation: it returns a new map and leaves
`m` untouched, the same way `+` on a string returns a new string. The
canonical counter idiom is therefore a self-rebind:

```
counts = map_set(counts, w, map_get(counts, w, 0) + 1)
```

Maps are values, like everything else: assigning one (`b := counts`) is a
deep copy, so mutating the copy never touches the original, and `==`/`!=`
compare entry-wise (`a == b` is true exactly when `b` is an independent copy
of `a`). They cross function boundaries the same way arrays do — a `[string: int]`
parameter is a read-only borrow (mutating it is a compile error; copy it first
or take it `inout`), and returned maps are promoted into the caller's arena. An
`inout [string: int]` lets a callee share and mutate the caller's map in place
(a counter threaded through calls), exactly like an `inout` array.

The self-rebinds `m = map_set(m, k, v)` and `m = map_del(m, k)` look O(n) per
step (build a fresh map each time), but because value semantics proves `m` is
uniquely owned at that point, the compiler mutates it in place — the loop is
O(n) total, the same trick as in-place string append (see
[Memory model](#memory-model)).

All the operations (`map_set`/`map_get`/`map_has`/`map_del`/`keys`/`len`, the
in-place accumulator rebind, `==`, `inout`) work the same on both; `map_get`'s
default and `map_set`'s value just take the map's value type. *Not yet:* value
types other than `int`/`float`, and key types other than `string`.

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
by value like everything else. *Not yet:* `Option` as a struct field or array
element, and comparing two options with `==` (match on them instead).

### Declarations and assignment

```
x := 41          # inferred
y : int = 1      # explicit type
x = x + 1        # assignment (variable must already exist)
```

### Expressions

- Arithmetic on `int`: `+ - * /` (`/` truncates), unary `-`.
- Arithmetic on `float`: `+ - * /` (`/` is true division), unary `-`. A float
  literal is `digits.digits` (e.g. `3.14`, `4.0` — no exponent or leading-dot
  form yet). `int` and `float` never mix implicitly: convert with `to_float(n)`
  / `to_int(x)` (the latter truncates toward zero).
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

There is exactly one loop keyword, `for`, in two shapes — it does
everything a `while` would (cf. Wren):

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
```

In the counting form `range(...)` is the only thing a `for` may iterate
(there are no general iterables yet); the loop variable is an `int`
scoped to the loop. The condition form takes any `bool` expression.

### Builtins

| Builtin | Type | Notes |
| --- | --- | --- |
| `print(s)` | `string -> void` | No implicit newline; use `"\n"`. |
| `input()` | `-> string` | Reads one line from stdin (newline stripped). |
| `str(x)` | `int -> string` / `float -> string` | Number to string (a float prints with up to 15 significant digits, always with a `.`). |
| `to_float(n)` | `int -> float` | Widen an int to a float. |
| `to_int(x)` | `float -> int` | Truncate a float toward zero. |
| `len(x)` | `string -> int` / `[T] -> int` / `[string: int] -> int` | Byte length of a string, element count of an array, or entry count of a map. |
| `substr(s, a, b)` | `(string, int, int) -> string` | Substring `[a, b)`; a fresh copy. Out-of-range bounds are clamped (no error). |
| `find(s, sub)` | `(string, string) -> int` | Byte index of the first occurrence of `sub`, or `-1` if absent. |
| `split(s, sep)` | `(string, string) -> [string]` | Split on a non-empty separator; `n` separators yield `n+1` fields (an empty `s` yields one empty field). Empty separator aborts. |
| `map_set(m, k, v)` | `([string: int], string, int) -> [string: int]` | Returns a new map with `k`→`v` added/overwritten; pure (`m` unchanged). The `m = map_set(m, …)` self-rebind grows in place. |
| `map_get(m, k, d)` | `([string: int], string, int) -> int` | Value for key `k`, or default `d` if absent. |
| `map_has(m, k)` | `([string: int], string) -> bool` | Whether key `k` is present. |
| `map_del(m, k)` | `([string: int], string) -> [string: int]` | Returns a new map without key `k` (no-op if absent); pure. The `m = map_del(m, …)` self-rebind deletes in place. |
| `keys(m)` | `[string: int] -> [string]` | The live keys as a `[string]` (unspecified order); index it to iterate the map. |

String escapes: `\n \t \\ \"`. Strings are byte buffers; `len`, `s[i]`,
`substr`, and `find` are all byte-oriented (not Unicode-aware).

## Memory model

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
naively each step copies the whole accumulator. Hier compiles a *self-append*
(`acc` on the left of `+`, reassigned to `acc`) to grow `acc`'s buffer in
place with geometric capacity, like an array's `push`, so the loop is O(n)
time and O(n) memory. This is sound because value semantics already
guarantees `acc` is uniquely owned at that point — a `b := acc` elsewhere
took its own deep copy, so growing `acc` in place is invisible to everyone
else. Measured: accumulating an n-char string went from ~836 MB at n=40 000
(quadratic) to a flat ~3 MB (linear). Like the others, it changes nothing in
the source — `acc = acc + e` is still just value-semantic concatenation.

None of this appears in Hier source.

### Known limitations (proof-of-concept)

- No modules or generics. Single source file. Arrays nest (`[int]`, `[float]`,
  `[string]`, `[Struct]`, `[[T]]`) and may be struct fields (incl. recursive
  `[Node]`), but a struct field cannot yet be an `Option`, and you cannot mutate
  *through* an array element (`arr[i]` is a copy, so `arr[i].f = v` /
  `push(arr[i].xs, v)` are rejected — rebuild the element instead). Maps are
  string-keyed with
  `int` or `float` values (`[string: int]`, `[string: float]`) — no other key
  or value type yet; they support
  `map_set`/`map_get`/`map_has`/`map_del`/`keys`/`len`, in-place accumulator
  rebinds, and `inout`.
  `inout` covers int, bool, pure-value structs, and the heap aggregates
  `[int]`/`[string]` and heap-bearing structs — including `push`/growth and
  element/field mutation through the borrow (shared mutable state across calls,
  e.g. a memo table — see `examples/memo.hi`, `examples/collect.hi`,
  `examples/context.hi`). Only plain `inout string` is excluded (immutable).

## Repository layout

```
src/hierc.c        the compiler (lexer, parser, type resolver, C codegen)
runtime/hier_rt.c  the arena runtime, embedded verbatim into every output
build/             generated embed header (make artifact)
podman/Dockerfile  Alpine/musl image for static builds
examples/          hello, demo, accumulate, accumulate_big, arrays,
                   array_fns, structs, strings, words, wordcount, records,
                   inout, memo, collect, context (.hi)
tests/run.sh       test harness (native -O2 vs ASan/UBSan, + golden output)
tests/*.hi         dedicated regression programs (+ optional <name>.in stdin)
tests/*.out        recorded expected output (goldens) for every test program
bench/run.sh       performance guard (peak RSS / time bounds per optimization)
bench/*.hi         one benchmark program per optimization; bench/peakrss.c helper
docs/thesis.md     why value semantics makes implicit arenas work (+ limits)
docs/arrays-structs.md   the original aggregates design pressure-test
```

The runtime is turned into a C string literal at build time (`make`
generates `build/hier_rt_embed.h` from `runtime/hier_rt.c`) and prepended
to every generated `.c`, so output files are self-contained.
