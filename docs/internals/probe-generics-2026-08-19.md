# Generics probe, 2026-08-19 — what a first-time reader hits

> **Provenance.** This run was a Claude subagent spawned inside the author's
> own session, not an independent third party — see
> [probe-model-comparison-2026-08-20.md](probe-model-comparison-2026-08-20.md).
> Every finding below was rebuilt on `main` before being written down, so the
> findings stand; the independence does not.
A fresh agent was given a stripped checkout, a compiler built from `main`, and
one instruction: build a real program using generic functions, a generic
container of its own, and payload enums with `Option`/`Result`. It produced an
853-line CSV query engine — lexer, recursive-descent parser over a 6-variant
recursive `enum Expr`, generic `Index($K,$V)` and `MinHeap($T)`, 18 `or_return`
sites. **The program is not the artifact — this is.** It was thrown away.

Aimed here because generics, newtypes, `subscript`, `bounded[N]`, `select` and
the enum error paths had zero non-author programs. It reported five frictions
and rebuilt every one here before they were written down.

## 1. A NULL pointer reaches the `cc` command line

With the compiler moved away from its corelib and `TYCHO_CORELIB` unset:

```
sh: 1: Syntax error: "(" unexpected
tychoc: C compilation failed (cc -O3 -fwrapv -pthread -I(null) -o hi hi.c -lm )
```

`%s` on a NULL path. glibc renders `(null)`, `sh` reads it as a subshell, and the
user is shown an error about their shell for a compiler that cannot find its
standard library. Nothing in the message says corelib, path, or `TYCHO_CORELIB`.

Reproduced both ways: the in-tree compiler, which has `corelib/` beside it,
builds the same file with `TYCHO_CORELIB` unset. So this only fires when the
install is broken or the binary has been moved — which is exactly when a clear
message matters most, and it is the FIRST command a new user runs.

## 2. `-> [T]` is misparsed, but only when a `where` clause follows

The agent reported `-> [T]` as always wrong. It is narrower than that, and the
narrowing is the finding:

| written | result |
|---|---|
| `fn f(xs: [$T]) -> [T] where comparable(T):` | `error: a fixed-size array length must be an integer literal or an int const -- 'T' is not` |
| `fn f(xs: [$T]) -> [$T] where comparable(T):` | compiles |
| `fn f(xs: [$T]) -> [T]:` (no `where`) | compiles, and runs correctly |

The error names a feature the author never used — fixed-size arrays — so it
sends them to the wrong page. And `$` is documented as "introduce once, refer to
it as `T` after", which holds everywhere except here.

## 3. A generic struct's type arguments must be all-parameter or all-concrete

```text
fn idx_append(ix: inout Index($K, [$V])) where hashable(K):
fn idx_c(ix: inout Index($K, [float])) where hashable(K):
```
Both refused with *"a type argument may not partially mention a type parameter"*.
The second contains no partial mention at all — `$K` is a bare parameter and
`[float]` is concrete — so the rule is stricter than its own message describes.
The rule appeared nowhere in `docs/reference/generics.md`; the error text was the
only place it was written down. **Both fixed the same day**: the message now says
the arguments must be *either* exactly the generic's own parameters in order *or*
all concrete, and `generics.md` states the rule with an example.

## 4. `fn name$(K, V)()` is not the declaration form

`error: expected '('`, caret under the `$`. Type parameters are introduced by `$`
at their first *use* in a signature, including inside the return type. Every
`name$(T)` example in the reference is at a **call**; the guide has the
declaration form and the reference does not cross-reference it.

## 5. `package main` sweeps the directory

Adding `package main` to a file made a sibling probe file's `Index` collide.
`docs/reference/packages.md` does say *"A file with an `import` is a package, so
give it its own directory"* — in the last paragraph, where the agent read it as
style advice rather than the cause of an error in a file it had not named.

## What went right, in its words

The generic container worked first try, including `where hashable(K)` admitting
`K` as a map key inside the body and `[]K: V` as an empty map literal over two
parameters — "the piece I braced for". The 6-variant recursive enum, five
mutually-recursive functions returning `Result`, and `or_return` through all of
them compiled with zero errors. Forward references need no prototype.

And the batching added earlier the same day did visible work: adding a variant to
a finished enum reported **four** non-exhaustive `match` sites in one compile,
with `_:` wildcard arms correctly silent.

`docs/reference/enums-options.md` answered every question without a second look.
Every gap is in generics, and all five are omissions — no example of the shape
needed — rather than anything being wrong.
