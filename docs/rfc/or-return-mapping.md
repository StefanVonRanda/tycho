# `or_return` with a mapping expression

> **Status: proposal, measured, not built.** Reaches a decision for the
> maintainer. The measurement is done; the spelling is not chosen.

## The measurement

A sweep of every `match` block in the tree whose only job is to unwrap an `Ok`
and leave on an `Err` found **76 sites across 17 files**. They split cleanly:

| shape | count | today |
|---|--:|---|
| `Err(e): return Err(e)` — same error, forwarded | 34 | **`or_return` already does this** — all 34 migrated in `f6f1025a` |
| `Err(e): return Err(Wrap(e))` — wrapped | 25 | no form exists |
| `Err(e): return Err(Fresh(...))` — replaced | 17 | no form exists |

So **42 sites remain** that `or_return` cannot express, because
[§14.6](../spec/10-statements.md#146-or_return) returns `Err(err)` *unchanged*.
Every one of them deliberately changes the error, which is the whole point of a
domain error type: a `net.listen` failure surfacing to a caller as a `SrvErr`,
not as a `NetErr`.

For scale, the freeze has been broken three times, and `packed` — the first —
was justified by 141 sites. This is 42.

## What the 42 look like

```text
match net.listen(host, port):
    Ok(f): fd = f
    Err(e): return Err(Listen(host, port))
```

Four lines. The middle one is the only one carrying information.

## The proposal

`or_return` takes an optional expression whose value is a **function from the
callee's error type to the enclosing function's error type**. On `Err(e)` the
function is applied and `Err(f(e))` is returned; on `Ok(x)` nothing changes.

**No new syntax is required.** Tycho already has function values and lambdas
(`Lambda ::= "fn" "(" LambdaParams? ")" ( "->" Type )? ":" Expr`,
[§4.4](../spec/02-grammar.md)), so both spellings below already parse:

```text
fd := net.listen(host, port) or_return fn(e): Listen(host, port)   # replace
b  := store.read(path)       or_return Storage                     # wrap, by name
```

The second form works wherever the new error is a one-argument constructor,
which covers the 25 wrapping sites (`Storage(e)`, `Wal(e)`, `Plan(e)`). The
lambda form covers the 17 that ignore `e` and build a fresh error from other
variables in scope.

### The grammar delta

```text
ExprStmt ::= ( Call | Call "or_return" Expr? ) NEWLINE
OrReturn ::= Unary "or_return" Expr?
```

`or_return` stays a postfix operator binding tighter than any binary operator,
so `f() or_return Wrap + 1` does not parse as a mapping of `Wrap + 1` — the
expression slot should be restricted to a primary, which is what the two
spellings above need and nothing more.

## What it costs

The feature touches both compilers. In the shipped one: `lex.ty` (no new token —
the keyword exists), `parse.ty` (the optional expression), `tcheck.ty` (five
existing `or_return` rules gain a mapped-error case), `emit.ty` (four sites),
`zsema.ty` (the `parallel for` restriction is unchanged). `src/tychoc.c` carries
19 `or_return` sites that need the same treatment. Plus spec §14.6, a
`surface.lock` entry is **not** needed — no new keyword — and fixtures for the
refusals (a mapping whose type does not match the enclosing error type; a
mapping on an `Option`, which has no error to map).

## Three ways this could go, and what each gives up

1. **Build it as proposed.** Covers all 42, subsumes the 34, and adds no
   keyword. The cost is real work in two compilers and one more thing in the
   language to learn.
2. **Build only the by-name form** (`or_return Storage`). Covers the 25 wrapping
   sites, is trivially simple to explain, and leaves the 17 replacement sites as
   `match` blocks. Smallest change that removes the most common shape.
3. **Build nothing.** The 42 stay as four-line `match` blocks. They are correct
   and readable; they are simply the most repeated shape in the tree, and the
   one a newcomer meets first when they write a program that can fail.

## Recommendation

**(1), and only when a program outside this repository wants it.** The near-term
roadmap is explicit that library and surface work is demand-gated — *"built
against a real program that needs it, never ahead of one"*. 42 sites in the
author's own code is a strong signal and not that trigger; the same standard
that held `packed` to 141 measured sites should hold this one to a user who hits
the wall. The measurement is recorded here so that when one does, the work is
already scoped.
