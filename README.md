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
| `make clean` | Remove build artifacts. |

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

### Types

`int` (64-bit), `bool`, `string`, and `[int]` (growable array of int).

### Arrays (`[int]`)

```
xs := [10, 20, 30]      # literal
ys := []int             # empty (element type required)
push(xs, 40)            # append in place
len(xs)                 # length -> int
xs[0]                   # index read (bounds-checked)
xs[0] = 99              # index write (bounds-checked)
zs := xs                # value semantics: zs is an independent deep copy
```

Arrays are values: assigning one copies it, so mutating the copy never
touches the original. Out-of-bounds access aborts with a message.

Arrays cross function boundaries too:

```
fn make_squares(n: int) -> [int]:   # returned arrays are promoted into
    r := []int                      # the caller's arena (no dangling)
    for i in range(n):
        push(r, i * i)
    return r

fn sum(a: [int]) -> int:            # a parameter is a read-only borrow:
    total := 0                      # you may read it but not push/index-set
    for i in range(len(a)):         # it (that needs a copy, or `inout` later)
        total = total + a[i]
    return total
```

A parameter is passed as a borrow (no copy); mutating it in place is a
compile error — copy it first (`b := a`) to get a mutable local. *Not yet:*
`[string]`/struct elements, and `inout` parameters for in-place mutation.

### Declarations and assignment

```
x := 41          # inferred
y : int = 1      # explicit type
x = x + 1        # assignment (variable must already exist)
```

### Expressions

- Arithmetic on `int`: `+ - * /`, unary `-`.
- `+` on `string` concatenates.
- Comparisons `== != < > <= >=` produce `bool`. `==`/`!=` work on any
  matching pair (including `string`); ordering is `int`-only.
- Calls: `f(a, b)`.

### Control flow

There is exactly one loop keyword, `for`, in two shapes — it does
everything a `while` would (cf. Wren):

```
if cond:
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
| `str(n)` | `int -> string` | Integer to string. |

String escapes: `\n \t \\ \"`.

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

None of this appears in Hier source.

### Known limitations (proof-of-concept)

- Values accumulated into an outer-scope variable inside a loop (e.g. the
  `total` above) grow that scope's arena for the whole loop; individual
  old values are not reclaimed until the scope ends. This is inherent to
  arena allocation, not a leak — memory is bounded by the scope's
  lifetime.
- A `return` from *inside* a loop does not free that loop's scratch arena
  (the function's own arena and the returned value are handled correctly).
  Reclaimed at process exit. Harmless for short-lived programs.
- No structs, arrays, floats, modules, or generics. Single source file.

## Repository layout

```
src/hierc.c        the compiler (lexer, parser, type resolver, C codegen)
runtime/hier_rt.c  the arena runtime, embedded verbatim into every output
build/             generated embed header (make artifact)
podman/Dockerfile  Alpine/musl image for static builds
examples/          hello.hi, demo.hi, accumulate.hi
```

The runtime is turned into a C string literal at build time (`make`
generates `build/hier_rt_embed.h` from `runtime/hier_rt.c`) and prepended
to every generated `.c`, so output files are self-contained.
