# Learning Hier — for programmers coming from Python, JavaScript, or Ruby

A hands-on, project-driven introduction to Hier — a tiny, statically typed, AOT-compiled systems language with implicit arena memory management. By the end you'll have written real programs in it and understand how Hier's value-semantic model gives you memory safety without a garbage collector, lifetimes, or manual `malloc`/`free`.

Hier is an experimental, open proof-of-concept. It's small on purpose — usually one way to do each thing — so this guide can cover the whole language and still fit in one sitting.

**Who this is for:** You've written a dynamic, managed language — JavaScript, Python, or Ruby. You know what a function, a loop, and a string are. You may have heard "stack vs heap" but never managed memory yourself. This guide meets you there and walks you into a systems language one concept at a time. Read it top to bottom: each section builds on the last, and the projects at the end pull everything together.

---

## Table of Contents

1. [What is Hier?](#1-what-is-hier)
2. [Setup and Hello World](#2-setup-and-hello-world)
3. [Variables, Types, and Arithmetic](#3-variables-types-and-arithmetic)
4. [Functions](#4-functions)
5. [Control Flow](#5-control-flow)
6. [Strings](#6-strings)
7. [Arrays](#7-arrays)
8. [Structs](#8-structs)
9. [Maps](#9-maps)
10. [Option and Result](#10-option-and-result)
11. [Enums and Pattern Matching](#11-enums-and-pattern-matching)
12. [Tuples and Multiple Return Values](#12-tuples-and-multiple-return-values)
13. [Type Inference](#13-type-inference)
14. [Value Semantics: The Mental Model](#14-value-semantics-the-mental-model)
15. [Closures and Higher-Order Functions](#15-closures-and-higher-order-functions)
16. [Methods (UFCS)](#16-methods-ufcs)
17. [Concurrency](#17-concurrency)
18. [Packages and the Standard Library](#18-packages-and-the-standard-library)
19. [Calling C (FFI)](#19-calling-c-ffi)
20. [Project: Inverted-Index Search Engine](#20-project-inverted-index-search-engine)
21. [Project: JSON Parser](#21-project-json-parser)
22. [Project: Ray Tracer](#22-project-ray-tracer)
23. [Cheat Sheet](#23-cheat-sheet)

---

## 1. What is Hier?

Hier is a **systems language** — it compiles to native machine code, like C or Rust. But its syntax looks like Python:

```
fn greet(name: string) -> string:
    return "hello " + name + "\n"

fn main():
    print("what is your name: ")
    name := input()
    print(greet(name))
```

Three things make Hier different from the languages you know:

| Concept | JavaScript/Python | Rust | Hier |
|---|---|---|---|
| Memory | Garbage collector | Borrow checker + lifetimes | Implicit arenas (automatic) |
| Types | Dynamic | Static, inferred | Static, inferred |
| Null | `null` / `None` | `Option<T>` | `Option(T)` |
| Errors | Exceptions | `Result<T, E>` | `Result(T, E)` |
| Concurrency | Shared mutable state | `Send`/`Sync` + lifetimes | Copy-in/copy-out (race-free) |

The key insight, and the one idea to hold onto for the whole guide: **every value is independently owned**. When you assign `b = a`, `b` gets its own deep copy. Two variables never share storage. This is "value semantics," and it's what lets the compiler manage memory for you automatically — each scope gets an arena (a memory pool), and when the scope ends, the whole arena is freed at once. You never write `free()`, never count references, and never annotate lifetimes.

Don't worry if that's abstract right now. You'll *see* it in every section, and section 14 returns to it once you have enough code under your belt for it to click.

---

## 2. Setup and Hello World

### Building the compiler

You need a C compiler (`gcc` or `clang`) and `make`. Building Hier is one step — there are no dependencies to install:

```
git clone <repo-url> hier
cd hier
make
```

This produces `./hierc`, the Hier compiler. (On macOS, install the Xcode Command Line Tools first: `xcode-select --install`.)

### Compiling and running a program

`hierc` compiles a `.hi` source file to a native executable next to it, then you run that executable:

```
./hierc examples/hello.hi     # produces ./examples/hello
./examples/hello
```

### Hello World

Create `hello.hi`:

```
fn main():
    print("Hello, world!\n")
```

Compile and run:

```
./hierc hello.hi
./hello
```

That's the full edit-compile-run loop you'll use for every program in this guide.

**Key differences from JavaScript/Python:**
- `fn` instead of `function` / `def`
- `-> type` declares the return type
- Indentation is significant (tabs or spaces, but don't mix the two within one line)
- Every block header ends with `:`
- `#` starts a comment
- `print()` takes a string and writes it — no implicit newline (add `\n` yourself)
- `println()` is `print()` plus a trailing newline: `println("hi")` ≡ `print("hi" + "\n")` (still string-only, so `println(str(x))` for non-strings)
- Every program needs a `fn main():` as the entry point (no return type)

---

## 3. Variables, Types, and Arithmetic

Hier has two ways to introduce a variable: `:=` declares a new one and infers its type from the value, while `=` reassigns an existing one. Types are static — every variable has one fixed type — but you rarely write the type yourself.

```
fn main():
    # declarations
    x := 41              # type inferred from the value (int)
    y : int = 10         # explicit type annotation
    name := "Ada"
    pi := 3.14
    flag := true

    # assignment (variable must already exist)
    x = x + 1            # x is 42

    # compound assignment
    x += 1               # x is 43
    y *= 2               # y is 20

    # arithmetic
    a := 10 + 3           # 13
    b := 10 - 3           # 7
    c := 10 * 3           # 30
    d := 10 / 3           # 3 (integer division truncates)
    e := 10 % 3           # 1 (remainder)

    # floats
    f := 10.0 / 3.0       # 3.333... (true division)
    n := to_float(x)      # int -> float
    m := to_int(f)        # float -> int (truncates toward zero)

    # string concatenation
    greeting := "hello " + name   # "hello Ada"

    # comparisons produce bool
    print(str(x == 43) + "\n")    # "true"
    print(str(x > 40) + "\n")     # "true"

    # convert anything to a string with str()
    print(str(x) + "\n")          # "43"
```

### Built-in types

| Type | Description | Example |
|---|---|---|
| `int` | 64-bit signed integer | `42`, `0`, `-7` |
| `float` | 64-bit IEEE double | `3.14`, `0.0` |
| `bool` | `true` or `false` | `true`, `false` |
| `string` | Byte buffer (immutable) | `"hello"` |
| `char` | Single byte | `'A'`, `'\n'` |
| `[T]` | Array of T | `[1, 2, 3]` |
| `[K: V]` | Map | `["a": 1]` |
| `Option(T)` | A value or nothing | `Some(5)`, `None` |
| `Result(T, E)` | A value or an error | `Ok(5)`, `Err("bad")` |
| `(T1, T2)` | Tuple | `(1, "two")` |

### Converting between types

```
to_float(42)       # int  -> float: 42.0
to_int(3.9)        # float -> int: 3 (truncates)
str(42)            # int  -> string: "42"
str(3.14)          # float -> string: "3.14"
str(true)          # bool -> string: "true"
chr(65)            # int  -> string: "A"
```

---

## 4. Functions

A function declares its parameter types and, if it returns a value, its return type after `->`. The arguments come in *by value* — the function works on its own copies — which is your first taste of value semantics in action.

```
# a function that returns an int
fn add(a: int, b: int) -> int:
    return a + b

# a function that returns nothing (no -> type)
fn say(s: string):
    print(s + "\n")

# parameters are copies by default (value semantics)
fn double(n: int) -> int:
    n = n * 2          # mutates the local copy
    return n           # caller's original is unchanged

# mut: mutate the caller's variable (pass with &)
fn incr(n: mut int):
    n = n + 1

fn main():
    print(str(add(3, 4)) + "\n")     # 7

    x := 10
    y := double(x)
    print(str(x) + " " + str(y) + "\n")   # 10 20

    incr(&x)               # x is now 11 (& marks the shared variable)
    print(str(x) + "\n")   # 11
```

**Mental model (coming from Python or JavaScript):**

- `fn foo(x: int)` — like passing a primitive in JS. The function gets its own copy.
- `fn foo(xs: [int])` — like a read-only view. You can read elements but not mutate the array.
- `fn foo(xs: mut [int])` — like passing by reference. The function can mutate the caller's array. Mark the call with `&`.

**String interpolation** (like template literals in JS):

```
name := "Ada"
age := 36
print(f"{name} is {age}\n")             # "Ada is 36"
print(f"2 + 3 = {2 + 3}\n")            # "2 + 3 = 5"
```

Use `f"..."` to interpolate. `{expr}` inserts the value. Plain `"..."` strings are never interpolated.

---

## 5. Control Flow

### if / elif / else

```
fn classify(n: int) -> string:
    if n < 0:
        return "negative"
    elif n == 0:
        return "zero"
    else:
        return "positive"
```

### Loops

There's one loop keyword — `for` — in three shapes:

```
# 1. Condition form (like while)
x := 10
for x > 0:
    print(str(x) + " ")
    x = x - 1
# prints: 10 9 8 7 6 5 4 3 2 1

# 2. Counting form (like for i in range)
for i in range(5):          # i = 0, 1, 2, 3, 4
    print(str(i) + " ")

for i in range(2, 7):       # i = 2, 3, 4, 5, 6
    print(str(i) + " ")

for i in range(10, 0, -2):  # i = 10, 8, 6, 4, 2
    print(str(i) + " ")

# 3. Foreach form (iterate an array or string)
names := ["Ada", "Grace", "Alan"]
for name in names:
    print(name + "\n")

for byte in "ABC":
    print(str(byte) + " ")    # prints byte values: 65 66 67
```

### break and continue

```
for i in range(100):
    if i == 5:
        continue          # skip 5
    if i > 10:
        break             # stop the loop
    print(str(i) + " ")
```

### Logical operators

```
and    # && in JS — short-circuits
or     # || in JS — short-circuits
not    # ! in JS
```

Precedence: comparisons bind tightest, then `not`, `and`, `or` — so `a < b and not done` means `(a < b) and (not done)`.

---

## 6. Strings

Strings are **immutable byte buffers** — you can't change a character in place, but you can build new strings with `+`.

```
fn main():
    s := "hello"
    print(str(len(s)) + "\n")     # 5

    # byte indexing (read-only)
    print(str(s[0]) + "\n")       # 104 (byte value of 'h')

    # concatenation builds a new string
    s2 := s + " world"
    print(s2 + "\n")              # "hello world"

    # substring: substr(s, start, end) — returns a copy
    print(substr("hello world", 0, 5) + "\n")   # "hello"

    # find: returns byte index or -1
    print(str(find("hello world", "world")) + "\n")   # 6

    # split: returns an array
    parts := split("one,two,three", ",")
    for p in parts:
        print(p + "\n")
```

### Building strings in a loop

Hier has a secret optimization: when you accumulate a string with `total = total + x` in a loop, the compiler grows the buffer in place — O(n) total, not O(n²). This is automatic:

```
fn main():
    total := ""
    for i in range(1, 6):
        total = total + str(i)        # this is O(n), not O(n²)
        if i < 5:
            total = total + ", "
    print(total + "\n")               # "1, 2, 3, 4, 5"
```

### The `char` type

`char` is a single byte. Use `'x'` syntax:

```
s := "hello"
# append a single byte without allocating
s = s + '\n'            # zero-alloc byte append
# build characters from byte values
s = s + ('0' + 5)       # appends '5'
```

---

## 7. Arrays

Arrays are **dynamic, heap-allocated, value-semantic** lists. Assigning one makes a deep copy.

```
fn main():
    # literals
    xs := [10, 20, 30]           # [int]
    names := ["Ada", "Alan"]     # [string]
    empty := []int               # empty, element type required

    # operations
    push(xs, 40)                 # append in place: [10, 20, 30, 40]
    last := pop(xs)              # remove + return last: 40
    print(str(len(xs)) + "\n")   # 3

    # indexing (bounds-checked)
    print(str(xs[0]) + "\n")     # 10
    xs[0] = 99                   # write
    print(str(xs[0]) + "\n")     # 99

    # value semantics: assignment is a deep copy
    ys := xs                     # ys is an independent copy
    ys[0] = 0                    # mutating ys never touches xs
    print(str(xs[0]) + "\n")     # still 99
```

### Arrays of structs

```
struct Point:
    x: int
    y: int

fn main():
    ps := [Point(1, 2), Point(3, 4)]
    push(ps, Point(5, 6))
    ps[0].x = 10                    # write through an element
    print(str(ps[0].x) + "\n")      # 10
```

### Arrays as function parameters

A `[int]` parameter is a **read-only borrow** — no copy, but you can't mutate it:

```
fn sum(a: [int]) -> int:
    total := 0
    for i in range(len(a)):
        total = total + a[i]
    return total

fn add_one(a: mut [int]):
    for i in range(len(a)):
        a[i] = a[i] + 1            # mutate the caller's array

fn main():
    xs := [1, 2, 3]
    print(str(sum(xs)) + "\n")      # 6 (no copy)
    add_one(&xs)                    # xs is now [2, 3, 4]
```

### Slices

`xs[a:b]` creates a sub-range — a **zero-copy view** when passed to a function, a **deep copy** when stored:

```
fn sum(a: [int]) -> int:
    total := 0
    for v in a:
        total = total + v
    return total

fn main():
    xs := [10, 20, 30, 40, 50]
    print(str(sum(xs[1:4])) + "\n")   # 90 — zero-copy view
    mid := xs[1:4]                    # deep copy: [20, 30, 40]
```

### Returning arrays from functions

```
fn make_squares(n: int) -> [int]:
    r := []int
    for i in range(n):
        push(r, i * i)
    return r                # promoted into the caller's arena

fn main():
    sq := make_squares(5)   # [0, 1, 4, 9, 16]
    for v in sq:
        print(str(v) + " ")
    print("\n")
```

---

## 8. Structs

Structs are **named product types** with value semantics. Think of them as plain objects that are always deep-copied.

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
    # positional construction (fields in declaration order)
    a := Point(1, 2)
    r := Rect(a, Point(4, 6))

    print(str(area(r)) + "\n")       # 9

    # value semantics: copies are independent
    b := a               # b is a deep copy
    b.x = 99             # never touches a

    # field access and nested writes
    r.lo.x = 100         # write a nested field in place
```

### Structs with heap fields

A struct can own strings, arrays, and other structs:

```
struct Person:
    name: string
    tags: [string]

fn main():
    a := Person("Ada", ["systems", "compiled"])
    b := a                        # deep copy: b.name and b.tags are independent
    b.name = "Alan"               # never touches a
    push(b.tags, "logic")         # never touches a.tags
```

### Mutable context objects with `mut`

```
struct Ctx:
    label: string
    log: [int]

fn step(ctx: mut Ctx, name: string, code: int):
    ctx.label = name
    push(ctx.log, code)

fn main():
    ctx := Ctx("start", [0])
    snap := ctx                      # snapshot (deep copy)
    step(&ctx, "phase-1", 10)        # mutates ctx
    step(&ctx, "phase-2", 20)        # mutates ctx
    print("label: " + ctx.label + "\n")                # "phase-2"
    print("snapshot label: " + snap.label + "\n")      # "start" (isolated)
    print("log entries: " + str(len(ctx.log)) + "\n")  # 3
```

---

## 9. Maps

Maps are key-value stores with `string` or `int` keys. Like arrays, they're value-semantic — assigning one makes a deep copy.

```
fn main():
    # literals and empty maps
    counts := ["ada": 1, "alan": 2]       # [string: int]
    empty := []string: int                 # empty map

    # read
    v := map_get(counts, "ada", 0)         # 1 (0 is the default if absent)
    has := "grace" in counts               # false

    # update
    counts["grace"] = 5                    # add/overwrite a key
    delete counts["alan"]                  # remove a key (no-op if absent)

    # iterate
    ks := keys(counts)                     # [string] of keys (unordered)
    for i in range(len(ks)):
        k := ks[i]
        print(k + ": " + str(map_get(counts, k, 0)) + "\n")

    # size
    print(str(len(counts)) + "\n")
```

### The counter idiom

The canonical pattern for building a frequency map:

```
counts := []string: int
words := split("the cat sat on the mat", " ")
for i in range(len(words)):
    w := words[i]
    counts[w] = map_get(counts, w, 0) + 1
```

Despite looking like it rebuilds the map each step, the compiler **mutates in place** because value semantics proves `counts` is uniquely owned — O(n) total, not O(n²).

### `m[k]` is a place

`m[k]` names a value's storage slot, so you write through it directly:

```
fn main():
    counts := []string: int
    counts["ada"] = 1          # write a key
    counts["ada"] += 1         # compound update -> 2
    counts["new"] += 5         # missing key starts at 0, then += 5 -> 5

    n := counts["ada"]         # READ -> 2
    miss := counts["nope"]     # READ of a missing key -> 0 (the value type's zero)
    print(str(n) + " " + str(miss) + " " + str(len(counts)) + "\n")   # 2 0 2
```

Two things to know about reading `m[k]`:

- **It's a pure read with a zero default.** A missing key yields the value
  type's zero — `0` for int, `0.0` for float, `""` for string, `false` for bool
  — and **does not insert anything** (the `nope` read above leaves `len` at 2).
  This is exactly `map_get(m, k, <zero>)`; use `map_get(m, k, myDefault)`
  directly when you want a different default.
- **Reads work for any value type.** For composite values (arrays, structs,
  maps) a missing key gives the empty/zeroed value — an empty array, a struct
  with zero fields — still without inserting. So `len(counts[k])`, `m[k].field`,
  and passing `m[k]` along all read naturally; writes (`m[k] = v`,
  `m[k].field = x`, `push(m[k], v)`, `m[k][j] = x`) work for any value type too.

So the counter idiom reads most naturally as:

```
counts := []string: int
for i in range(len(words)):
    counts[words[i]] += 1          # or: counts[w] = counts[w] + 1
```

### Maps with `mut`

```
fn tally(m: mut [string: int], word: string):
    m[word] = map_get(m, word, 0) + 1

fn main():
    counts := []string: int
    tally(&counts, "hello")
    tally(&counts, "hello")
    print(str(map_get(counts, "hello", 0)) + "\n")   # 2
```

---

## 10. Option and Result

### Option(T) — no null

Instead of `null`, Hier uses `Option(T)`: it's either `Some(value)` or `None`.

```
fn index_of(xs: [int], target: int) -> Option(int):
    for i in range(len(xs)):
        if xs[i] == target:
            return Some(i)
    return None

fn main():
    match index_of([10, 20, 30], 20):
        Some(i):
            print("found at " + str(i) + "\n")    # runs
        None:
            print("not found\n")
```

The `match` is **exhaustive** — you must handle both `Some` and `None`. This is the compiler ensuring you don't forget the null check.

### Result(T, E) — no exceptions

Instead of throwing exceptions, functions that can fail return `Result(T, E)`:

```
fn checked_div(a: int, b: int) -> Result(int, string):
    if b == 0:
        return Err("divide by zero")
    return Ok(a / b)

fn main():
    match checked_div(10, 0):
        Ok(v):
            print("= " + str(v) + "\n")
        Err(e):
            print("error: " + e + "\n")    # runs: "error: divide by zero"
```

### `or_return` — error propagation

When you want to unwrap a `Result` and propagate errors, use `or_return`:

```
fn parse_digit(s: string) -> Result(int, string):
    if len(s) == 1 and s[0] >= 48 and s[0] <= 57:
        return Ok(s[0] - 48)
    return Err("not a digit: " + s)

fn add_two(a: string, b: string) -> Result(int, string):
    x := parse_digit(a) or_return    # unwrap, or return Err from add_two
    y := parse_digit(b) or_return
    return Ok(x + y)

fn main():
    match add_two("3", "7"):
        Ok(v):
            print(str(v) + "\n")     # 10
        Err(e):
            print("error: " + e + "\n")
```

---

## 11. Enums and Pattern Matching

An `enum` is a tagged union — a value that is exactly one of several named variants, each optionally carrying data.

```
enum Shape:
    Circle(float)                    # radius
    Rect(float, float)               # width, height

fn area(s: Shape) -> float:
    match s:
        Circle(r):
            return 3.14159 * r * r
        Rect(w, h):
            return w * h

fn main():
    c := Circle(2.0)
    r := Rect(3.0, 4.0)
    print(str(area(c)) + "\n")       # ~12.57
    print(str(area(r)) + "\n")       # 12.0
```

### Recursive enums (ASTs)

Enums can carry themselves, making trees:

```
enum Expr:
    Num(int)
    Add(Expr, Expr)
    Mul(Expr, Expr)

fn eval(e: Expr) -> int:
    match e:
        Num(v):
            return v
        Add(l, r):
            return eval(l) + eval(r)
        Mul(l, r):
            return eval(l) * eval(r)

fn main():
    # (2 + 3) * 4 = 20
    expr := Mul(Add(Num(2), Num(3)), Num(4))
    print(str(eval(expr)) + "\n")    # 20
```

Every `match` must be **exhaustive** — every variant needs an arm. The compiler won't let you forget a case.

Enums are value-semantic too: copying an enum value deep-copies the whole tree.

---

## 12. Tuples and Multiple Return Values

```
fn divmod(a: int, b: int) -> (int, int):
    q := a / b
    return q, a - q * b

fn main():
    quot, rem := divmod(17, 5)     # destructuring: quot=3, rem=2
    print(str(quot) + " " + str(rem) + "\n")

    # tuples are first-class values
    t := divmod(17, 5)
    print(str(t.0) + " " + str(t.1) + "\n")    # 3 2
```

---

## 13. Type Inference

Hier infers types — you don't annotate everything:

```
x := 42                # int (inferred from the literal)
s := "hello"           # string
xs := [1, 2, 3]        # [int]
f := 1.5               # float
g := f + 2             # float (int literal adapts to float context)

# empty collections need a type hint
ys : [int] = []        # annotation supplies the element type
m := []string: int     # empty [string: int] map

# bidirectional: the expected type flows into subexpressions
# (the return type of a function tells what None means)
fn maybe() -> Option(int):
    return None        # None's type comes from the return annotation
```

Function signatures are always explicit — they're the module interface.

---

## 14. Value Semantics: The Mental Model

This is the most important concept in Hier. Internalize these three rules and everything else follows:

### Rule 1: Assignment copies

```
a := [1, 2, 3]
b := a              # b gets its own independent copy
push(b, 4)
print(str(len(a)))  # 3 — a is unchanged
```

### Rule 2: Parameters are copies (unless `mut`)

```
fn add_one(xs: [int]) -> [int]:
    push(xs, 1)           # ERROR: xs is a read-only borrow
    return xs             # would return the borrow — also wrong

fn add_one(xs: [int]) -> [int]:
    out := xs             # copy
    push(out, 1)          # mutate the copy
    return out            # return the new array
```

### Rule 3: No two variables ever share storage

```
a := Person("Ada", ["x"])
b := a               # deep copy — a and b are 100% independent
b.name = "Alan"
b.tags[0] = "y"
# a.name is still "Ada", a.tags[0] is still "x"
```

### Why this matters: automatic memory management

Because every value has exactly one owner, the compiler can attach each value to a **scope** (a function, a block, a loop iteration). When the scope ends, all its values are freed at once — no garbage collector, no reference counting, no `free()` calls.

```
fn main():
    total := ""                        # main's arena
    for i in range(1000000):
        # each iteration gets a scratch arena that resets afterward
        tmp := str(i) + " "            # allocated in the scratch arena
        total = total + tmp            # total lives in main's arena — survives
    print(str(len(total)) + "\n")
    # main's arena is freed here
```

The loop runs a million iterations in **constant memory** because each iteration's temporary data is reclaimed when the scratch arena resets.

---

## 15. Closures and Higher-Order Functions

### Named functions as values

```
fn dbl(x: int) -> int:
    return x * 2

fn apply(g: fn(int) -> int, x: int) -> int:
    return g(x)

fn main():
    f := dbl                           # f : fn(int) -> int
    print(str(f(5)) + "\n")            # 10 (indirect call)
    print(str(apply(dbl, 21)) + "\n")  # 42
```

### Lambdas (anonymous functions)

A lambda body is a single expression (implicit return):

```
fn apply(f: fn(int) -> int, x: int) -> int:
    return f(x)

fn main():
    # lambda with explicit types
    print(str(apply(fn(x: int) -> int: x * x, 3)) + "\n")  # 9

    # lambda with inferred types (from apply's signature)
    print(str(apply(fn(x): x * 2, 5)) + "\n")               # 10
```

### Closures capture by value

```
fn main():
    n := 10
    addn := fn(x: int) -> int: x + n     # captures n by deep copy
    print(str(addn(5)) + "\n")           # 15
```

The captured variable is **deep-copied** into the closure when created. Mutating the original afterward doesn't affect the closure:

```
fn main():
    a := [10, 20]
    get_len := fn() -> int: len(a)
    push(a, 30)                  # mutate the original
    print(str(get_len()))        # 2 — closure kept its own copy
```

### Closures can escape (be returned)

```
fn make_adder(n: int) -> fn(int) -> int:
    return fn(x: int) -> int: x + n

fn main():
    add5 := make_adder(5)
    print(str(add5(100)) + "\n")    # 105
```

The captured `n` is automatically re-homed into the caller's arena — no lifetime annotations needed.

### Closures in structs and arrays

```
struct Handler:
    cb: fn(int) -> int

fn main():
    h := Handler(fn(x: int) -> int: x + 1)
    print(str(h.cb(5)) + "\n")       # 6

    ops := [fn(x: int) -> int: x * 2, fn(x: int) -> int: x + 10]
    print(str(ops[0](5)) + "\n")     # 10
    print(str(ops[1](5)) + "\n")     # 15
```

---

## 16. Methods (UFCS)

`x.foo(a, b)` is sugar for `foo(x, a, b)` — Uniform Function Call Syntax. Any function whose first parameter matches `x`'s type can be called this way. No classes, no `self`:

```
struct Vec3:
    x: float
    y: float
    z: float

fn vadd(a: Vec3, b: Vec3) -> Vec3:
    return Vec3(a.x + b.x, a.y + b.y, a.z + b.z)

fn vlen(v: Vec3) -> float:
    return sqrt(v.x * v.x + v.y * v.y + v.z * v.z)

fn main():
    a := Vec3(1.0, 2.0, 3.0)
    b := Vec3(4.0, 5.0, 6.0)
    c := a.vadd(b)        # sugar for vadd(a, b)
    print(str(c.vlen()))  # sugar for vlen(c), prints ~9.54...
```

Calls chain: `a.vadd(b).vlen()` = `vlen(vadd(a, b))`.

---

## 17. Concurrency

Hier's concurrency model is **race-free by construction**. Every value that crosses a thread boundary is deep-copied. No locks, no lifetimes, no `Send`/`Sync` bounds.

### spawn / wait

```
fn fib(n: int) -> int:
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

fn main():
    t1 := spawn fib(30)       # runs on another thread (arg deep-copied)
    t2 := spawn fib(28)       # runs in parallel
    a := wait(t1)             # blocks until done, result deep-copied back
    b := t2.wait()            # alternative syntax
    print(str(a + b) + "\n")
```

Tasks are **structured**: a task handle can't be copied, stored, or leaked. It must be waited exactly once. Un-waited tasks are implicitly joined when their scope exits.

### parallel for

```
fn score(n: int) -> int:
    return n * n

fn main():
    total := 0
    parallel for i in range(1000000):
        total += score(i)         # reduction: safe, deterministic
    print(str(total) + "\n")
```

Reduction operators (`+=`, `*=`) are safe because each thread accumulates locally, then the results are folded in order. Any other write to an outer variable is a compile error.

### Channels

```
fn producer(ch: Channel(string), n: int) -> int:
    for i in range(n):
        ch.send("item-" + str(i))    # deep-copies payload in
    return n

fn consumer(ch: Channel(string)) -> int:
    count := 0
    for true:
        match recv(ch):               # deep-copies payload out
            Some(s):
                count = count + len(s)
            None:
                return count

fn main():
    ch := channel(string, 16)         # bounded queue
    c := spawn consumer(ch)
    p := spawn producer(ch, 100)
    wait(p)
    close(ch)                         # signal no more sends
    print(str(wait(c)) + "\n")
```

Channels are bounded lock-free queues. `send` blocks when full, `recv` blocks when empty, and `recv` returns `None` after `close` drains the last item.

---

## 18. Packages and the Standard Library

Hier has packages: a directory of `.hi` files sharing one namespace.

```
package main
import "core:math"
import "core:strings"
import s "core:sort"           # alias

fn main():
    print(str(math.gcd(12, 18)) + "\n")       # 6
    print(strings.to_upper("hello") + "\n")   # "HELLO"
    idx := s.argsort([3, 1, 4, 1, 5])
    for i in range(len(idx)):
        print(str(idx[i]) + " ")
    print("\n")
```

### Corelib packages

| Package | What it provides |
|---|---|
| `core:math` | `abs`, `imin`, `imax`, `clamp`, `sign`, `gcd`, `ipow` |
| `core:strings` | `to_upper`, `to_lower`, `starts_with`, `ends_with`, `contains`, `repeat`, `trim`, `parse_int`, `lines`, `replace` |
| `core:arrays` | `[int]` utilities: `contains`, `index_of`, `count`, `sum`, `reverse`, `sort` |
| `core:arrays_str` | `[string]` utilities: `contains`, `index_of`, `join`, `sort` |
| `core:arrays_float` | `[float]` utilities |
| `core:iter` | Higher-order over `[int]`: `map`, `filter`, `reduce`, `count`, `any` |
| `core:iter_str` | Higher-order over `[string]` |
| `core:iter_float` | Higher-order over `[float]` |
| `core:sort` | `argsort`, `argsort_desc`, `argsort_str`, `by_key` |
| `core:rand` | `seed`, `next`, `below`, `shuffle` (xorshift32, state threaded via mut) |

---

## 19. Calling C (FFI)

`extern fn` declares a C function Hier can call:

```
extern fn getpid() -> int
extern "m" fn cos(x: float) -> float       # links -lm

fn main():
    print(str(getpid()) + "\n")
    print(str(cos(0.0)) + "\n")             # 1.0
```

The boundary covers scalars (`int`, `float`, `bool`), strings (C strings are copied into Hier's arena), and the opaque `ptr` type for foreign handles. Composites and `mut` are rejected for safety.

---

## 20. Project: Inverted-Index Search Engine

This is a real text search engine — index documents, query them with boolean AND, count term frequencies. Adapted from `examples/invindex.hi`.

```
# invindex.hi — An inverted-index search engine in ~80 lines
#
# Concepts exercised:
#   - structs with array fields (Posting)
#   - [string: int] maps for the term index
#   - mut for shared mutable state across calls
#   - in-place map/array mutation through projections
#   - string processing (split, normalize)

struct Posting:
    term: string
    docs: [int]      # document IDs containing this term
    freqs: [int]     # parallel: how many times in each doc

# Normalize: lowercase ASCII, drop non-letters
fn normalize(w: string) -> string:
    r := ""
    for i in range(len(w)):
        c := w[i]
        if c >= 65 and c <= 90:            # A-Z -> a-z
            r = r + chr(c + 32)
        elif c >= 97 and c <= 122:         # a-z
            r = r + chr(c)
    return r

# Add one occurrence of `term` in document `doc`
fn add(postings: mut [Posting], tidx: mut [string: int], term: string, doc: int):
    pi := map_get(tidx, term, -1)
    if pi < 0:
        # first time we see this term — create a new posting
        p := Posting(term, []int, []int)
        push(p.docs, doc)
        push(p.freqs, 1)
        push(postings, p)
        tidx[term] = len(postings) - 1
    else:
        n := len(postings[pi].docs)
        if n > 0 and postings[pi].docs[n - 1] == doc:
            # same doc again — bump the count
            postings[pi].freqs[n - 1] += 1
        else:
            # new document for this term
            push(postings[pi].docs, doc)
            push(postings[pi].freqs, 1)

# Index all words in one document
fn index_doc(postings: mut [Posting], tidx: mut [string: int], text: string, doc: int):
    words := split(text, " ")
    for i in range(len(words)):
        t := normalize(words[i])
        if len(t) > 0:
            add(&postings, &tidx, t, doc)

fn contains(xs: [int], v: int) -> bool:
    for x in xs:
        if x == v:
            return true
    return false

# Boolean AND: docs containing ALL query terms
fn query_and(postings: [Posting], tidx: [string: int], terms: [string]) -> [int]:
    res := []int
    if len(terms) == 0:
        return res
    p0 := map_get(tidx, terms[0], -1)
    if p0 < 0:
        return res
    for ci in range(len(postings[p0].docs)):
        doc := postings[p0].docs[ci]
        ok := true
        for ti in range(1, len(terms)):
            pj := map_get(tidx, terms[ti], -1)
            if pj < 0 or not contains(postings[pj].docs, doc):
                ok = false
        if ok:
            push(res, doc)
    return res

fn main():
    # corpus of documents
    corpus := []string
    push(corpus, "The quick brown Fox jumps.")
    push(corpus, "The lazy dog sleeps all day.")
    push(corpus, "A quick brown dog runs fast!")
    push(corpus, "The Fox and the dog play together.")
    push(corpus, "Lazy cats sleep; quick foxes run.")

    # build the index
    postings := []Posting
    tidx := []string: int
    for d in range(len(corpus)):
        index_doc(&postings, &tidx, corpus[d], d)

    print("docs=" + str(len(corpus)) + " vocab=" + str(len(postings)) + "\n")

    # query: documents mentioning both "the" and "dog"
    q := []string
    push(q, "the")
    push(q, "dog")
    result := query_and(postings, tidx, q)
    s := "AND(the, dog) -> ["
    for i in range(len(result)):
        if i > 0:
            s = s + ", "
        s = s + str(result[i])
    print(s + "]\n")
```

**What this exercises:**
- `mut` arrays and maps shared across recursive calls
- In-place mutation through projections (`postings[pi].freqs[n - 1] += 1`)
- String processing (`split`, character arithmetic, `chr`)
- The map accumulator idiom (`tidx[term] = ...`)

---

## 21. Project: JSON Parser

A full recursive-descent JSON parser + serializer in ~200 lines. This is `examples/json.hi`, adapted for learning.

The key insight: the parsed document is a recursive `Json` enum — an AST. The parser builds it by recursive descent, the serializer walks it, and **zero memory management appears in the source**. Every node lives in a lexical arena and is freed when its scope exits.

```
# json.hi — A JSON parser + serializer
#
# Concepts exercised:
#   - recursive enums (ASTs)
#   - exhaustive pattern matching
#   - mut for threaded parser state
#   - string building with char type
#   - value semantics: the tree is an independent value

enum Json:
    JNull
    JBool(bool)
    JNum(int)
    JStr(string)
    JArr([Json])
    JObj([string], [Json])         # parallel keys / values arrays

# skip whitespace
fn skip_ws(s: string, n: int, pos: mut int):
    for pos < n and (s[pos] == 32 or s[pos] == 9 or s[pos] == 10 or s[pos] == 13):
        pos = pos + 1

fn parse_string(s: string, n: int, pos: mut int) -> string:
    pos = pos + 1                   # skip opening "
    r := ""
    for pos < n and s[pos] != 34:   # until closing "
        if s[pos] == 92:            # backslash escape
            pos = pos + 1
            e := s[pos]
            if e == 110:            # n -> \n
                r = r + '\n'
            elif e == 116:          # t -> \t
                r = r + '\t'
            elif e == 34:           # " -> "
                r = r + '"'
            elif e == 92:           # \ -> \
                r = r + '\\'
            else:
                r = r + ('\0' + e)  # raw byte via char type
        else:
            r = r + ('\0' + s[pos]) # regular character via char
        pos = pos + 1
    pos = pos + 1                   # skip closing "
    return r

fn parse_number(s: string, n: int, pos: mut int) -> int:
    neg := false
    if s[pos] == 45:                # '-'
        neg = true
        pos = pos + 1
    v := 0
    for pos < n and s[pos] >= 48 and s[pos] <= 57:
        v = v * 10 + (s[pos] - 48)
        pos = pos + 1
    if neg:
        return 0 - v
    return v

fn parse_value(s: string, n: int, pos: mut int) -> Json:
    skip_ws(s, n, &pos)
    c := s[pos]
    if c == 123:                    # { -> object
        pos = pos + 1
        ks := []string
        vs := []Json
        skip_ws(s, n, &pos)
        for pos < n and s[pos] != 125:
            k := parse_string(s, n, &pos)
            skip_ws(s, n, &pos)
            pos = pos + 1           # skip :
            push(ks, k)
            push(vs, parse_value(s, n, &pos))
            skip_ws(s, n, &pos)
            if pos < n and s[pos] == 44:   # ,
                pos = pos + 1
                skip_ws(s, n, &pos)
        pos = pos + 1               # skip }
        return JObj(ks, vs)
    if c == 91:                     # [ -> array
        pos = pos + 1
        xs := []Json
        skip_ws(s, n, &pos)
        for pos < n and s[pos] != 93:
            push(xs, parse_value(s, n, &pos))
            skip_ws(s, n, &pos)
            if pos < n and s[pos] == 44:
                pos = pos + 1
                skip_ws(s, n, &pos)
        pos = pos + 1               # skip ]
        return JArr(xs)
    if c == 34:                     # " -> string
        return JStr(parse_string(s, n, &pos))
    if c == 116:                    # true
        pos = pos + 4
        return JBool(true)
    if c == 102:                    # false
        pos = pos + 5
        return JBool(false)
    if c == 110:                    # null
        pos = pos + 4
        return JNull
    return JNum(parse_number(s, n, &pos))

# --- serializer: walk the tree, build a string ---

fn esc(s: string) -> string:
    out := ""
    for i in range(len(s)):
        c := s[i]
        if c == 34:
            out = out + "\\\""
        elif c == 92:
            out = out + "\\\\"
        elif c == 10:
            out = out + "\\n"
        elif c == 9:
            out = out + "\\t"
        else:
            out = out + ('\0' + c)
    return out

fn to_json(j: Json) -> string:
    match j:
        JNull:
            return "null"
        JBool(b):
            if b:
                return "true"
            return "false"
        JNum(x):
            return str(x)
        JStr(s):
            return "\"" + esc(s) + "\""
        JArr(xs):
            out := "["
            for i in range(len(xs)):
                if i > 0:
                    out = out + ","
                out = out + to_json(xs[i])
            return out + "]"
        JObj(ks, vs):
            out := "{"
            for i in range(len(ks)):
                if i > 0:
                    out = out + ","
                out = out + "\"" + esc(ks[i]) + "\":" + to_json(vs[i])
            return out + "}"

# --- query helpers ---

fn get(j: Json, key: string) -> Json:
    match j:
        JObj(ks, vs):
            for i in range(len(ks)):
                if ks[i] == key:
                    return vs[i]
            return JNull
        JNull:
            return JNull
        JBool(b):
            return JNull
        JNum(x):
            return JNull
        JStr(s):
            return JNull
        JArr(xs):
            return JNull

fn as_num(j: Json) -> int:
    match j:
        JNum(x):
            return x
        JNull:
            return 0
        JBool(b):
            return 0
        JStr(s):
            return 0
        JArr(xs):
            return 0
        JObj(ks, vs):
            return 0

fn main():
    src := "{\"name\": \"hier\", \"version\": 7, \"tags\": [\"systems\", \"arena\"], \"nested\": {\"ok\": true, \"n\": -3}}"
    pos := 0
    j := parse_value(src, len(src), &pos)

    # round-trip through serializer
    print("round-trip: " + to_json(j) + "\n")

    # query the tree
    print("version = " + str(as_num(get(j, "version"))) + "\n")
    print("nested.n = " + str(as_num(get(get(j, "nested"), "n"))) + "\n")
```

**What this exercises:**
- Recursive enums as ASTs
- Threaded mutable state via `mut int` (the parser position)
- The `char` type for byte-level string building
- Exhaustive `match` on every variant
- Deep value queries into nested structures

---

## 22. Project: Ray Tracer

A tiny diffuse ray tracer rendering spheres to a PPM image. Exercises float math, nested structs, arrays of structs, and the O(n) in-place string accumulator.

```
# raytrace.hi — A diffuse ray tracer
#
# Concepts exercised:
#   - float math (sqrt, arithmetic)
#   - structs with struct fields (Vec3 in Sphere)
#   - arrays of structs
#   - in-place string accumulator for the output buffer

struct Vec3:
    x: float
    y: float
    z: float

struct Sphere:
    center: Vec3
    radius: float
    color: Vec3

fn vsub(a: Vec3, b: Vec3) -> Vec3:
    return Vec3(a.x - b.x, a.y - b.y, a.z - b.z)

fn vscale(a: Vec3, s: float) -> Vec3:
    return Vec3(a.x * s, a.y * s, a.z * s)

fn vdot(a: Vec3, b: Vec3) -> float:
    return a.x * b.x + a.y * b.y + a.z * b.z

fn vlen(a: Vec3) -> float:
    return sqrt(vdot(a, a))

fn vnorm(a: Vec3) -> Vec3:
    l := vlen(a)
    return vscale(a, 1.0 / l)

fn hit_sphere(o: Vec3, d: Vec3, s: Sphere) -> float:
    oc := vsub(o, s.center)
    b := vdot(oc, d)
    c := vdot(oc, oc) - s.radius * s.radius
    disc := b * b - c
    if disc < 0.0:
        return -1.0
    t := 0.0 - b - sqrt(disc)
    if t < 0.001:
        return -1.0
    return t

fn clamp01(x: float) -> float:
    if x < 0.0:
        return 0.0
    if x > 1.0:
        return 1.0
    return x

fn comp(x: float) -> string:
    return str(to_int(clamp01(x) * 255.0))

fn main():
    nx := 120
    ny := 80

    # scene: four spheres
    scene := []Sphere
    push(scene, Sphere(Vec3(0.0, -0.5, -3.0), 0.5, Vec3(0.9, 0.2, 0.2)))
    push(scene, Sphere(Vec3(1.1, -0.4, -3.5), 0.6, Vec3(0.2, 0.4, 0.9)))
    push(scene, Sphere(Vec3(-1.0, -0.3, -2.5), 0.4, Vec3(0.2, 0.8, 0.3)))
    push(scene, Sphere(Vec3(0.0, -100.5, -3.0), 100.0, Vec3(0.6, 0.6, 0.6)))

    light := vnorm(Vec3(-1.0, 1.0, 0.5))
    origin := Vec3(0.0, 0.0, 0.0)

    out := "P3\n" + str(nx) + " " + str(ny) + "\n255\n"

    for j in range(ny):
        for i in range(nx):
            u := (to_float(i) / to_float(nx)) * 2.0 - 1.0
            v := 1.0 - (to_float(j) / to_float(ny)) * 2.0
            dir := vnorm(Vec3(u * 1.5, v, -1.0))

            best := 1000000.0
            hit := -1
            for k in range(len(scene)):
                t := hit_sphere(origin, dir, scene[k])
                if t > 0.0 and t < best:
                    best = t
                    hit = k

            r := 0.0
            g := 0.0
            b := 0.0
            if hit < 0:
                # sky gradient
                tt := 0.5 * (dir.y + 1.0)
                r = (1.0 - tt) + tt * 0.5
                g = (1.0 - tt) + tt * 0.7
                b = (1.0 - tt) + tt * 1.0
            else:
                s := scene[hit]
                p := Vec3(origin.x + dir.x * best, origin.y + dir.y * best, origin.z + dir.z * best)
                n := vnorm(vsub(p, s.center))
                diff := vdot(n, light)
                if diff < 0.0:
                    diff = 0.0
                sh := 0.15 + 0.85 * diff
                r = s.color.x * sh
                g = s.color.y * sh
                b = s.color.z * sh

            # IMPORTANT: each piece is a separate `out = out + X`
            # so the in-place string-append optimization fires.
            out = out + comp(r)
            out = out + " "
            out = out + comp(g)
            out = out + " "
            out = out + comp(b)
            out = out + "\n"

    print(out)
```

Compile and render:

```
./hierc raytrace.hi
./raytrace > out.ppm
```

Open `out.ppm` in any image viewer.

---

## 23. Cheat Sheet

### Declarations

```
x := 42                # inferred declaration
y : int = 10           # explicit type
x = 43                 # assignment (variable must exist)
x += 1                 # compound: += -= *= /= %= &= |= ^= <<= >>=
```

### Types

```
int     float    bool    string    char
[T]     [K: V]   Option(T)   Result(T, E)
(T1, T2)   struct    enum    type New = Old
```

### Functions

```
fn name(a: int, b: int) -> int:
    return a + b

fn void_fn():
    print("no return\n")

fn mutate(x: mut int):
    x = x + 1
```

### Control flow

```
if cond: ... elif cond: ... else: ...
for cond: ...                         # while
for i in range(n): ...                # 0..n-1
for i in range(a, b, step): ...       # a..b-1
for x in xs: ...                      # foreach
break    continue
match expr: Some(v): ... None: ...    # exhaustive
```

### Strings

```
"hello" + " world"          # concatenate
f"{name} is {age}"          # interpolate
len(s)  substr(s, a, b)  find(s, sub)  split(s, sep)
str(42)  chr(65)  s[i]      # byte at index (read-only)
'\n'  '\t'  '\0' + n        # char literals and byte arithmetic
```

### Arrays

```
[1, 2, 3]          # literal
[]int               # empty
push(xs, 4)         # append
pop(xs)             # remove last
len(xs)             # length
xs[i]               # read/write (bounds-checked)
xs[a:b]             # slice (view or copy depending on context)
```

### Maps

```
["a": 1, "b": 2]                # literal
[]string: int                    # empty
m[k] = v          map_get(m, k, default)
k in m            delete m[k]
keys(m)           len(m)
m[k] = map_get(m, k, 0) + 1     # in-place accumulator idiom
```

### Structs

```
struct Point:
    x: int
    y: int

p := Point(1, 2)     # construct (positional)
q := p               # deep copy
p.x = 10             # field write
```

### Enums

```
enum Expr:
    Num(int)
    Add(Expr, Expr)

match e:
    Num(v): ...
    Add(l, r): ...                 # exhaustive
```

### Option / Result

```
Some(42)   None           # Option(T)
Ok(42)     Err("bad")    # Result(T, E)
v := expr or_return       # unwrap or propagate Err
```

### Concurrency

```
t := spawn f(args)         # launch on another thread
wait(t)   t.wait()         # join (exactly once)
parallel for i in range(n):
    total += work(i)       # reduction
ch := channel(T, cap)      # bounded queue
ch.send(v)   recv(ch)      # deep-copy in/out
close(ch)                   # drain then None
```

### Packages

```
package main
import "core:math"
import s "core:sort"
math.gcd(12, 8)   s.argsort(xs)
```

### Memory model rules

1. **Every scope** gets its own arena — functions, blocks, loop iterations.
2. **Assignment copies** — `b = a` gives `b` an independent deep copy.
3. **Parameters are borrows** (read-only) — copy first or use `mut` to mutate.
4. **Return values are promoted** into the caller's arena — no dangling pointers.
5. **Loop scratch arenas reset** each iteration — bounded memory in loops.
6. **The programmer never sees any of this** — no `malloc`, `free`, or arena API.

---

## Where to Go Next

- **Read the examples:** `examples/` has 20 programs, from trivial to substantial.
- **Read the tests:** `tests/*.hi` covers every language feature with focused examples.
- **Read the thesis:** `docs/thesis.md` explains *why* value semantics makes implicit arenas work — and where it doesn't.
- **Read the source:** The self-hosted compiler `compiler/hierc0.hi` is ~10,000 lines of Hier written in Hier — a real program that exercises every feature.
- **Run the benchmarks:** `make bench` to see the performance properties for yourself.
- **Try the fuzzer:** `make fuzz` to see how the two compilers are checked against each other.

---

*The language is deliberately small — one way to do each thing — and it's an experimental proof-of-concept, not a production toolchain. But what's there is tested, measured, and self-hosting. Welcome to systems programming without the memory management.*
