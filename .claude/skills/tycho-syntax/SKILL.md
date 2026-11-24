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
| `for a, b in pairs:` | `for p in pairs:` then `a, b := p` | ``for` binds one name` |
| `t[0]` on a tuple | `t.0`, `t.1` | `a tuple element is named `.0`, `.1`, … not `[i]`` |
| `struct S:` with field `bytes` | any non-keyword name | `'bytes' is a reserved keyword … as a field name` |
| `fn get(i: Item($A))` | reuse the struct's own parameter: `Item($T)` | `its type arguments must be EITHER exactly its own parameters, in order` |
| `_, b = f()` (never declared) | `_, b := f()` | ``_` is not a discard — it is an ordinary variable` |
| `fn show(a: int)` + `fn show(b: string)` | one name, one definition — or a package prefix | `'show' is already defined` |
| `handle Db: free: db_close` | a block: `handle Db:` then an indented `free: db_close` | `expected newline` |
| `fn open() -> Db:` returning a handle | only an `extern fn` may return one | `a Tycho fn cannot return a handle -- only an `extern fn` opener may` |
| `v := r or_else 0` for a default | `result.unwrap_or(r, 0)` -- `or_else` RETURNS its value from the enclosing function | `or_else returns its value from the enclosing function, which returns nothing` |
| `x := a if c else b` | `x := if c:` then each branch's value on its own indented line, with `else:` | `there is no inline `a if c else b`` |
| `fn(x: int) -> int: return x + 1` | `fn(x: int) -> int: x + 1` — one expression, no `return` | `a lambda body is a single expression and is its own result` |
| a function named `select` | any other name — `select` is a keyword (channels) | `'select' is a reserved keyword and cannot be called` |

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

**A generic body is checked AFTER substitution**, so it may call an ordinary
function on `$T` with no constraint involved — do not reach for a `where` clause
to make this legal, and do not assume it is impossible:

```tycho
package main

struct Point:
    x: int
    y: int

fn show(p: Point) -> string:
    return "(" + str(p.x) + "," + str(p.y) + ")"

fn render(v: $T) -> string:
    return show(v)

fn main():
    println(render(Point(1, 2)))   # (1,2)
```

The limit is the overloading row above: `show` can have only one definition, so
this works at one type per name, not several.

**Vector maths is built in — do not write a `Vec3` struct and `v_add` helpers.**
`vector[N]T` (N a power of two, T `int`/`float`/`f32`) and fixed arrays `[N]T`
take `+ - * /` and unary `-` element-wise, with a scalar on either side (a
`float` or `f32` scalar variable adapts to float lanes). `.x .y .z .w` name
lanes, and a 2- or 4-letter swizzle (`v.yzxw`) reads lanes as a vector. A 3D
vector is `vector[4]f32` with w unused. `f32` widens to `float` on its own in
arithmetic, comparisons and `float` arguments. Generics take
`vector[$N]$T`. `core:linalg` has `vec3f`, `dot`, `length`, `cross`,
`normalize`, `lerp`:

```tycho
package main
import "core:linalg"

fn main():
    a := linalg.vec3f(3.0, 0.0, 4.0)
    k := 2.0
    b := -a * k + linalg.vec3f(1.0, 1.0, 1.0)
    z: vector[4]f32 = 0.0                          # a scalar fills every lane
    println(str(linalg.length(a)) + " " + str(b.x) + " " + str(a.zyxw.x) + " " + str(z.w))   # 5.0 -5.0 4.0 0.0
```

| You will write | Tycho requires | The compiler says |
|---|---|---|
| `a.(1, 2, 0, 3) * b.(...)` | `a.yzxw * b.zxyw` — the `.( )` form is a TUPLE (it is also the struct form) | `arithmetic requires two ints or two floats (got (f32, f32, f32, f32), ...)` |
| `type Vec3 = vector[4]f32` | use the vector type itself — a newtype drops the operators | `arithmetic requires two ints or two floats (got Vec3, float)` |
| `[v.x, k]` with no annotation, f32 lane and float `k` | annotate: `xs: [float] = [v.x, k]` — inference takes element 0's type | `array elements must all have the same type` |
| `v * n` with `n: int` on a float vector | `v * to_float(n)` — an int VARIABLE never widens, as in `1.0 * n` | `requires the scalar to have the array's element type` |

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

**The default integer parser is strict; the lax prefix scanner is explicit.**
`strings.parse_int("3x")` is `Err(Garbage)`, not `3`. The fail-open scanner is
`strings.parse_int_prefix("3x")` (3). `decimal.from_str("1.5x")` is still
fail-open (`0.15` — a plausible wrong number, not zero); its strict sibling is
`from_str_checked`. The general rule: anything read from a file, a socket or a
user gets the fail-closed form.

```tycho
package main
import "core:strings"

fn main():
    match strings.parse_int("3x"):
        Ok(v): println("ok " + str(v))
        Err(e): println("refused")                   # refused
    println(str(strings.parse_int_prefix("3x")))     # 3 -- explicit prefix
```

**`_` is an ordinary variable, not a discard.** `_ , b := f()` declares it and
works; one `_` per destructuring list, and `_, b = f()` with no prior `_` is an
error naming the rule.

**`core:filepath` is Windows-style on every host.** It turns `/a/b` into
`\a\b` even on macOS and Linux; POSIX paths are `core:path` (`path.join`,
`path.dir`). The Go reflex -- `filepath` is the host-native one -- picks the
wrong package and still compiles.

**A slice clamps, it does not bounds-check.** `b[1:99]` on a 3-byte value gives
you length 2 and no trap. Use `strings.slice_bytes` / `slice_str` when the
bounds must be checked.

## Finding a corelib answer

**Check the package list before you hand-roll a helper, call corelib "missing",
or reach for Python.** `ls corelib` lists 80+ packages. A session with a Tycho
renderer wrote its image diffs and contact sheets in Python and a 26-branch
flag parser by hand, then called image I/O a gap, while `core:raster`
(BMP/QOI/TGA/PPM), `core:image` (PNG/JPEG) and `core:cli` sat there.

Per-function semantics are not fully written down (`docs/spec/18-library.md`
says so). The order that works:

1. Read `corelib/<pkg>/<pkg>.ty` — the signature and its comment are the
   contract, and the file is short.
2. Check `corelib/test/<pkg>/main.ty` and its `.out` golden for the answer to
   an edge case.
3. `docs/reference/corelib.md` is the catalogue: which package holds what.

Names that are not where you would look: `strings.parse_int` / `parse_int_prefix`
(no bare builtin), `sort.asc` / `sort.desc` / `sort.by_key` (not
`sort.strings`), `to_under(x)` to unwrap a newtype.

## Affine types: task, typed handle, channel

One owner, destructor at scope exit. The refusals are compile errors and each
names the fix — these were compiled, not recalled:

| You write | The compiler says |
|---|---|
| `d := c` (a second name) | `a channel cannot be copied — it is freed once, when its creating scope exits` |
| a channel as a struct field | `a struct field cannot be a channel` |
| `xs := [c]`, or `Some(c)` | `cannot be stored in a container or aggregate — pass it as an argument instead` |
| a closure that uses `c` | `a closure cannot capture a channel handle — take it as a parameter instead` |

Also refused: returning one from a Tycho function, `sink` or `inout` on one
(passing it plainly is already a borrow), and `close(h)` on a call result rather
than a variable. Every refusal holds through a generic too.

`sink` consumes: a `sink` argument may be mentioned **once** in the calling
function, and the diagnostic counts the mentions for you — `'b' is consumed by a
`sink` parameter but is mentioned 2 times in this function`.

## Rules a model breaks by habit

Mined from `tests/reject/` — 337 fixtures, each pinning a form that must not
compile. One row per rule, not per fixture.

| Habit | The compiler says |
|---|---|
| passing a newtype where its underlying type is wanted | `argument 1 of 'takes_raw' is Ids, expected [int]` |
| comparing a newtype to its underlying | `cannot compare A with [int]` |
| a `string` key into a map keyed by a newtype | `map key must be UserId, got string` |
| a `match` that handles only the variants you care about | `non-exhaustive match: missing variant B of E` |
| a `match` on an int with no `_` arm | `a match on int must carry a `_` arm (the domain is unbounded)` |
| `_` written before the other arms | `a `_` wildcard must be the last match arm` |
| `const XS = [1, 2]` | `a `const` must be a single scalar literal — an int, float, string, bool or char` |
| assigning to a `const` | `cannot assign to constant 'X'` |
| re-declaring a name in the same scope | `'a' is already declared in this scope` |
| a variadic parameter that is not last | `a variadic parameter must be the last parameter of 'f'` |
| `void` as an ordinary type | `'void' is a type only as a Result's ok payload, as in Result(void, string)` |
| passing `[3]int` to a `[int]` parameter | `argument 1 of 'take' is [3]int, expected [int]` |

A newtype is distinct **everywhere** — argument position, comparison, map key —
and it is erased at runtime, so nothing catches it later. `to_under(x)` unwraps.

## What the compiler already tells you

`tests/diag/` exists because these mistakes were expected. The message usually
carries the fix, so read it before guessing:

| You will write | Tycho requires | The compiler says |
|---|---|---|
| `while cond:` | `for cond:` | `Tycho has no `while` — use `for cond:` for a conditional loop` |
| `for i in range(n):` | `for i in 0..<n:` | ``range()` was removed` |
| a named `fn` inside a `fn` | declare it at top level | `a named function cannot be declared inside another` |
| `1 + 2.5` | convert one side | `arithmetic requires two ints or two floats (got int, float)` |
| `match s:` with string arms | `if`/`elif` on strings | ``match` does not take a string arm — int, char, bool, a range and a const name do` |
| `Shape.Circle` | `Circle` | `'Circle' is a variant of enum Shape, not a package member — variant names are global` |
| `xs := [string]` | `xs := []string` | ``[string]` is the TYPE, not a value` |
| `fn main() -> int:` | return nothing, or `Result(void, string)` | `'main' returns nothing or Result(void, string), not int` |
| `(int)` as a tuple type | a tuple needs two elements | `parentheses do not group a type` |
| `Empty` from a generic enum | `Empty$(int)` | `supply the type explicitly, e.g. Empty$(int)` |

Two habits worth trusting: a misspelling gets a suggestion (`unknown variable
'coutn'; did you mean 'count'?` — the same for fields, types and procedures),
and calling a corelib function you have not imported names the package —
`unknown procedure 'sort' — core:arrays provides `sort` — add `import
"core:arrays"``.

## Generics and `where`

The five predicates are closed. The compiler lists them when you invent one:
`unknown `where` predicate 'fancy' (known: numeric, comparable, has_str,
hashable, defaultable)`.

| Habit | The compiler says |
|---|---|
| `where` on a function with no `$T` | ``where` constraints require a generic function (one with a `$T` parameter)` |
| `where` naming something that is not a parameter | ``where` refers to 'Q', which is not a type parameter of this function` |
| a type parameter that appears only in the return type | `no argument fixes 'empty''s type parameter $T — it appears only in the return type` |
| a `float` as a map key, or under `hashable(K)` | `instantiated with K = float, which does not satisfy `hashable(K)`` |
| a `string` under `numeric(T)` | `instantiated with T = string, which does not satisfy `numeric(T)`` |
| a struct with no zero value under `defaultable(T)` | `instantiated with T = P, which does not satisfy `defaultable(T)`` |

A constraint is checked **at the instantiation**, and the message names it —
`'sm' instantiated with T = string`. When a generic fails, the type that broke it
is in the error, not left for you to work out.

The affine refusals hold through a generic and say where it happened: `a struct
field cannot be a channel — 'Box' was instantiated at one`.

## Two mechanical traps when probing

**Sibling `.ty` files share a package.** `tychoc` compiles every `.ty` beside
the entry file, so two probe programs in one directory collide on `main`. Give
each probe its own directory — several "findings" have been this and nothing
else.

**`--emit-c -o NAME` writes `NAME.c`.** Checking for `NAME` finds nothing and
reads as a failed compile.
