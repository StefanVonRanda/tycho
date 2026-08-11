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

> Provenance: array element restriction `src/tychoc.c:2072-2073`,`:2089-2090`;
> `pop`-empty abort `:12981@pop from an empty array`,`:13170@pop from an empty array`; `reserve` `:6019-6045`,`:9328-9334`; tuple
> arity `:2316@a tuple has at most 8 elements`,`:2320@a tuple type needs at least two elements`, index `:4978-4986`; destructuring `:3410-3424`,`:6984-7000`;
> map read (pure `map_get`, no insert) `:5289-5304`; map place insert+zero
> `:10176-10185`; `keys()` insertion order — the walk `:13139@m.elive[e]` over the append-only entries array `:13094@m->ecount++`; `delete` → `map_del`
> `:3258-3282`,`:5948-5954`; subscript parse + rules `:3895-3947`, dispatch
> `:3969-3977`; `or_return` `:5165-5182`.

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
ys := []int             # empty; element type required
zs := xs                # zs is an independent deep copy — mutating zs never touches xs
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
a[i] = v                 # replace an element
ps[0].x = 10             # a field of a struct element
push(ps[0].tags, "x")    # grow an array-valued field of an element, in place
grid[1][2] = 60          # an element of a nested array
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
`[T]`. `pop(a)` on an **empty** array MUST abort (`src/tychoc.c:12121`); it is not
silently zero-returning. `reserve(a, n)` is a capacity hint only — it copies the
existing elements into a buffer of capacity `≥ n` and is a no-op when
`n ≤ cap`; it never changes `len` and never inserts elements
(`src/tychoc.c:12105-12110`).

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
> `src/tychoc.c:2381@elem`, and its diagnostic now states that as the whole rule
> instead of an allow-list (`src/tychoc.c:2399@void`; the loops-cleanup plan). The fixed forms test both:
> `src/tychoc.c:2055-2056` (`[$N]T`) and `src/tychoc.c:2074-2075` (`[N]T`);
> `bounded[N]T` at `src/tychoc.c:1989-1990`. Pinned both ways by
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
(`src/tychoc.c:1353@TK_PERCENT` rather than the `char` arm at
`:1357@et == T_CHAR`), which is why it has a fixture of its own.

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

This is **post-freeze** syntax: it is a type error to the frozen `tychoc0`. Its
fixtures lived in `tests/postfreeze/` until the `tychoc0` lanes were retired on
2026-07-29; they are now in `tests/` and `tests/abort/` (Appendix E.2.1).

> Provenance: two-array arm `src/tychoc.c:6315-6345`, broadcast arm
> `src/tychoc.c:6374-6400`; per-element-type operator set
> `src/tychoc.c:1352@elem_arith_ok`; fixed-length mismatch
> `src/tychoc.c:6853@on a fixed array requires the same static length`; mixed
> kinds `src/tychoc.c:6845@cannot mix a fixed array and a growable array`;
> `bounded`/`[$N]T` `src/tychoc.c:6835@IS_BOUNDED`; element-type mismatch
> `src/tychoc.c:6838@arr_elem(lt) != arr_elem(rt)`; scalar must land at the
> element type `src/tychoc.c:6906@requires the scalar to have the array's element type`,
> its literal adaptation `src/tychoc.c:6385-6390`; the fresh spine
> `src/tychoc.c:10395@arena_alloc`, the per-element emit shared with the scalar
> case `src/tychoc.c:10379@gen_arith_op`, operands never reordered
> `src/tychoc.c:10376@int la = is_array`; the runtime length check, emitted only
> when both sides are arrays `src/tychoc.c:10399@tycho_ew_len`, and the abort
> itself `runtime/tycho_rt.c:2920@arithmetic on arrays of different lengths`;
> literal-zero divisor `src/tychoc.c:6803@division by zero`.

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
b := a                   # deep copy; b.x, b.y independent of a
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

### 17.2 Field access and places

A field is read with `p.x` and written with `p.x = v`. Writes compose through
nesting and through array-valued fields, and a field MAY be taken as an `inout`
argument; all of these are **places**:

```tycho
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
9-element tuple is a compile error, `src/tychoc.c:2018` (min) and `:2014` (max)). Tuples are first-class
values, not merely a return convention:

- **Construction.** A parenthesized list `(10, 20)`, or a bare `return a, b`,
  builds a tuple.
- **Multiple return values.** A function whose return type is a tuple returns
  several values as one tuple (§15).
- **Positional access and places.** `t.0`, `t.1`, … read an element; `t.0 = v`
  writes one in place (a tuple element is a writable place). An index out of
  `0 .. n-1` is a compile error (`src/tychoc.c:5161-5163`).
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

The right-hand side MUST be a tuple, and the number of names MUST equal the
tuple's arity; a mismatch is a compile error, as is a duplicate name in a `:=`
destructuring list (`src/tychoc.c:7272-7294`). At most 8 targets are permitted.
Each name receives its element deep-copied, preserving value semantics.

<!-- fence-skip: shows a fn beside the call site's binding form; Tycho has no top-level statements, so the pair is illustrative, not a program -->
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
counts := ["ada": 1, "alan": 2]   # a [string: int]
empty := []string: int            # empty; key and value types required
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
  (`src/tychoc.c:12220-12224`).

This makes the accumulator idioms one line each; the compiler proves the map is
uniquely owned at the mutation and updates it in place, so a `+=` loop is O(n)
total, not O(n²):

```tycho
counts[w] += 1                    # zero-initialized on first sight, then incremented
push(index[term], doc)            # grow a [string: [int]] value in place
totals[user].balance = 0          # mutate a struct-valued entry's field in place
```

### 18.3 `m[k]` as an rvalue — reading never inserts

Read as an rvalue, `m[k]` returns the value and MUST NOT insert. For an **absent**
key it yields:

- the **value type's zero** when `V` is scalar; and
- a **deep copy of the zero value** when `V` is composite.

Either way, an absent-key read leaves the map unchanged
(`src/tychoc.c:5289-5304`). When a non-zero default is wanted, use `m.get`
(§18.5).

### 18.4 Membership, `delete`, `len`

| Form | Meaning |
|---|---|
| `k in m` | membership test → `bool`; does **not** insert and does **not** iterate |
| `delete m[k]` | remove the entry for `k`; a **no-op** if `k` is absent |
| `len(m)` | entry count → `int` |

`delete m[k]` is a contextual-keyword statement that lowers to a functional
map-delete rebinding the map (`src/tychoc.c:3255-3271`,`:5946-5952`); it removes
at most one entry and never aborts on a missing key.

### 18.5 `m.get`

`m.get(k)` is exactly `m[k]` read as an rvalue (§18.3): it returns `V`'s zero (or
a deep copy of it) for an absent key and never inserts. `m.get(k, default)`
returns `default` on a miss instead of the zero value — the same read, spelled as
a method:

```tycho
counts[w] = counts.get(w, 0) + 1    # equivalent to counts[w] += 1
```

### 18.6 `keys(m)`

`keys(m)` returns the map's live keys as an array `[K]` in **insertion order** —
the order in which each key was first inserted (`src/tychoc.c:12254-12257`; the
emitted `keys` walks the append-ordered entries array and keeps the live ones.
The **insertion-ordered link chain** the two refs here formerly named — a `nxt`
field threaded through the slot table — no longer exists anywhere in the
compiler, so those refs are not repointed but retired: order now falls out of
append order plus an `elive` flag, not out of a chain). It is the way to
iterate a map; `k in m` only tests membership. For a newtype or fieldless-enum
key type, `keys` returns the wrapped key values.

```tycho
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
subscript edge(g: Graph, i: int) -> inout Node:
    yield &g.nodes[i]

g.edge(1).weight = 10        # write in place through the projection — no copy
w := g.edge(0).weight        # read through it
bump(&g.edge(0).weight)      # a field of the projection as an `inout` argument
```

The following rules are checked at compile time and MUST be enforced; each fails
closed (the subscript is rejected, never silently mis-projected)
(`src/tychoc.c:3895-3946`):

- **Yields a place.** The body MUST be a single `yield &<place>`, where
  `<place>` is a field/index spine; a non-place operand is rejected.
- **Type match.** The yielded place's type MUST equal the declared `-> inout U`;
  a mismatch is rejected at the call site (dispatch `:4422-4426`,`:4497-4502`).
- **Rooted in a parameter.** The yielded place MUST be rooted in one of the
  subscript's parameters — it projects *into* an argument, so it cannot dangle. A
  place rooted in a fresh local is rejected.
- **Each parameter used at most once** in the yielded place, so no argument is
  double-evaluated when substituted.

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
recognised by their spelling wherever they appear, so a `fn` or `const` of one
of those names is unreachable everywhere and MUST be rejected with the same
diagnostic. This is not the §3.7 builtin shadowing that a warning describes —
that shadowing redirects a name to the new declaration, and these four cannot
be redirected at all.

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
match s:
    Circle(r):               # r bound to the payload
        …
    Rect(w, h):              # multiple payload fields bind in order
        …
    Unit:                    # no payload, no bindings
        …
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
label := match status:          # value match — each arm ends in a value expression
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
  compile error (`src/tychoc.c:5173-5181`).

On an **`Option`**, `v := opt or_return` binds `v` on `Some(v)` and returns
`None`; the enclosing function MUST return an `Option` (`src/tychoc.c:5167-5171`).

The short-circuited payload (`Err`'s error, or `None`) is promoted into the
caller's storage, so it outlives the return
([§10](07-memory-model.md#10-object-lifetimes-and-storage)). `or_return` MUST NOT
appear inside a `parallel for` body — a chunk has no early exit
(`src/tychoc.c:6639`).

<!-- fence-skip: calls parse_digit, defined in the prose above, and has no main; the complete program follows immediately below and IS checked -->
```tycho
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
