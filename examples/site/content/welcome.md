title: Welcome to Tycho
date: 1690000000
author: tycho
---
# Welcome

Tycho is a small, data-oriented systems language. Values are copied instead
of aliased, and every scope gets its own memory arena, reclaimed when the
scope ends.

## Why it is different

- no garbage collector pausing your program
- no manual free to forget
- no borrow checker to fight

If you can write a function and a loop, you already know enough to start.
Read on, and welcome.

## A first program

```tycho
fn main():
    println("hello")
```

Build it with `tychoc`, and find the **tutorial** in the docs.
