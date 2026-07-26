# 14. Statements and control flow

The grammar of statements is in
[§4.3](02-grammar.md#43-blocks-and-statements); this chapter defines their
meaning. Declarations and assignments are covered in
[§12](08-declarations.md); this chapter covers control flow.

> Provenance: `parse_stmt` `src/tychoc.c:2605-2950` (`parse_if` `:2338`,
> `parse_match` `:2409`, `for` `:2741-2826`, `select` `:2686-2722`). Loop and `match` behaviors marked
> "probed" were confirmed on both compilers (spec-plan.md §6a).

## 14.1 Blocks

A block is an indentation-delimited sequence of one or more statements
([§3.4](01-lexical.md#34-indentation-indent--dedent)). Each block is a scope
([§12.3](08-declarations.md#123-scope-and-shadowing)). The only bare-expression
statement permitted is a call; a bare variable, index, field, or `or_return`
expression is rejected as having no effect.

## 14.2 `if` / `elif` / `else`

```text
if C1: … elif C2: … else: …
```

Each condition MUST be `bool`. Conditions are tested in order; the first `true`
condition's block runs and the rest are skipped; if none is true and an `else` is
present, its block runs. `elif` is exactly sugar for an `else` block containing a
single nested `if`. A value-producing `if` (in tail position) is defined in
[§13.5](09-expressions.md#135-expression-valued-if-and-match).

## 14.3 `match`

```text
match E: <arm>+
```

`match` evaluates its subject `E` **exactly once** (*probed*) and runs the arm
whose pattern matches. A pattern is a variant name (optionally binding its
payload into 0–8 names local to the arm), a qualified `pkg.Variant`, or the
wildcard `_`. A `match` MUST be **exhaustive**: every variant of the subject's
type MUST be covered, or a `_` arm MUST be present; a non-exhaustive `match` is
rejected at compile time. Payload bindings are visible only within their arm. The
statement form runs a block per arm; the value form (arms are blocks ending in a
value expression, tail position) is defined in §13.5.

### 14.3.1 Nested patterns

An `Ok`, `Err` or `Some` arm MAY refine its single payload with **one** nested
pattern instead of binding it: `Err(net.Timeout)`, `Err(Timeout)`,
`Err(TooBig(n))`. The nested pattern names a variant of the payload's enum type
and MAY bind that variant's own payload into 0–8 names. Nesting is **one level
deep** and is permitted **only** on an `Option`/`Result` payload: inside a plain
enum arm a nested pattern is rejected, and the payload MUST be matched in its own
`match`.

Because the payload's enum type is already known at the pattern, the nested
variant MAY be written **unqualified** even when its enum belongs to another
package — `Err(Timeout)` and `Err(net.Timeout)` are the same pattern. It follows
that **a name that is a variant of the payload's enum is always a pattern, never a
binding**: `Err(Timeout)` does not bind a variable called `Timeout`. Where such a
name is not a legal pattern (it carries a payload but was written bare, or the
enclosing arm is a plain enum arm) the program is **rejected**; it is never
silently read as a binding.

A side of the match (its `Ok` arms, its `Err` arms, its `Some` arms) is an ordered
decision list: refined arms are tested in source order, then the unrefined arm for
that side, if any. An unrefined arm therefore MUST come last among its side's
arms — a refined arm written after one is dead and is rejected as a duplicate. Two
refined arms naming the same variant are likewise rejected. A side is exhaustive
when it has an unrefined arm, when its refined arms name every variant of the
payload's enum, or when a `_` arm is present.

```tycho
match httpd.read_request(fd):
    Ok(req): serve(req)
    Err(httpd.TooLarge): refuse(431)
    Err(httpd.Timeout): refuse(408)
    Err(e): close()
```

## 14.4 Loops

Tycho has one loop keyword, `for`, in three shapes; `break` and `continue` are
valid in every shape and are errors outside a loop.

**Condition (`while`) form** — `for C:` runs its body while the `bool` condition
`C` holds.

**Counting form** — `for i in range(a, b, step):` binds `i` (an `int`, scoped to
the loop) to successive values from `a` toward `b`, stepping by `step`. `range(n)`
means `range(0, n, 1)`; `range(a, b)` means step `1`. The bound `b` is exclusive.
A **negative step** counts downward. A **zero step** never advances the counter,
so it is a program error: a literal `0` step is **rejected at compile time**, and
a step that evaluates to `0` at run time **aborts** (`tycho: range step is zero`)
— the same fail-closed discipline as division by zero.

**Foreach form** — `for x in xs:` iterates a collection: an array (binding each
element) or a `string` (binding each byte as an `int`). The **collection is
evaluated exactly once** before the loop.

## 14.5 `return`

`return` with no operand returns from a `void` function. `return e` returns the
value of `e`, checked against the function's return type. `return a, b, …`
returns a tuple (2–8 elements). `return if … / return match …` returns the value
of a tail-position `if`/`match` (§13.5). A returned value is produced directly in
the caller's storage ([§10.2](07-memory-model.md#102-the-escape-rule)).

## 14.6 `or_return`

`v := e or_return` evaluates `e`, which MUST be an `Option` or a `Result`. For a
`Result`, `Ok(x)` binds `v := x` and execution continues, while `Err(err)` causes
the enclosing function to `return Err(err)` immediately — the function MUST return
a `Result(_, E)` with the same error type `E`. For an `Option`, `Some(x)` binds
`v := x` and `None` causes the enclosing function to `return None`. The
short-circuited `Err`/`None` payload is promoted into the caller's storage so it
outlives the return. `or_return` binds tighter than any binary operator.

## 14.7 `delete`

`delete m[k]` removes the entry for key `k` from the map place `m` (which may be
a bare variable, a field, or a nested place). Deleting an absent key is a no-op.

## 14.8 `die`, `exit`, and termination

`die(msg)` prints `msg` to standard error and terminates the program with status
`1`. `exit(code)` terminates with status `code` and prints nothing — that is the
one for a program that has *answered* (a `--help` needs `0`, which `die` cannot
give). Both are typed `void`, so a non-`void` function that `die`s in one branch
still type-checks, **and** both are modelled as **diverging**, so either is legal
as the tail of a value `if`/`match` branch even though it produces no value
([§29.12.1](16-builtins.md)). `die` is the only user-callable *abort*
([§29](16-builtins.md); there is no `assert`/`panic`). Other
terminating conditions (division by zero, out-of-bounds access, and the like) are
the defined runtime aborts of [§30](17-runtime.md). Normal
termination occurs when `main` returns, after which all program storage is
reclaimed ([§10.3](07-memory-model.md#103-observable-storage-guarantees)).
