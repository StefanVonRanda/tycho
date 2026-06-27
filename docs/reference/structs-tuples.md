# Structs and tuples

Both are product types — a value made of several fields. A `struct` names its fields and is
declared as a type; a tuple is anonymous and is how a function returns more than one value.
Both are values, deep-copied on every bind, so two never share storage.

## Structs

```
struct Point:
    x: int
    y: int

struct Rect:
    lo: Point
    hi: Point

fn area(r: Rect) -> int:
    return (r.hi.x - r.lo.x) * (r.hi.y - r.lo.y)

fn main():
    a := Point(1, 2)            # positional construction, fields in declaration order
    r := Rect(a, Point(4, 6))
    r.lo.x = 100               # nested field write, in place
```

A field may be `int`, `float`, `bool`, `string`, an array (including an array of structs —
even of the struct being defined, so `children: [Node]` builds a recursive tree), an
`Option` (a nullable field, `age: Option(int)`), or another struct. A field that would make
the struct infinitely large *by value* — `next: Option(Node)` directly inside `Node` — is a
compile error; use indirection through an array (`[Node]`), whose elements are arena-allocated.

Structs are values, and the copy is **deep**: a field that owns heap bytes (a `string` or
array, at any nesting depth) is duplicated too, so copying a struct copies its whole tree and
two struct variables never share storage.

```
struct Person:
    name: string
    tags: [string]

a := Person("Ada", ["x"])
b := a                         # deep copy: b.name and b.tags are independent
b.name = "Alan"                # never touches a
```

Fields are read with `p.x` and written with `p.x = v`, including nested (`r.lo.x = v`) and an
array-field element (`p.tags[0] = v`). Construction is positional, in declaration order, and
a struct must be declared before it is used as a type. Two structs compare by value with
`==`/`!=` — field-wise, recursing into nested structs, arrays, and strings — so `a == b` is
true exactly when `b` is an independent copy of `a`. A struct may be a "method" receiver; see
[Functions](functions.md#methods-ufcs).

## Tuples and multiple return values

A tuple `(T1, ..., Tn)` (2–8 elements) is an anonymous product — the way a function returns
more than one thing. `return a, b` builds one, and you **destructure** it at the call:

```
fn divmod(a: int, b: int) -> (int, int):
    q := a / b
    return q, a - q * b           # builds the tuple (q, remainder)

fn main():
    quot, rem := divmod(17, 5)     # destructure -> quot = 3, rem = 2
```

Tuples are first-class values, not only a return convention: store one whole
(`t := divmod(17, 5)`), index it by position (`t.0`, `t.1`), write a literal (`p := (10, 20)`),
pass it as an argument or a struct field, and compare two with `==` (element-wise). Any
element type works, including heap ones (`(string, [int])`); a tuple is deep-copied on bind
like everything else, so two are independent. Destructuring comes in two forms — `a, b := f()`
declares fresh locals, `a, b = f()` assigns into existing variables. A tuple element is also a
writable place: `t.0 = v` updates it in place, and value semantics is preserved — a copy taken
beforehand is unaffected.
