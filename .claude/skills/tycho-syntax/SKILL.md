---
name: tycho-syntax
description: Write correct Tycho. Load this before generating, editing or reviewing any .ty file — no model has Tycho in its training data, and Tycho resembles Python and Go closely enough that the reflex to write one of those produces code that looks right and does not compile. Covers the forms a model gets wrong, the traps that compile anyway, and how to find a corelib answer.
---

<!-- EDITORIAL GUIDELINES FOR THIS FILE
Loaded into an agent's context window as a correction layer. Every line costs
context.
- Only include what a model gets WRONG. If the reflex from Python/Go/Rust is
  already right, say nothing.
- Every claim here was produced by compiling the wrong form and reading the
  real diagnostic. Do not add a row from memory.
- WRONG/CORRECT pairs, short enough to pattern-match. No prose restating them.
- Every fenced example is run by `make docs-fences`. A stale example reddens a
  gate instead of teaching an agent to write broken code.
- Before adding a section ask: would a model get this wrong? If not, cut it.
-->

**No model has seen Tycho.** Its training data contains zero Tycho, so it will
not draw a blank — it will pattern-match onto Python or Go and emit something
that reads correctly and does not compile. That reflex is the thing this file
corrects.

Verify by compiling. `./tychoc f.ty && ./f` — a file with an `import` is a
package and needs its own directory plus a `package main` first line.

## The reflex table

Each row was compiled. The third column is the diagnostic you will actually see.

| You will write | Tycho requires | The compiler says |
|---|---|---|
| `m := [string: int]` | `m := []string: int` | ``[string: int]` is the TYPE, not a value` |
| `for k in m:` | `for k in keys(m):` | `a map is not directly iterable` |
| `P{x: 1}` | `P(1)` — positional | `unexpected character '{'` |
| `fn main() { … }` | `fn main():` + indent | `unexpected character '{'` |
| `where numeric(T) and comparable(T)` | `where numeric(T), comparable(T)` | `expected ':' before the block` |
| `fn add(a: int, b: int) int:` | `-> int:` | `expected ':' before the block` |
| `func main():` / `def main():` | `fn main():` | `expected 'fn'` |
| `let x = 1` | `x := 1` | `a bare expression has no effect` |
| `x = 5` (new variable) | `x := 5` | `assignment to unknown variable 'x'` |
| `import strings` | `import "core:strings"` | `expected an import path string` |
| `s := soa [P]` | `s := soa []P` | `expected ']' (an empty soa literal is `soa []Struct`)` |
| `xs.append(3)` | `push(xs, 3)` | `'.append' on a non-struct value` |
| `nil` / `None` | `null` | `unknown variable 'nil'` |
| `True` / `False` | `true` / `false` | `unknown variable 'True'` |
| `"val %d" % x` | `f"val {x}"` | `modulo … require two matching integers` |
| `bytes := 3` | any other name | `'bytes' is a reserved keyword` |

An unused import is an **error**, not a warning: `` `core:strings` imported and
not used in this file ``.

## What already works — do not avoid these

Method-call syntax on the builtins is real, so both spellings compile:

```tycho
fn main():
    s := "hello"
    println(str(len(s)) + " " + str(s.len()))   # 5 5
```

Same for `str`, `substr`, `chr`, `split`, `keys`, `find`, `char_at`.

## The traps that COMPILE

The table above is the cheap half — the compiler catches all of it. These do
not fail, and are where real defects have come from.

**`len(s)` is BYTES.** Indexing and slicing are byte operations. On non-ASCII
input the natural program is the wrong one, and it is wrong silently.

```tycho
package main
import "core:utf8"

fn main():
    s := "héllo"
    println(str(len(s)))            # 6 -- bytes
    println(str(utf8.count(s)))     # 5 -- characters
```

**A bare-value parser in corelib is lax by design.** `strings.parse_int("3x")`
is `3`, not an error. `decimal.from_str("1.5x")` is `0.15` — a plausible wrong
number, not zero. There is usually a `_checked` sibling returning a `Result`;
reach for it for anything read from a file, a socket or a user.

```tycho
package main
import "core:strings"

fn main():
    println(str(strings.parse_int("3x")))            # 3 -- fails OPEN
    match strings.parse_int_checked("3x"):
        Ok(v): println("ok " + str(v))
        Err(e): println("refused")                   # refused
```

**`_` is an ordinary variable, not a discard.** `_ , b := f()` declares it and
works; one `_` per destructuring list, and `_, b = f()` with no prior `_` is an
error naming the rule.

**A slice clamps, it does not bounds-check.** `b[1:99]` on a 3-byte value gives
you length 2 and no trap. Use `strings.slice_bytes` / `slice_str` when the
bounds must be checked.

## Finding a corelib answer

Per-function semantics are not fully written down (`docs/spec/18-library.md`
says so). The order that works:

1. Read `corelib/<pkg>/<pkg>.ty` — the signature and its comment are the
   contract, and the file is short.
2. Check `corelib/test/<pkg>/main.ty` and its `.out` golden for the answer to
   an edge case.
3. `docs/reference/corelib.md` is the catalogue: which package holds what.

Names that are not where you would look: `strings.parse_int` (no bare builtin),
`sort.asc` / `sort.desc` / `sort.by_key` (not `sort.strings`), `to_under(x)` to
unwrap a newtype.

## Affine types: task, typed handle, channel

One owner, destructor at scope exit. All three refuse the same shapes at
compile time: no copy (`g := f` and `g = f` both refused), no storing in an
array, map, struct field, tuple, `Option` or `Result`, no capture by a closure
or a `parallel for`, no returning one from a Tycho function, and no `sink` or
`inout` — passing one plainly is already a borrow. `close(h)` needs a handle
variable, not a call result.

Every one of those refusals holds through a generic too.

## Two mechanical traps when probing

**Sibling `.ty` files share a package.** `tychoc` compiles every `.ty` beside
the entry file, so two probe programs in one directory collide on `main`. Give
each probe its own directory — several "findings" have been this and nothing
else.

**`--emit-c -o NAME` writes `NAME.c`.** Checking for `NAME` finds nothing and
reads as a failed compile.
