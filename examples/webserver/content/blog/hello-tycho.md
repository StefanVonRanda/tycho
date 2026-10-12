date: 2026-07-01
# Hello, Tycho

Welcome to the blog. Tycho is a small systems language that compiles to C —
and the compiler itself is written in Tycho, giving byte-identical output on
every pass.

```tycho
fn main():
    println("hello, world")
```

Values are copied, never aliased, so there is no `free` to call and no
collector running behind your back. Each scope's arena is reclaimed when the
scope ends. That is the whole memory model, and this post is its first demo.
