# 16. Arrays and slices · 17. Structs, tuples, `soa` · 18. Maps and subscripts · 19. Enums, `Option`, `Result`, `match`

Every aggregate is a **value** ([§9](07-memory-model.md)): binding, argument
passing, and return copy it deeply, so two aggregate variables never share
storage. This chapter defines the **operations** on the aggregate types —
construction, indexing, mutation, growth, iteration, elimination — and their
place semantics. It does **not** redefine the types: their identity, ranges,
element/key restrictions, and recursion rules are fixed in
[§5.3](03-types.md#53-composite-types) and are cross-referenced, not repeated,
here. Where an operation reads or writes through part of an aggregate it
produces a **place** (an lvalue); the general place, borrow, and `inout` rules
are in [§11](07-memory-model.md#11-inout).

> Provenance: array element restriction `src/tychoc.c:2277-2278`,`:2294-2295`;
> `pop`-empty abort `:14133@pop from an empty array`,`:14133@pop from an empty array`; `reserve` `:7104-7130`,`:10557-10563`; tuple
> arity `:2478@a tuple has at most 8 elements`,`:2483@a tuple type needs at least two elements`, index `:5780-5788`; destructuring `:3807-3821`,`:8615-8631`;
> map read (pure `map_get`, no insert) `:6287-6302`; map place insert+zero
> `:11409-11418`; `keys()` insertion order — the walk `:14295@m.elive[e]` over the append-only entries array `:14047@m->ecount++`; `delete` → `map_del`
> `:3655-3679`,`:7027-7033`; subscript parse + rules `:4363-4415`, dispatch
> `:4438-4446`; `or_return` `:6140-6157`.

---

## 16. Arrays and slices

### 16.1 Growable arrays `[T]`

`[T]` is the growable, value-semantic array of [§5.3.1](03-types.md#531-arrays-t).
An **array literal** `[e1, …, en]` constructs a `[T]` whose element type `T` is
the common type of the elements ([§6](04-inference.md)); every element MUST have
the same type `T`. An **empty literal** carries no elements from which to infer
`T`, so it MAY be written with an explicit element type — `[]int`, `[]string`,
`[][int]`. A bare `[]` used as a `:=` initializer is instead *pending*: it is
grounded by the element type expected at its first use in the block (`xs := []`
then `push(xs, 1)` fixes `xs` to `[int]`), and is a compile error only if never
grounded ([§6.4](04-inference.md#64-pending-types)). Binding an array
deep-copies it:

```tycho
xs := [10, 20, 30]      # a [int]
_ys := []int             # empty; element type required
_zs := xs                # zs is an independent deep copy — mutating zs never touches xs
```

A complete program that sums an array by iterating it:

```tycho
fn main():
    xs := [2, 4, 6, 8]
    total := 0
    for x in xs:
        total = total + x
    println("sum = " + str(total))
```

```output
sum = 20
```

`T` MAY be any storable type (`int`, `float`, `string`, a struct, an enum, a
tuple, a map, another array, …), nested to any depth; the sole exclusions are
`void` and `bool` in element-type position (§16.7). `len(a)` yields the current
element count as an `int`.

### 16.2 Indexing and bounds

`a[i]` denotes the element at zero-based index `i`, an `int`. Indexing is
**bounds-checked**: an implementation MUST abort with a diagnostic if
`i < 0` or `i >= len(a)`, and MUST NOT read or write out-of-range storage. The
check is normative and always active in a conforming implementation — it is not
an optional hardening pass. (Rationale: every value lives in one arena block, so
an unchecked out-of-range access would silently land on other live data rather
than fault; the check is what makes the abort observable. Program-abort
semantics are specified in §30.)

Read as an rvalue, `a[i]` yields the element **by copy** for a composite element
and by value for a scalar one. Used on the left of an assignment or as the spine
of a place, `a[i]` is a place (§16.3). Indexing a `string` (`s[i]`) or a `bytes`
(`b[i]`) yields the `i`-th byte as an `int` and is **not** a place — see
[§5.2.5](03-types.md#525-string) and [§5.2.6](03-types.md#526-bytes).

### 16.3 Element places

An array element is a **mutable place**: it MAY be written in situ without
rebuilding the whole element. Writing through an element is a *projection* into
the backing buffer; no pointer is exposed to Tycho, and the projection is
bounds-checked exactly as a read is (§16.2). The following are all places:

```tycho
struct Tagged:
    x: int
    tags: [string]

fn bump(n: inout int):
    n = n + 1

a := [1, 2, 3]
i := 0
v := 9
ps := [Tagged(1, ["a"]), Tagged(2, ["b"])]
grid := [[10, 20], [30, 40]]

a[i] = v                 # replace an element
ps[0].x = 10             # a field of a struct element
push(ps[0].tags, "x")    # grow an array-valued field of an element, in place
grid[1][1] = 60          # an element of a nested array
bump(&ps[1].x)           # a field of an element passed as an `inout` argument
```

Value semantics is preserved: after `qs := ps`, a later `ps[0].x = 10` leaves
`qs` untouched, because each owns its buffer. The owning array of a projected
element MUST be a mutable variable or field; projecting through a **read-only
borrowed parameter** is rejected (§16.4, [§11](07-memory-model.md#11-inout)).

### 16.4 Growth: `push`, `pop`, `reserve`

Three built-in operations change an array's length or capacity (the builtins are
catalogued in §29); each requires a mutable array place as its
first argument.

| Form | Effect |
|---|---|
| `push(a, v)` | Append a deep copy of `v` as the new last element; `len` grows by one. |
| `pop(a)` | Remove the last element and return it (deep-copied into the caller). |
| `reserve(a, n)` | Grow backing capacity to at least `n`; `len` is unchanged. |

`push` and `pop` require element type equality: `v` MUST have type `T` for a
`[T]`. `pop(a)` on an **empty** array MUST abort (`src/tychoc.c:13934@pop`); it is not
silently zero-returning. `reserve(a, n)` is a capacity hint only — it copies the
existing elements into a buffer of capacity `≥ n` and is a no-op when
`n ≤ cap`; it never changes `len` and never inserts elements
(`src/tychoc.c:13007-13012`).

An array **parameter** is a read-only borrow ([§11](07-memory-model.md#11-inout)):
passed without a copy, but `push`, `pop`, `reserve`, or an index-write on it is a
compile error. Copy it first (`b := a`) for a mutable local, or declare the
parameter `inout`. A **returned** array is promoted into the caller's storage and
never dangles ([§10](07-memory-model.md#10-object-lifetimes-and-storage)).

### 16.5 Fixed-size arrays `[N]T`

`[N]T` is the fixed-size array of [§5.3.2](03-types.md#532-fixed-size-arrays-nt):
exactly `N` elements, stored inline and copied by value. Its operations differ
from `[T]` on three points:

- **Literal arity is exact.** A literal bound to a `[N]T` MUST have exactly `N`
  elements; `v: [3]int = [1, 2]` is a compile error. A bare literal
  `[1, 2, 3]` is a dynamic `[int]` **unless** its destination type is a
  fixed-size array, in which case it coerces (element count and element types
  checked).
- **No growth or slicing.** `push`, `pop`, `reserve`, and slice expressions
  (§16.6) are not meaningful on a `[N]T`; use a dynamic `[T]` when the length
  varies. (In the reference compiler a fixed array is not caught by a dedicated
  Tycho diagnostic here — it reaches the C backend, which emits no growth/slice
  operation for it, so the misuse surfaces as a C-compiler error rather than a
  clean language-level rejection.)
- **`len` is a compile-time constant.** `len(v)` for `v: [N]T` is the constant
  `N`, with no runtime length field. Indexing is bounds-checked against the
  static `N`; `==` compares element-wise.

A `[N]T` MAY be a struct field (stored inline), a by-value parameter, and a
return value. The size-generic form `[$N]T` — a template inferring `N` from a
fixed-array argument, meaningful only as a function parameter — is specified with
const generics in [§7.4](05-generics.md).

### 16.6 Slices `xs[a:b]`

A slice expression names a sub-range of a **dynamic** array. Four forms exist,
each with default bounds:

| Form | Range |
|---|---|
| `xs[a:b]` | indices `a` (inclusive) to `b` (exclusive) |
| `xs[a:]` | `a` to `len(xs)` |
| `xs[:b]` | `0` to `b` |
| `xs[:]` | the whole array |

Every bound is checked: `0 ≤ a ≤ b ≤ len(xs)` MUST hold, otherwise the program
aborts. The result is an **ordinary array value** of the same type as `xs`, so
its cost is decided entirely by its use:

- Passed to a function that only **reads** its parameter, a slice is a
  **zero-copy view** — the descriptor points into `xs`'s buffer, the same borrow
  an ordinary array argument is; nothing is copied.
- **Stored**, **returned**, or **pushed**, a slice deep-copies into an owning
  array, so value semantics holds: mutating `xs` afterward never touches the
  stored copy.

That is what keeps the view non-storable — it can never outlive or alias the
buffer it came from — without a borrow checker. Slices compose (`xs[1:5][1:3]`).
One aliasing constraint MUST be enforced: a slice of `xs` and an `inout` of `xs`
MUST NOT be passed to the same call, since the `inout` could reallocate the
viewed buffer.

A **string slice** `s[a:b]` (with the same `s[a:]` / `s[:b]` / `s[:]` forms)
also exists, but unlike an array view it **always copies** into a fresh
substring: there is no zero-copy string view. A string slice also **clamps**
out-of-range bounds rather than aborting (`start < 0` → `0`, `end > len` → `len`,
`end < start` → empty) — unlike an array slice, which aborts — because it is
exactly `substr(s, a, b)`, the equivalent function form (§29).

A **`bytes` slice** `b[a:b]` behaves identically to a string slice — always
copies, clamps rather than aborts, same four bound forms — and yields `bytes`
([§5.2.6](03-types.md#526-bytes)). It is the same operation on the same buffer;
only the static type of the result differs.

### 16.7 Element-type restriction

`void` MUST be rejected as a bracket-array element type in every form. `bool` is
rejected only in the **inline fixed-capacity** forms — `[N]T`, `[$N]T` and
`bounded[N]T` — which have no bool codegen. A **dynamic** `[bool]` is legal and
fully supported: literal, `push`, index-write, iteration, `str`, `==`, as a
`struct` field, as a map value, and nested.

| element form | `void` | `bool` |
|---|---|---|
| dynamic `[T]` | rejected | **accepted** |
| fixed `[N]T`, `[$N]T` | rejected | rejected |
| `bounded[N]T` | rejected | rejected |

The restriction applies only to the direct element of a bracket type; a `bool`
may appear inside any array indirectly (e.g. a `struct` field of a `[Struct]`
element).

> Provenance: dynamic `[T]` admits `bool` because it tests `void` alone —
> `src/tychoc.c:2560@elem`, and its diagnostic now states that as the whole rule
> instead of an allow-list (`src/tychoc.c:2578@void`; the loops-cleanup plan). The fixed forms test both:
> `src/tychoc.c:2134-2135` (`[$N]T`) and `src/tychoc.c:2156-2157` (`[N]T`);
> `bounded[N]T` at `src/tychoc.c:2047-2048`. Pinned both ways by
> `tests/bool_array.ty` (accepts, with a golden) and
> `tests/reject/fixarr_elem_bool.ty` / `tests/reject/bounded_elem_bool.ty`.

> Note: this section previously stated that `bool` MUST be rejected in **every**
> bracket form, including `[bool]`. That was never implemented — `tests/bool_array.ty`
> has exercised dynamic bool arrays since the drift hunt, and `tests/cond_stmt_expr.ty`
> carries a `[bool]` field — so the sentence was a spec defect, corrected here.
> The reference page `docs/reference/arrays-slices.md` gives only positive array
> examples and does not state the restriction — an under-documentation gap, not a
> contradiction. See also [§5.3.1](03-types.md#531-arrays-t).

### 16.8 Element-wise arithmetic

The five arithmetic operators of [§13.2](09-expressions.md#132-operators) apply
to arrays. `a OP b` on two arrays yields a **fresh** array whose `i`-th element
is `a[i] OP b[i]`; with one array and one scalar the scalar is **broadcast** over
every element.

```tycho
fn main():
    a := [1, 2, 3]
    b := [2, 2, 2]
    println(str(a * b))          # element-wise
    println(str(a * 2))          # broadcast, array on the left
    println(str(2 - [5, 1]))     # broadcast, scalar on the left — order kept
```

```output
[2, 4, 6]
[2, 4, 6]
[-3, 1]
```

**Legality is the scalar rule, element by element.** `a OP b` is legal **iff**
`a[i] OP b[i]` is legal, so the operator set follows the element type and the
array form is never more permissive than the scalar form:

| element type | operators |
|---|---|
| `int`, and `u8`/`u16`/`u32`/`u64`/`i8`/`i16`/`i32`/`i64` | `+ - * / %` |
| `float`, `f32`, a newtype over `int` or `float` | `+ - * /` |
| `char` | `+ -` |

Every other element type (`string`, `bytes`, a struct, a nested array, …) has no
scalar arithmetic and gets none here. `& | ^ << >>` are **not** element-wise —
they are not arithmetic — so an array operand reaches the bitwise and shift rules
of §13.2 and is refused there in their existing wording, unchanged.

The `char` row is the one this table cannot demonstrate by annotation: `char` has
no type keyword ([§5.2.4](03-types.md#524-char)), so a `[char]` is only ever
reached by **inference** — an array literal of `char_at` calls, or of character
literals. Its conformance witnesses are written that way: `tests/char_elem_ops.ty`
for `+` and `-`, and `tests/reject/char_elem_mul.ty`,
`tests/reject/char_elem_div.ty`, `tests/reject/char_elem_mod.ty` for the three
operators the row excludes. `%` leaves by a different gate from `*` and `/`
(`src/tychoc.c:1418@TK_PERCENT` rather than the `char` arm at
`:1422@et == T_CHAR`), which is why it has a fixture of its own.

**Both array kinds, and their two different mismatch rules.** For a `[N]T` the
length is static, so both operands MUST have the same `N` and a mismatch is a
**compile error** — the same requirement `==` already makes to compare two fixed
arrays element-wise (§16.5). For a `[T]` the lengths are not known until run time,
so a mismatch **aborts** with a diagnostic naming both lengths (`tycho:
element-wise arithmetic on arrays of different lengths (3 and 2)`, exit status
`1`), joining the abort set of §30.2. Neither side is padded and neither is
truncated: inventing an element would be a silent wrong answer.

**Mixing the kinds is refused.** A `[N]T` and a `[T]` cannot agree on a length at
compile time and carry different mismatch rules, so a mixed expression has no one
defensible behavior and is a compile error; copy one side into the other's kind
first. `bounded[N]T` and the size-generic `[$N]T` ([§7.4](05-generics.md)) are
refused for the same reason — `bounded` carries a capacity *and* a separate live
length, and no element-wise meaning for that pair is settled. The two arrays MUST
also share an element type: `[char] + [int]` is a compile error.

**Broadcast, both directions, order preserved.** `a OP s` and `s OP a` are both
defined for every legal operator, including the non-commutative `-`, `/` and `%`,
and the operands are never reordered: `2 - [5, 1]` is `[-3, 1]`, `[5, 1] - 2` is
`[3, -1]`. The scalar is evaluated once, not once per element. **Literal
adaptation** applies to the scalar against the **element** type, with the
value-directional restriction of [§8.1](06-conversions.md): a *literal* adapts
(`[1.0, 2.0] * 2` is `[2.0, 4.0]`), a *variable* never widens (with `n := 2`,
`[1.0, 2.0] * n` is a compile error exactly as `1.0 * n` is). After adaptation
the scalar's type MUST **equal** the element type, which makes `['a', 'b'] + 1` a
compile error even though the scalar `'a' + 1` is legal (§13.2's byte-domain
rule). That narrowing is deliberate: allowing it only in the broadcast direction
would make broadcast more permissive than the two-array rule above, which already
refuses `[char] + [int]`.

**The result is a fresh array.** Value semantics (§9) holds: neither operand is
aliased or mutated, mutating a source after `c := a * b` never changes `c`, and
growing one never grows the other. Every element goes through the same emit a
scalar operation does, so an element never gets fewer guards than a scalar:
division or modulo by an integer **literal** `0` is a compile error, by a zero
*value* or a zero *element* it aborts (`tycho: division by zero` / `tycho: modulo
by zero`, exit `1`), and `float`/`f32` division by zero stays IEEE (`inf`) — the
§13.2 rules exactly, not a second set.

Its fixtures are in `tests/` and `tests/abort/` (Appendix E.2.1).

> Provenance: two-array arm `src/tychoc.c:7139-7169`, broadcast arm
> `src/tychoc.c:7198-7225`; per-element-type operator set
> `src/tychoc.c:1417@elem_arith_ok`; fixed-length mismatch
> `src/tychoc.c:7756@on a fixed array requires the same static length`; mixed
> kinds `src/tychoc.c:7748@cannot mix a fixed array and a growable array`;
> `bounded`/`[$N]T` `src/tychoc.c:7730@IS_BOUNDED`; element-type mismatch
> `src/tychoc.c:7741@arr_elem(lt) != arr_elem(rt)`; scalar must land at the
> element type `src/tychoc.c:7810@requires the scalar to have the array's element type`,
> its literal adaptation `src/tychoc.c:7209-7215`; the fresh spine
> `src/tychoc.c:11312@arena_alloc`, the per-element emit shared with the scalar
> case `src/tychoc.c:11276@gen_arith_op`, operands never reordered
> `src/tychoc.c:11273@int la = is_array`; the runtime length check, emitted only
> when both sides are arrays `src/tychoc.c:11316@tycho_ew_len`, and the abort
> itself `runtime/tycho_rt.c:3190@arithmetic on arrays of different lengths`;
> literal-zero divisor `src/tychoc.c:7698@division by zero`.

---

## 17. Structs, tuples, `soa`

### 17.1 Struct declaration and construction

A `struct` is the nominal product type of
[§5.3.4](03-types.md#534-structs). It MUST be declared before it is used as a
type. Construction is **positional in field-declaration order**: `Point(1, 2)`
sets the first declared field to `1` and the second to `2`. Each argument's type
MUST match its field's type. Binding a struct deep-copies its whole tree — every
heap-owning field (`string`, array, map, nested struct, at any depth) is
duplicated — so two struct variables never share storage.

```tycho
struct Point:
    x: int
    y: int

a := Point(1, 2)         # positional, in declaration order
_b := a                   # deep copy; b.x, b.y independent of a
```

As a complete program:

```tycho
struct Point:
    x: int
    y: int

fn main():
    p := Point(3, 4)
    println("(" + str(p.x) + ", " + str(p.y) + ")")
```

```output
(3, 4)
```

### 17.1a Packed layout

A `struct` declaration MAY carry the `packed` attribute. `packed` is a
DECLARATION attribute, not a type constructor: it is a property of the
aggregate, so it does not participate in type identity and never reaches the
array-type table `bounded[N]T` uses.

A packed struct is laid out with **no padding between fields and no trailing
padding**: its size is exactly the sum of its field sizes, and each field sits
at the sum of the sizes before it. An unpacked struct's layout is
implementation-defined ([Appendix F](appendix-f-impl-defined.md)).

Every field of a packed struct MUST be a **fixed-width value type**: `int`,
`float`, `bool`, `char`, one of `u8`/`u16`/`u32`/`u64`/`i8`/`i16`/`i32`/`i64`/`f32`,
a newtype over one of those, or another packed struct. A heap-owning field
(`string`, `bytes`, an array, a map, an enum, an `Option`, a `Result`, a tuple)
is refused at the declaration, and so is a `$T` type parameter. `packed` on any
declaration other than a `struct` is refused.

Packing only ever REDUCES alignment, so a packed value is storable anywhere an
unpacked one is; the 8-byte arena rounding of §22 is unaffected.

```tycho
packed struct Ins:          # exactly 9 bytes: 1 + 4 + 4
    op: u8
    a: i32
    b: i32

fn main():
    i := Ins(to_u8(3), to_i32(-1), to_i32(7))
    println(str(i.op) + " " + str(i.a) + " " + str(i.b))
```

```output
3 -1 7
```

### 17.1a-2 Stated alignment

A `struct` declaration MAY instead carry the `align(N)` attribute, written
`align(N) struct Name:`. Like `packed` it is a DECLARATION attribute and not a
type constructor, and like `packed` it does not participate in type identity.

`align(N)` RAISES the aggregate's alignment to `N` bytes: the declaration's size
becomes a multiple of `N`, so consecutive elements of an array of it stay
aligned. It never lowers alignment below what the fields already require.

`N` MUST be a literal power of two, and it MUST NOT exceed **8**. The ceiling is
the arena's: §22's allocator rounds every allocation to 8 bytes and guarantees
nothing above that, so an alignment above 8 is refused at the declaration rather
than accepted and not delivered. Raising the arena's own rounding was measured
before this rule was written and costs 20-30% more arena bytes on every program.

`packed` and `align(N)` on the same declaration are a contradiction -- one
removes padding and the other adds it -- and are refused. `align` on any
declaration other than a `struct` is refused.

```tycho
align(8) struct Pair:       # every Pair, and every element of a [Pair], is 8-aligned
    a: u8
    b: u8

fn main():
    p := Pair(to_u8(1), to_u8(2))
    println(str(p.a) + " " + str(p.b))
```

```output
1 2
```

### 17.1b The bytes bridge

A packed struct is the only aggregate whose storage a program may move into or
out of a `bytes` value. Three forms:

- `size_of$(T)` is the size of the packed struct `T` in bytes, an `int`. `T`
  MUST be a packed struct (or a newtype over one); every other type's layout is
  implementation-defined and has no stated size.
- `to_bytes(v)`, where `v` is a packed struct, yields exactly `size_of$(T)`
  bytes.
- `from_bytes$(T)(b)` reads a `bytes` value back into a `T`. `len(b)` MUST equal
  `size_of$(T)` **exactly**; any other length is a runtime error naming the type
  and both lengths. A shorter buffer is not accepted and padded, and a longer
  one is not accepted and truncated.

**The byte order is LITTLE-ENDIAN on every host**, not the host's own. Each
field occupies its own width with the least significant byte first, fields in
declaration order, a nested packed struct flattened in place. So the same
program writes the same bytes on every target, which is what makes the pair
usable for a file format rather than only for a same-machine round trip.

This is safe because §17.1a already bars every heap and pointer field: a packed
struct is a flat run of fixed-width scalars, so no length, capacity or pointer
read out of a foreign buffer can be believed by the runtime.

```tycho
packed struct Ins:
    op: u8
    a: i32
    b: i32

fn main():
    b := to_bytes(Ins(to_u8(3), to_i32(-1), to_i32(7)))
    println(str(size_of$(Ins)) + " " + str(len(b)) + " " + str(b[1]))
    r := from_bytes$(Ins)(b)
    println(str(r.op) + " " + str(r.a) + " " + str(r.b))
```

```output
9 9 255
3 -1 7
```

### 17.2 Field access and places

A field is read with `p.x` and written with `p.x = v`. Writes compose through
nesting and through array-valued fields, and a field MAY be taken as an `inout`
argument; all of these are **places**:

```tycho
struct Point:
    x: int
    y: int

struct Tagged:
    x: int
    tags: [string]

struct Span:
    lo: Point
    hi: Point

fn bump(n: inout int):
    n = n + 1

r := Span(Point(0, 0), Point(1, 1))
p := Tagged(5, ["a"])

r.lo.x = 100             # a nested-struct field, in place
p.tags[0] = "x"          # an element of an array-valued field
push(p.tags, "y")        # grow an array-valued field, in place
bump(&p.x)               # a field as an `inout` argument
```

Two structs compare with `==`/`!=` field-wise, recursing into nested structs,
arrays, and strings, so `a == b` holds exactly when `b` is an independent copy of
`a` ([§5.5](03-types.md#55-equality-and-ordering)). A struct MAY be a method
(UFCS) receiver (§15).

### 17.3 Recursion only through a container

A struct field MAY be recursive **only through a container** whose elements are
allocated out of line: `children: [Node]` inside `Node` is permitted. A direct
by-value self-field — `next: Node`, or `next: Option(Node)` naming the enclosing
struct inline — is **rejected**, because it would make the struct infinitely
sized. This is a type-formation rule; it is stated normatively in
[§5.3.4](03-types.md#534-structs) and enforced there.

### 17.4 Tuples

A tuple `(T1, …, Tn)` is the anonymous product of
[§5.3.3](03-types.md#533-tuples), with **2 to 8** elements (a 1-element or
9-element tuple is a compile error, `src/tychoc.c:2483@least` (min) and `:2033@most` (max)). Tuples are first-class
values, not merely a return convention:

- **Construction.** A parenthesized list `(10, 20)`, or a bare `return a, b`,
  builds a tuple.
- **Multiple return values.** A function whose return type is a tuple returns
  several values as one tuple (§15).
- **Positional access and places.** `t.0`, `t.1`, … read an element; `t.0 = v`
  writes one in place (a tuple element is a writable place). An index out of
  `0 .. n-1` is a compile error (`src/tychoc.c:5860-5862`).
- **Equality.** Two tuples compare element-wise with `==`
  ([§5.5](03-types.md#55-equality-and-ordering)).

Any element type is allowed, including heap-bearing ones (`(string, [int])`); a
tuple is deep-copied on bind like every value.

### 17.5 Destructuring

A tuple-returning call (or any tuple value) MAY be destructured into several
names. Two forms exist, distinguished by the binding operator:

| Form | Meaning |
|---|---|
| `a, b := f()` | declares fresh locals `a` and `b` (new bindings) |
| `a, b = f()` | assigns into existing variables `a` and `b` |
| `(a, b) = f()` | assigns into 2–8 **places**, simultaneously (below) |
| `v.(x, y) = e` | assigns into 2–8 components of one value, simultaneously (below) |

The right-hand side MUST be a tuple, and the number of targets MUST equal the
tuple's arity; a mismatch is a compile error, as is a duplicate name in a `:=`
destructuring list (`src/tychoc.c:8183-8206`). At most 8 targets are permitted.
Each target receives its element deep-copied, preserving value semantics.

#### Simultaneous assignment

The parenthesised form `(t1, ..., tn) = e` differs from the bare one in two
ways. Its targets are **places** (§4.4) and not only names, so a field, an array
element or a map value may stand on the left; and its evaluation order is
specified: **every right-hand side, and every index expression appearing in a
target, is evaluated before any target is written.** A swap therefore needs no
temporary, and `(a, b) = (b, a + b)` advances a Fibonacci pair in one step —
the `a` read on the right is the old one. In `(i, xs[i]) = (2, 99)` the element
written is `xs[old i]`, because an index belongs to the left side's address and
not to the value.

A target MUST NOT be repeated. With every right-hand side evaluated first the
two writes have no order to tell them apart, so the statement would have no
meaning to give. (Go and Odin both accept the repetition and let the last write
win; Tycho does not.) Two targets are the same place when they are written the
same way down to a name, a field, a tuple index and a **literal** index: `a` and
`a`, `p.x` and `p.x`, `xs[0]` and `xs[0]`. Targets that are not decidable this
way are not compared: whether `xs[i]` and `xs[j]` denote one slot is not
decidable when the program is compiled.

A user `subscript` call is a place for the single-target form `p = e`, and it
MUST NOT stand in this list. The restriction is a limit of the present
implementations, not a property of the form.

The form is a statement of its own and MUST NOT appear as the init or post
clause of a three-clause `for` (§13). It lowers to a prelude that binds every
right-hand side ahead of the writes, and a `for` clause has nowhere to put one:
the prelude lands outside the loop, where the post clause's bindings would be
evaluated once instead of once per iteration, and where the init clause's are
not in scope at all.

```tycho
fn main():
    a := 1
    b := 2
    (a, b) = (b, a + b)            # a = 2, b = 3 -- NOT b = 4
    println(str(a) + " " + str(b))
```

```output
2 3
```

#### Swizzling

`v.(c1, ..., cn)` names two to eight **components of one value** and is a
[tuple](#174-tuples) of them, in the order written. A component is a struct
field name, or an integer **lane index** for an array, a `bounded` or a
`vector[N]T` — each type's components are spelled the way that type is already
read.

A swizzle is an ordinary expression: it MAY be read wherever a tuple value is
accepted, and it MAY stand on the left of a simultaneous assignment, where it is
the target list of those same components. `v.(x, y) = v.(y, x)` IS
`(v.x, v.y) = (v.y, v.x)` and carries that form's evaluation order unchanged, so
a swizzled swap needs no temporary and a three-component rotation
`v.(x, y, z) = v.(z, x, y)` is not the same statement as three assignments
written in order.

The base is repeated once per component, so it MUST be a variable or a field of
one; a base that could be evaluated twice, such as a call result, is rejected,
and so is one that carries an index (`vs[i].(x, y)`). An indexed base is
rejected even though the spelled-out `(vs[i].x, vs[i].y) = (vs[i].y, vs[i].x)`
is accepted: two targets are the same place only when their indices are
literals, so under a non-literal index the repeated-component rule below could
not be enforced. A component MUST NOT be repeated on the LEFT, by the rule
above. On the right a
repeat is ordinary duplication and is permitted: `v.(x, x)` is the pair
`(v.x, v.x)`.

```tycho
struct V:
    x: int
    y: int
    z: int

fn main():
    v := V(1, 2, 3)
    v.(x, y, z) = v.(z, x, y)      # a rotation, not three assignments
    println(str(v.x) + " " + str(v.y) + " " + str(v.z))

    w: vector[4]float = [1.0, 2.0, 3.0, 4.0]
    w.(0, 1) = w.(1, 0)            # lanes, by index
    println(str(w[0]) + " " + str(w[1]))
```

```output
3 1 2
2.0 1.0
```

```tycho
fn divmod(a: int, b: int) -> (int, int):
    q := a / b
    return q, a - q * b            # builds (quotient, remainder)

quot, rem := divmod(17, 5)         # fresh locals: quot = 3, rem = 2
```

### 17.6 `soa [Struct]`

`soa [S]` is the struct-of-arrays collection of
[§5.3.7](03-types.md#537-soa): its element type `S` MUST be a struct, otherwise
the type is rejected. It presents the **same value-semantic array interface** as
`[S]` — `push`, `pop`, indexing, element places, `len`, deep-copy on bind — while
storing each field of `S` in its own backing array. The externally observable
behavior of `soa [S]` MUST match `[S]`; the difference is storage layout, not
semantics.

---

## 18. Maps and subscripts

### 18.1 Map literals and type

A map `[K: V]` associates keys with values ([§5.3.5](03-types.md#535-maps-k-v)).
The legal **key types** `K` and the unrestricted **value type** `V` are defined
in §5.3.5 and are not re-derived here. The map type follows from a literal or an
annotation:

```tycho
_counts := ["ada": 1, "alan": 2]   # a [string: int]
_empty := []string: int            # empty; key and value types required
```

A composite key works in literal form too (`[Point(1, 2): 10]`). `len(m)` yields
the entry count.

### 18.2 `m[k]` as a place — writing inserts

`m[k]` on the left of an assignment, or as the spine of a place, is a **place**.
Writing to `m[k]`:

- **overwrites** the value if `k` is present;
- **inserts** the entry if `k` is absent, first initializing the slot to `V`'s
  zero (for a compound `V`, the zero-value is materialized before the write, so a
  field- or element-write lands on a valid zero-initialized value)
  (`src/tychoc.c:13103-13107`).

This makes the accumulator idioms one line each; the compiler proves the map is
uniquely owned at the mutation and updates it in place, so a `+=` loop is O(n)
total, not O(n²):

```tycho
struct Account:
    balance: int

counts := []string: int
index := []string: [int]
totals := ["ada": Account(10)]

counts["ada"] += 1                # zero-initialized on first sight, then incremented
push(index["tycho"], 1)           # grow a [string: [int]] value in place
totals["ada"].balance = 0         # mutate a struct-valued entry's field in place
```

### 18.3 `m[k]` as an rvalue — reading never inserts

Read as an rvalue, `m[k]` returns the value and MUST NOT insert. For an **absent**
key it yields:

- the **value type's zero** when `V` is scalar; and
- a **deep copy of the zero value** when `V` is composite.

Either way, an absent-key read leaves the map unchanged
(`src/tychoc.c:5991-6006`). When a non-zero default is wanted, use `m.get`
(§18.5).

### 18.4 Membership, `delete`, `len`

| Form | Meaning |
|---|---|
| `k in m` | membership test → `bool`; does **not** insert and does **not** iterate |
| `delete m[k]` | remove the entry for `k`; a **no-op** if `k` is absent |
| `len(m)` | entry count → `int` |

`delete m[k]` is a contextual-keyword statement that lowers to a functional
map-delete rebinding the map (`src/tychoc.c:3652-3668`,`:7025-7031`); it removes
at most one entry and never aborts on a missing key.

### 18.5 `m.get`

`m.get(k)` is exactly `m[k]` read as an rvalue (§18.3): it returns `V`'s zero (or
a deep copy of it) for an absent key and never inserts. `m.get(k, default)`
returns `default` on a miss instead of the zero value — the same read, spelled as
a method:

```tycho
counts := ["ada": 1]
counts["ada"] = counts.get("ada", 0) + 1   # equivalent to counts["ada"] += 1
```

### 18.6 `keys(m)`

`keys(m)` returns the map's live keys as an array `[K]` in **insertion order** —
the order in which each key was first inserted (`src/tychoc.c:13137-13140`; the
emitted `keys` walks the append-ordered entries array and keeps the live ones —
order falls out of append order plus an `elive` flag). It is the way to
iterate a map; `k in m` only tests membership. For a newtype or fieldless-enum
key type, `keys` returns the wrapped key values.

```tycho
counts := ["ada": 1, "alan": 2]
for k in keys(counts):
    println(k + " = " + str(counts[k]))
```

A complete program building a map, updating a value in place, and iterating it
in insertion order ([§30.4](17-runtime.md#304-defined-string-and-map-behavior)):

```tycho
fn main():
    counts := ["ada": 1, "alan": 2]
    counts["ada"] += 1
    for k in keys(counts):
        println(k + " = " + str(counts[k]))
```

```output
ada = 2
alan = 2
```

### 18.7 Subscripts — user-defined projections

A `subscript` declares a **compile-time place-macro**: a reusable, zero-copy
place into one of its parameters, generalizing the built-in `&m[k]`. Its grammar
is given in [§4.1.3](02-grammar.md#413-extern-functions-and-subscripts):

```text
subscript <name>(<recv>: T, <params>…) -> inout U:
    yield &<place>
```

A subscript does **not** return a value and has **no runtime object**. At a call
site `recv.name(args)` is not a function call: the compiler substitutes the
arguments into the yielded place `<place>` and inlines it, after which the
surrounding read or write flows through the ordinary place machinery. It is
therefore usable both as a **place** and as an **rvalue**, and callable as a
**method** on its first parameter, whose type selects the subscript:

```tycho
struct Node:
    weight: int

struct Graph:
    nodes: [Node]

subscript edge(g: Graph, i: int) -> inout Node:
    yield &g.nodes[i]

fn demo():
    g := Graph([Node(1), Node(2)])
    g.edge(1).weight = 10        # write in place through the projection — no copy
    w := g.edge(0).weight        # read through it
    println(str(w))
```

The following rules are checked at compile time and MUST be enforced; each fails
closed (the subscript is rejected, never silently mis-projected)
(`src/tychoc.c:4215-4266`):

- **Yields a place.** The body MUST be a single `yield &<place>`, where
  `<place>` is a field/index spine; a non-place operand is rejected.
- **Type match.** The yielded place's type MUST equal the declared `-> inout U`;
  a mismatch is rejected at the call site (dispatch `:4422-4426`,`:4497-4502`).
- **Rooted in a parameter.** The yielded place MUST be rooted in one of the
  subscript's parameters — it projects *into* an argument, so it cannot dangle. A
  place rooted in a fresh local is rejected.
- **Each parameter used at most once** in the yielded place, so no argument is
  double-evaluated when substituted.
- **Concrete receiver.** The receiver (first) parameter's type MUST NOT mention a
  type parameter; `subscript at(p: Pool($T), …)` is rejected at the declaration
  (`src/tychoc.c@parse_subscript`), whether or not it is ever called.

A subscript is a named place-macro on a concrete type, not a type-family
operation: the receiver selects it by exact type, so a `Pool($T)` receiver could
never match the monomorphised `Pool(int)` a call site actually presents. Tycho
follows Go and Odin here — neither has user-defined indexing at all; built-in
types get built-in indexing and user containers get procedures — so the limit is
deliberate rather than pending. Declaring the subscript on a concrete instance
(`subscript at(p: Pool(int), i: int) -> inout int`) is the way out; a generic
container that needs element access across all its instantiations exposes a
generic **function** instead.

Value semantics is unchanged: a projection is a place into a value already owned
or borrowed, and the usual mutability rules apply (writing through a projection
into a by-value parameter mutates that parameter's private copy). A projection is
scoped and transient — it MUST NOT be stored in a field or sent across a
`spawn`/channel boundary — which is what keeps it compatible with the value/arena
model. Subscripts make traversing an index-pool structure ergonomic; they are not
a stored-reference facility.

---

## 19. Enums, `Option`, `Result`, `match`

### 19.1 Enum declaration and variants

An `enum` is the nominal sum type of
[§5.3.6](03-types.md#536-enums-option-result): a value that is exactly one of
several named **variants**, each carrying a payload of zero to eight types.
Variant names are **package-scoped**: a variant is written bare (`Circle`, not
`Shape.Circle`), and **no two enums in one package may share a variant name**
(enums in different packages may reuse a bare name, disambiguated by the package
qualifier).

A variant name also shares one namespace with the package's `fn`, `const`,
`struct`, `enum`, `type` and `handle` names: a `fn` (or `const`) MUST NOT take
the name of a variant in the same package, in either declaration order, and an
implementation MUST reject the program (`error: 'Name' is already defined`).
The rule exists because a bare variant wins at the use site, so such a function
would be silently unreachable by its own name.

The four builtin constructors `Ok`, `Err`, `Some` and `None` are covered by the
same rule in **every** package, not just the one that declares them: they are
recognised by their spelling wherever they appear, so a declaration of one of
those names — `fn`, `const`, `struct`, `enum`, `type`, `handle`, or a variant —
is unreachable everywhere and MUST be rejected with the same diagnostic. The
§3.7 builtin-name rule reaches the same verdict for the same reason — the
builtin wins, so the declaration is unreachable by its own name. What differs is
reach: §3.7 governs the *unqualified* call spelling, while these four are
recognised by spelling in every position, so the rejection here is
unconditional. A `struct Ok` used to declare cleanly and fail only at its first use, with
a diagnostic about the builtin `Result` and no mention of the struct.

A **run-time binding** cannot collide with a variant at all, in any package: a
local, parameter or pattern binding must start with a lowercase letter or `_`
([§3.5.1](01-lexical.md#351-case-and-the-two-name-spaces)), so the collision
this section rules out for `fn` and `const` is not expressible for a binding.

```tycho
enum Shape:
    Circle(float)            # a variant with a payload
    Rect(float, float)
    Unit                     # a payload-less variant
```

An enum value is a small value-semantic descriptor whose payload lives out of
line, so binding one deep-copies the whole payload and `==` compares two values
structurally ([§5.5](03-types.md#55-equality-and-ordering)). An enum MAY appear
anywhere a type may. Recursive payloads (`Add(Expr, Expr)` inside `Expr`) are
permitted and finite because the payload is allocated out of line. A generic
`enum Name($T)` is monomorphized like a generic struct ([§7](05-generics.md)).

### 19.2 Construction

A value is built by naming a variant and supplying its payload —
`Circle(2.0)`, `Rect(3.0, 4.0)` — or, for a payload-less variant, by naming it
bare: `Unit`. Each payload argument's type MUST match the variant's declared
payload type.

### 19.3 `match` — the exhaustive eliminator

`match` dispatches on the variant and binds the payload in one step. To ask only
which variant a value holds, without binding anything, use `is` (§19.8). Two
rules make `match` safe:

- **Exhaustive.** Every variant of the matched type MUST have an arm; a missing
  variant is a **compile error**. A wildcard arm `_:` matches every remaining
  variant and MAY stand in for the unlisted cases when a catch-all is wanted.
- **Arm-local bindings.** A payload binding exists only inside its own arm;
  there is no way to read a payload that does not belong to the current variant.

```tycho
enum Shape:
    Circle(float)
    Rect(int, int)
    Unit

s := Circle(1.5)

match s:
    Circle(r):               # r bound to the payload
        println(str(r))
    Rect(w, h):              # multiple payload fields bind in order
        println(str(w * h))
    Unit:                    # no payload, no bindings
        println("unit")
```

### 19.4 Statement `match` versus value `match`

`match` exists in two positions:

- **Statement `match`** is a compound statement
  ([§4.3.2](02-grammar.md#432-compound-statements)); its arms are blocks that may
  contain any statements and produce a value by assigning or `return`-ing from
  inside each arm.
- **Value `match`** stands as the whole right-hand side of a `:=`, a typed
  `x: T =`, a plain assignment, or a `return` (tail position). Each arm is a block
  whose **final statement is a value expression** (a bare value expression is the
  one-statement case); leading ordinary statements may precede it. That final
  expression's value becomes the result; all arms MUST have the **same type**,
  which is the type of the whole expression. A value `match` is
  exhaustive exactly as the statement form is. The value form of `if` similarly
  requires an `else` (every path must produce a value). This desugars to the
  declare-then-assign-in-each-arm form (§14).

```tycho
enum Status:
    Active
    Idle
    Done

status := Active

_label := match status:          # value match — each arm ends in a value expression
    Active:  "on"
    Idle:    "waiting"
    Done:    "finished"
```

The same value `match` as a complete program, returned from a function:

```tycho
enum Status:
    Active
    Idle
    Done

fn label(s: Status) -> string:
    return match s:
        Active: "on"
        Idle: "waiting"
        Done: "finished"

fn main():
    println(label(Idle))
```

```output
waiting
```

### 19.5 `Option(T)`

`Option(T)` is the built-in enum with variants `Some(T)` and `None`
([§5.3.6](03-types.md#536-enums-option-result)). It replaces the null reference:
an absent value is *typed* absent and cannot be read without an exhaustive
`match`. `T` MAY be any type, including a nested `Option(Option(int))`.

`None` carries no type of its own, so a bare `None` is permitted **only where the
expected type is already known** — a return type, a declaration annotation
(`box: Option(string) = None`), an assignment target, or a call argument. A bare
`x := None` with no annotation is instead *pending* — grounded by its first use
(e.g. `x = Some(5)`), and a compile error only if never grounded. (Unlike `None`,
a bare `x := Ok(1)` or `x := Err(e)` *is* rejected immediately; the `Result`
constructors are not part of the pending set.) This context-grounding
of type-incomplete literals is specified in [§6.4](04-inference.md).

### 19.6 `Result(T, E)`

`Result(T, E)` is the built-in enum with variants `Ok(T)` and `Err(E)`
([§5.3.6](03-types.md#536-enums-option-result)) — error handling without
exceptions. A fallible function returns one, and the caller handles both outcomes
in a `match`. Both `T` and `E` MAY be any type. Like `None`, a bare `Ok(v)` fixes
only `T` and a bare `Err(e)` fixes only `E`; the other parameter is grounded from
context ([§6.4](04-inference.md)), and a bare `x := Ok(1)` is a compile error.

### 19.7 `or_return`

`or_return` is a postfix operator that propagates a failure without a `match`. It
binds tighter than any arithmetic, so it is valid anywhere the unwrapped value is
wanted (`foo(parse(s) or_return)`, `return Ok(parse(s) or_return + 1)`).

On a **`Result`**, `v := expr or_return`:

- binds `v` to the payload when `expr` is `Ok(v)`; and
- otherwise returns that `Err` from the **enclosing function**, which MUST itself
  return `Result(_, E)` with the **same** error type `E` — a differing `E` is a
  compile error (`src/tychoc.c:5778-5786`).

On an **`Option`**, `v := opt or_return` binds `v` on `Some(v)` and returns
`None`; the enclosing function MUST return an `Option` (`src/tychoc.c:5869-5873`).

The short-circuited payload (`Err`'s error, or `None`) is promoted into the
caller's storage, so it outlives the return
([§10](07-memory-model.md#10-object-lifetimes-and-storage)). `or_return` MUST NOT
appear inside a `parallel for` body — a chunk has no early exit
(`src/tychoc.c:8079@or_return`).

```tycho
fn parse_digit(s: string) -> Result(int, string):
    if len(s) == 1 and s >= "0" and s <= "9":
        return Ok(to_int(to_float(s[0]) - to_float("0"[0])))
    return Err("not a digit: " + s)

fn add_two(a: string, b: string) -> Result(int, string):
    x := parse_digit(a) or_return    # Ok → bind x; Err → return it from add_two
    y := parse_digit(b) or_return
    return Ok(x + y)
```

A complete program using `or_return` to thread `Result` and `match` to consume
it:

```tycho
fn checked(n: int) -> Result(int, string):
    if n < 0:
        return Err("negative")
    return Ok(n)

fn add(a: int, b: int) -> Result(int, string):
    x := checked(a) or_return
    y := checked(b) or_return
    return Ok(x + y)

fn main():
    match add(3, 4):
        Ok(v): println("ok " + str(v))
        Err(e): println("err " + e)
```

```output
ok 7
```

### 19.8 `is` — the variant test

`match` is the eliminator, but it is not a *question*: it dispatches and binds in
one step, so asking only "which variant is this?" costs an arm per variant. `is`
is that question as an expression.

`v is V` yields `bool` — `true` iff `v` currently holds variant `V`
([§13.2](09-expressions.md#132-operators)). Its rules:

- The left operand MUST be an enum, an `Option` or a `Result`. Any other type is
  a compile error.
- The right operand is a **variant name**, not an expression, spelled exactly as
  a `match` arm spells it: bare `V` within the declaring package, qualified
  `pkg.V` from outside it (§19.1). A name that is not a variant of that enum is a
  **compile error** naming both, never a test that is silently `false`.
- It binds **nothing**. `is` answers which variant; `match` is still the only way
  to read a payload, and remains the only exhaustive form.
- It does not chain (`a is X is Y` is a compile error).

Unlike `==`, it applies to a **payload-carrying** variant: `V` alone is not a
value there, so `v == V` is refused, and before `is` the only test was a `match`.
For a payload-less variant `v == V` stays legal and means the same thing
(§13.2).

```tycho
enum Shape:
    Circle(float)
    Rect(float, float)
    Unit

fn main():
    s := Rect(3.0, 4.0)
    println(str(s is Rect))
    println(str(s is Circle))
    println(str(Unit is Unit))
    if s is Rect and not (s is Unit):
        println("a rect, and not the unit")
```

```output
true
false
true
a rect, and not the unit
```

`Option` and `Result` answer `is` on the same terms, with their constructors'
own names: `Some` / `None` for an `Option` (§19.5), `Ok` / `Err` for a `Result`
(§19.6). Those four names are never package-qualified. A name from the other
family — `r is Some` on a `Result` — is a **compile error** naming the type and
the two names that are valid, on the same rule as an unknown enum variant above.
`or_return` still propagates; `is` only asks.

```tycho
fn lookup(k: int) -> Option(int):
    if k > 0:
        return Some(k)
    return None

fn parse(s: string) -> Result(int, string):
    if s == "1":
        return Ok(1)
    return Err("bad")

fn main():
    println(str(lookup(3) is Some))
    println(str(lookup(0) is Some))
    println(str(parse("1") is Ok))
    println(str(parse("x") is Err))
```

```output
true
false
true
true
```
