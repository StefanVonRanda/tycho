# Strings

A `string` is a **byte** string with a known length. It is not a sequence of
characters, and every indexing operation on it is a byte operation. That is the
one thing to know before writing anything that touches text you did not type
yourself.

## `len`, indexing and slicing are all bytes

```tycho
fn main():
    s := "hello"
    println(str(len(s)))        # 5
    println(str(s[0]))          # 104 -- a byte VALUE, an int, not a 1-char string
    println(s[1:3])             # el -- a slice IS a string
```

`s[i]` is the byte at `i` as an `int`. `s[a:b]` is the half-open byte range as a
new string. Both are O(1) and neither knows anything about characters.

## The trap: `len` is not the character count

Non-ASCII text has more bytes than characters, so the natural program is the
wrong one — and it is wrong only on input the author often never tries.

The examples below `import`, so they open with `package main` and belong in
their own directory: a file with an `import` is a package. A single-file program
with no imports needs neither.

```tycho
package main
import "core:utf8"

fn main():
    s := "héllo"
    println(str(len(s)))            # 6 -- BYTES
    println(str(utf8.count(s)))     # 5 -- characters
    println(s[0:2])                 # h? -- a slice through the middle of `é`
```

The third line is the failure. `s[0:2]` cuts `é` in half and produces a string
holding one stray continuation byte, which renders as plausible-looking mojibake
rather than raising anything. Nothing in the language stops you: the slice is a
valid byte range, and byte ranges are what the language promises.

**`utf8.count(s)` returns `-1` on invalid input** rather than a count, so a
negative answer is your signal that the bytes are not UTF-8 at all.

## Stepping by character

`utf8.decode(s, at)` returns the codepoint at a byte offset and how many bytes it
occupied.

```tycho
package main
import "core:utf8"

fn main():
    s := "héllo"
    i := 0
    n := 0
    for i < len(s):
        cp, nb := utf8.decode(s, i)
        if nb <= 0:
            nb = 1          # REQUIRED -- see below
        i = i + nb
        n = n + 1
    println(str(n))         # 5
```

**The `nb <= 0` guard is not optional.** On invalid input `decode` returns a
non-positive width, and a loop that adds it never advances — so a caller who
trusts the return spins forever on exactly the input an attacker controls.
`utf8.valid(s)` checks a whole string up front, but a streaming caller cannot
afford the second pass, which is why the guard belongs in the loop.

## Validating before you trust

```tycho
package main
import "core:utf8"

fn main():
    println(str(utf8.valid("héllo")))               # true
    println(str(utf8.valid("a" + chr(0xC0) + "b"))) # false -- 0xC0 starts nothing
```

Validate anything that arrives from a file, a socket or a user before you treat
it as text. A validator that accepts an overlong encoding is a filter bypass: the
same character arrives by a spelling the filter above it did not recognise, which
is why `core:utf8` rejects overlongs, surrogates and anything above U+10FFFF
rather than decoding them leniently.

## Building strings

`+` concatenates and produces a new string; there is no in-place append. Interpolation
is `f"..."` with `{}` holes.

```tycho
fn main():
    who := "world"
    n := 3
    println("hello " + who)         # hello world
    println(f"{who} x{n}")          # world x3
```

## Where the rest lives

| You want | Look in |
| --- | --- |
| case, trim, split, find, replace, padding, parsing | [`core:strings`](../guides/corelib.md) |
| character counts, decoding, validation, encoding | [`core:utf8`](../guides/corelib.md) |
| the byte-level type with no text semantics at all | [`bytes`](types.md) |
| `len`, `chr`, `char_at`, `find` and the other builtins | [Builtins](builtins.md) |

The rule that ties them together: **the language gives you bytes, and `core:utf8`
gives you characters.** Reaching for the second is a decision you make on
purpose, and text from outside your program always needs it.
