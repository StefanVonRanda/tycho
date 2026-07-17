# Tutorial

A guided first hour with Tycho. By the end you'll have written a small real program and
will understand the one idea that makes the language different — where the memory
management went.

This assumes you can already build the compiler; if not, see
[Trying it](../README.md#trying-it) in the README (it's `git clone` + `make`). Every
snippet below is real, runnable Tycho. Save one to `f.ty` and run it with:

```
$ ./tychoc f.ty && ./f
```

`./tychoc f.ty` transpiles to C and compiles a native binary `f`.

## 1. Hello

```
fn greet(name: string) -> string:
    return "hello " + name

fn main():
    print("what is your name: ")
    name := input()
    println(greet(name))
```

`fn` declares a function; `-> string` is its return type; `main` is the entry point.
`name := input()` declares a variable and infers its type. Indentation delimits blocks,
Python-style.

## 2. Values, and copies

Every binding is an independent value. `:=` infers the type; `x : T = …` names it.

```
fn main():
    a := [1, 2, 3]
    b := a            # b is a full, independent copy of a
    b[0] = 99
    println(str(a[0]) + " " + str(b[0]))   # 1 99  — a is untouched
```

There is no reference type: you can never hold a pointer into another value's memory.
Hold that thought — it's the whole trick, and section 9 explains what it buys you.

## 3. Control flow

```
fn main():
    for i in range(5):            # 0, 1, 2, 3, 4
        if i % 2 == 0:
            println(str(i) + " even")
        else:
            println(str(i) + " odd")

    xs := [10, 20, 30]
    for x in xs:                  # foreach over an array
        println(str(x))
```

`range(a, b)` and `range(a, b, step)` also exist; `for cond:` is the while-loop form.

## 4. Collections

Arrays are `[T]`; maps are `[K: V]`. `push` appends; `m.get(key, default)` reads safely.

```
fn main():
    names := ["Ada", "Bo", "Cy"]
    push(names, "Di")

    ages : [string: int] = []
    ages["Ada"] = 36
    ages["Bo"] = 41
    println("Ada is " + str(ages.get("Ada", 0)))
    println(str(len(names)) + " names")
```

## 5. Structs

```
struct Student:
    name: string
    score: int

fn main():
    s := Student("Ada", 91)
    println(s.name + " scored " + str(s.score))
```

A struct is a value like any other: copying it copies its whole tree, and it's freed when
its scope ends.

## 6. Enums, match, and Option

An `enum` is a tagged union; `match` is exhaustive — every variant needs an arm, checked
at compile time.

```
enum Grade:
    A
    B
    C
    F

fn grade_name(g: Grade) -> string:
    return match g:            # match is an expression: each arm is a value
        A: "A"
        B: "B"
        C: "C"
        F: "F"
```

`Option(T)` is the built-in enum for "a value or nothing" — variants `Some(x)` and `None`
— so there is no null:

```
fn first_even(xs: [int]) -> Option(int):
    for x in xs:
        if x % 2 == 0:
            return Some(x)
    return None

fn main():
    match first_even([1, 3, 4, 7]):
        Some(v): println("found " + str(v))
        None: println("none")
```

## 7. Putting it together

Here's a small program that uses all of the above — it grades a list of students and
tallies how many got each letter. Save it as `grades.ty` and run it.

```
enum Grade:
    A
    B
    C
    F

fn letter(score: int) -> Grade:
    if score >= 90:
        return A
    elif score >= 80:
        return B
    elif score >= 70:
        return C
    else:
        return F

fn grade_name(g: Grade) -> string:
    return match g:
        A: "A"
        B: "B"
        C: "C"
        F: "F"

struct Student:
    name: string
    score: int

fn main():
    students : [Student] = [
        Student("Ada", 91),
        Student("Bo", 72),
        Student("Cy", 88),
        Student("Di", 55),
    ]

    tally : [string: int] = []
    for s in students:
        g := grade_name(letter(s.score))
        tally[g] = tally.get(g, 0) + 1

    for s in students:
        println(s.name + ": " + grade_name(letter(s.score)))

    println("---")
    for g in ["A", "B", "C", "F"]:
        println(g + ": " + str(tally.get(g, 0)))
```

Running it:

```
Ada: A
Bo: C
Cy: B
Di: F
---
A: 1
B: 1
C: 1
F: 1
```

## 8. Where the frees went

Look back at that program: it built an array of structs, a map, and a pile of strings —
and there is no `free` anywhere, and no garbage collector running. So where did the memory
go?

Every scope — each function call, each loop iteration, each `if` block — owns a memory
arena. Allocations go into the current scope's arena, and when the scope exits, the whole
arena is released at once. Because the language has no reference type, a value can only
leave a scope in ways the compiler can see in the source (returned up, or passed down), so
the compiler places every allocation and free itself, from the syntax alone — no garbage
collector, no manual `free`, and no lifetime annotations to write.

That is the entire idea the language exists to test. The full argument, with the
measurements and the cases where it costs, is in [thesis.md](thesis.md).

## Next steps

- **[Language reference](reference/index.md)** — the precise behavior of every feature.
- **[Examples](../examples/)** — larger runnable programs (a JSON parser, an HTTP client,
  a static-site generator, a ray tracer).
- **[thesis.md](thesis.md)** — why the language is shaped this way.
- **[Core library](guides/corelib.md)** — the batteries: strings, math, io, json, http, and more.
