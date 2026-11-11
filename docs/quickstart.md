# Quickstart

Ten minutes from a clone to a running Tycho program. The [tutorial](tutorial.md)
is the guided first hour; this page is the shortest path to output.

## 1. Build

Tycho needs `make` and a C11 compiler — gcc, or clang 15 or newer.

```text
$ git clone https://github.com/StefanVonRanda/tycho
$ cd tycho
$ make
```

That builds `./tychoc`. Prebuilt binaries are on the
[releases page](https://github.com/StefanVonRanda/tycho/releases).

## 2. Write a program

Save this as `sum.ty`:

```tycho
fn total(xs: [int]) -> int:
    s := 0
    for x in xs:
        s += x
    return s

fn main():
    xs := [3, 1, 4, 1, 5]
    println("sum = " + str(total(xs)))
```

```output
sum = 14
```

## 3. Run it

```text
$ ./tychoc sum.ty
$ ./sum
sum = 14
```

`tychoc` transpiles to C and invokes your C compiler. There is no `free` in the
program: `xs` and `s` live in the scope that owns them and are released when
`main` returns. That is the whole memory model;
[from C to arenas](from-c-to-arenas.md) explains it from `malloc`.

## Next

- [Tutorial](tutorial.md) — structs, enums, `Result`, generics, concurrency.
- [Language reference](reference/index.md) — every construct, with runnable examples.
- [corelib](reference/corelib.md) — 77 packages, imported as `core:<name>`.
- [Examples](../examples/) — a JSON pipeline, an HTTP client, a static-site generator.
- [Performance](performance.md) — what the model costs and where it wins.
