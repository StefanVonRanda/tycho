date: 2026-07-01
# Hello, Tycho

This is the first post. Tycho compiles to C and self-hosts: a second compiler
written *in Tycho* reproduces its own output byte-for-byte.

```
fn main():
    println("hello, world")
```

Value semantics mean you pass a struct and get a copy — no aliasing, no
`free`, and the arena reclaims it when the scope ends.
