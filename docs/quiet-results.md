# Quiet results — where Tycho answers instead of refusing

Tycho's stated principle is to **fail closed**: a doubtful operation aborts
cleanly rather than proceeding into undefined behavior
([spec §30](spec/17-runtime.md)). Indexing is bounds-checked, `Option` removes
null, `Result` removes exceptions, and an integer divided by zero stops the
program rather than inventing an answer.

This page is the **complete register of the exceptions** — the handful of
operations that hand back a value instead of refusing. Every one of them is
deliberate and has been argued out; none is a bug, and this page is not a list
of things to be fixed. It exists because the failure they share is the only kind
the rest of the language works to prevent: a program that keeps running with an
answer you did not mean. The compiler cannot warn you, the sanitizers see
nothing wrong, and the tests pass.

Read it once before your first real program. Every output below is printed by
the code beside it.

| What you write | What you get | The fail-closed form |
|---|---|---|
| §1 `len(s)`, `s[i]`, `s[a:b]` | **bytes**, not characters | `core:utf8` (`utf8.count`) |
| §2 `utf8.decode` on bad input | width **`0`** — `at += w` never advances | test `w == 0` before advancing |
| §3 `s[a:b]` out of range | a **clamped** slice, no trap | `strings.slice_str` / `slice_bytes` → `Result` |
| §4 `a + b` past the integer range | **wraparound**, never a trap | check the operands before the operation |
| §5 `c + n` on a `char` | **wraps to a byte** (`0..255`) | treat `char` as a byte, because it is one |
| §6 `strings.parse_int("3x")` | `3` — stops at the garbage | `strings.parse_int_checked` → `Result` |
| §7 `_` | an **ordinary variable**, not a discard | call the function as a statement |

Everything else in the language that can go wrong **aborts** — a real bounds
violation, division by zero, an out-of-range `chr`, a second `wait` on a task, a
stale `core:pool` handle. Those you will find out about immediately, which is
why they are not on this page. The register of what aborts is
[spec §30.2](spec/17-runtime.md); what wraps is §30.1 and what clamps is §30.3.

The corelib entries (§2, §6) are library behavior rather than language rules, so
they are not in the spec's register; they are here because the failure they
produce is the same one.

## 1. `len`, indexing and slicing are byte operations

A `string` is a length-prefixed byte buffer, and every length-sensitive
operation uses that length ([spec §30.4](spec/17-runtime.md)). On ASCII the
distinction never shows. On anything else, the natural program is the wrong one
and it is wrong silently.

```tycho
package main
import "core:utf8"

fn main():
    s := "héllo"
    println("len       = " + str(len(s)))
    println("utf8.count= " + str(utf8.count(s)))
```

```output
len       = 6
utf8.count= 5
```

`s[i]` yields one byte as an `int` in `0..255`, so indexing into multi-byte text
gives you half a character. Reach for [`core:utf8`](reference/strings.md) the
moment the input is not known to be ASCII.

## 2. `utf8.decode` reports invalid input with a zero width

The companion to §1, and the only entry on this page whose failure is a **hang**
rather than a wrong value. `utf8.decode(s, at)` returns `(codepoint, width)`, and
on anything it cannot decode it returns `(-1, 0)`. A caller who advances with
`at = at + width` — the obvious loop — never advances at all.

```tycho
package main
import "core:utf8"

fn main():
    good := utf8.decode("é", 0)
    println("whole  'é'      -> cp=" + str(good.0) + " width=" + str(good.1))
    half := "é"[1:2]                      # the second byte, on its own
    bad := utf8.decode(half, 0)
    println("half   'é'[1:2] -> cp=" + str(bad.0) + " width=" + str(bad.1))
    at := 0
    steps := 0
    for at < len(half) and steps < 3:     # `steps` is the only reason this ends
        d := utf8.decode(half, at)
        at = at + d.1
        steps = steps + 1
    println("3 iterations later, at=" + str(at) + " len=" + str(len(half)))
```

```output
whole  'é'      -> cp=233 width=2
half   'é'[1:2] -> cp=-1 width=0
3 iterations later, at=0 len=1
```

Note where the bad input came from: slicing a string at a byte offset that falls
inside a character — §1 and this entry are the same mistake, one step apart.
**Always test the width before advancing**, and treat `width == 0` as "this input
is not UTF-8" rather than as a character to skip. A `-1` codepoint is easy to
notice; a `0` width is not, because nothing about the loop looks wrong.

## 3. A slice clamps — it is not a bounds check

This is the one bounds situation in the language that does not abort
([spec §30.3](spec/17-runtime.md)). `start < 0` becomes `0`, `end > len` becomes
`len`, and an inverted range yields empty.

```tycho
package main

fn at(n: int) -> int:
    return n

fn main():
    b := "abc"
    println("b[1:99] len = " + str(len(b[at(1):at(99)])))
    println("b[-3:2] len = " + str(len(b[at(0)-3:at(2)])))
    println("b[3:1]  len = " + str(len(b[at(3):at(1)])))
```

```output
b[1:99] len = 2
b[-3:2] len = 2
b[3:1]  len = 0
```

**Why it is this way, since Go and Odin both fault here.** A Tycho `s[a:b]` *is*
`substr` — the same operation in operator spelling — and `substr` clamps by
definition, because that is right for the text processing it exists for. Making
the operator trap would either desynchronise it from `substr` or drag `substr`
along with it. The divergence was measured against both languages and the clamp
kept deliberately; `tests/slice_clamp.ty` pins all eight cases so it cannot be
"fixed" by accident.

**When the bounds are data** — a length field, an offset from a file or a
socket — clamping turns a corrupt input into a plausible short read. Use
`strings.slice_str` or `strings.slice_bytes`, which take the same `(start,
stop)` and return `Result(_, SliceErr)` distinguishing `OutOfBounds` from
`Inverted`.

> An **array** slice is different: `a[2:10]` on a 5-element array aborts. The
> clamp is a string and `bytes` behavior, not a general one.

## 4. Integer overflow wraps, and an over-wide shift is zero

Signed overflow is **defined** two's-complement wraparound and never traps
([spec §30.1](spec/17-runtime.md)) — the reference compiles with `-fwrapv`, so
this is a guarantee rather than the C accident it looks like. A shift count at
or past the operand's width is defined as `0`.

```tycho
package main

fn at(n: int) -> int:
    return n

fn main():
    big := at(9223372036854775807)
    println("intmax + 1 = " + str(big + at(1)))
    println("1 << 64    = " + str(at(1) << at(64)))
```

```output
intmax + 1 = -9223372036854775808
1 << 64    = 0
```

Defined is not the same as intended. There is no checked-arithmetic form in the
language, so a sum that could exceed the range is a thing to test for before you
compute it, not after.

## 5. `char` arithmetic wraps to a byte

A `char` is a byte, and arithmetic on one stays in `0..255` — it does not widen
to an `int` and it does not abort on the way past the end.

```tycho
package main

fn at(n: int) -> int:
    return n

fn main():
    c := char_at("A", 0)
    println("'A' + 1   = " + str(c + at(1)))
    println("'A' + 255 = " + str(c + at(255)))
```

```output
'A' + 1   = B
'A' + 255 = @
```

`65 + 255` is `320`, which wraps to `64` — `@`. For anything that is arithmetic
rather than character stepping, convert to `int` first.

## 6. The bare corelib parsers fail open

`strings.parse_int` stops at the first character it cannot use and returns what
it had, with no way to tell that from a clean parse.

```tycho
package main
import "core:strings"

fn show(s: string):
    match strings.parse_int_checked(s):
        Ok(v): println("  checked -> ok " + str(v))
        Err(EmptyInput): println("  checked -> EmptyInput")
        Err(Garbage): println("  checked -> Garbage")
        Err(OutOfRange): println("  checked -> OutOfRange")

fn main():
    println("[3x]  lax -> " + str(strings.parse_int("3x")))
    show("3x")
    println("[]    lax -> " + str(strings.parse_int("")))
    show("")
    println("[ 7]  lax -> " + str(strings.parse_int(" 7")))
    show(" 7")
```

```output
[3x]  lax -> 3
  checked -> Garbage
[]    lax -> 0
  checked -> EmptyInput
[ 7]  lax -> 0
  checked -> Garbage
```

Note the third row: a leading space is not tolerated either, so `parse_int` on
`" 7"` is `0` — indistinguishable from the `0` you get for an absent option.

**This is deliberate for the case it was built for** — `0` is the right answer
for a missing CLI option, and demanding a `Result` there would be noise. It is
the wrong answer for a *format*: a damaged length field of `"1x4"` parses as `1`,
and a wrong length that parses is exactly how a reader ends up hashing the wrong
span of bytes. A real instance of this shipped a stale build output as current,
by reading a corrupted mtime out of a stamp file as `17`.

**The rule: anything that came from a file, a socket, or a user gets the
`_checked` sibling.** They exist across the corelib and return a `Result` with a
named error:

| Lax | Fail-closed |
|---|---|
| `strings.parse_int` | `strings.parse_int_checked` |
| `decimal.from_str` | `decimal.from_str_checked` |
| `json.parse` | `json.parse_checked` |
| `cli.parse` | `cli.parse_checked` |
| `io.list` | `io.list_checked` |

Most are drop-in; `cli.parse_checked` is not — it additionally takes the valued
and boolean option names, because validating a command line requires knowing
which flags expect a value.

`decimal.from_str` is further along than the others: it is **deprecated**, the
compiler warns on every call naming the exact failure (`"12.5x"` returns `1.25`),
and it is removed in 1.0.

## 7. `_` is an ordinary variable, not a discard

Tycho has no discard pattern. `_` is a name like any other, which means it
declares, it holds a value, and it can only be declared once per scope.

```tycho
package main

fn main():
    _ := 41
    println("_ holds " + str(_ + 1))
```

```output
_ holds 42
```

In a destructuring bind `_, b := f()` it reads like a discard and behaves like
one, because the value genuinely goes nowhere afterward — that spelling is fine
and is the idiom. What does not work is treating it as a keyword: a second `_`
in one list, or `_ = f()` as a way to drop a return value. The compiler refuses
both and says why. To drop a function's result, call it as a statement.

## Related

- [spec §30](spec/17-runtime.md) — the normative register: what wraps, what
  aborts, what clamps, what is unspecified.
- [`reference/strings.md`](reference/strings.md) — bytes, `substr`, and the
  checked slice functions.
- [`debugging.md`](debugging.md) — when a program is already misbehaving.
