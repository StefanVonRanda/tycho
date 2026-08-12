# 14. Statements and control flow

The grammar of statements is in
[§4.3](02-grammar.md#43-blocks-and-statements); this chapter defines their
meaning. Declarations and assignments are covered in
[§12](08-declarations.md); this chapter covers control flow.

> Provenance: `parse_stmt` `src/tychoc.c:3244-3724` (`parse_if` `:3160@parse_if`,
> `parse_match` `:3266@parse_match`, `for` `:3381-3582`, `select` `:3325-3361`). Loop and `match` behaviors marked
> "probed" were confirmed on both compilers (spec-plan.md §6a).

## 14.1 Blocks

A block is an indentation-delimited sequence of one or more statements
([§3.4](01-lexical.md#34-indentation-indent--dedent)). Each block is a scope
([§12.3](08-declarations.md#123-scope-and-shadowing)). A block cannot be empty;
`pass` is the no-op that gives one a body. The bare-expression
statements permitted are a call, and a call followed by `or_return` when the
callee's ok payload is `void` ([§5.3.6](03-types.md#536-enums-option-result));
a bare variable, index or field expression is rejected as having no effect, and
so is an `or_return` over any other payload type, which would drop a value.

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

### 14.3.2 Scalar subjects

A `match` whose subject is an `int`, `char`, or `bool` takes **scalar arms**: a
literal (`1:`, `'a':`, `true:`), an inclusive range (`1..9:`), a set
(`1 | 3 | 5:`), a set mixing ranges (`45 | 48..57:`), or an `int` constant name
(`OP_ADD:`, `OP_ADD..OP_GE:`). The arm elements must be the subject's own kind:
an `int` subject takes int literals and int constants; a `char` subject takes
char literals; a `bool` subject takes `true`/`false`. A range's two ends must be
the same kind, and its start must not exceed its end. The subject is evaluated
exactly once, as in §14.3.

`_` is **required** for `int` and `char` subjects — the domain is unbounded, so
exhaustiveness is unprovable — and a non-exhaustive scalar `match` is rejected
at compile time, exactly like a non-exhaustive enum `match`. A `bool` subject is
exhaustive when both `true` and `false` are covered; `_` is optional there.

Duplicate or **overlapping** arms are an error: `1..9` plus `5` is a duplicate,
because an earlier arm would be dead. `match` on `string`, `bytes`, or `float`
is refused — nothing in the tree dispatches on them; `if`/`elif` is the form.

The statement and value forms are those of §14.3. Codegen: a match with 4+ arms
emits a C `switch` (a jump table at `-O3` when the values are dense); fewer arms
emit a chain.

```tycho
match httpd.read_request(fd):
    Ok(req): serve(req)
    Err(httpd.TooLarge): refuse(431)
    Err(httpd.Timeout): refuse(408)
    Err(e): close()
```

## 14.4 Loops

Tycho has one loop keyword, `for`, in four shapes; `break` and `continue` are
valid in every shape and are errors outside a loop.

**Condition (`while`) form** — `for C:` runs its body while the `bool` condition
`C` holds.

**Infinite form** — `for:` runs its body until a `break` or a `return`. It is
the condition form with a literal `true` and nothing about it is otherwise
special.

**Three-clause form** — `for init; cond; post:` runs `init` once, then runs the
body while the `bool` `cond` holds, running `post` after every iteration. `init`
is a declaration or an assignment and `post` is an assignment to a variable; a
variable that `init` declares is scoped to the loop, and `post` is evaluated in
the loop's scope, not the body's, so it cannot read a body-local. **All three
clauses are required** — this grammar has no empty-statement production to put
in one — and `for:` is the only degenerate form. A `continue` **runs the post
clause**: it jumps *to* `post`, not past it, so a loop that advances only in
`post` still terminates.

**Foreach form** — `for x in xs:` iterates a collection: an array (binding each
element) or a `string` (binding each byte as an `int`). The **collection is
evaluated exactly once** before the loop.

There is **no counting form**, and `range` is not a name the language knows.
`for i in range(a, b, step):` was removed on 2026-07-29; a `for` head that names
`range` is refused with both replacements spelled out. Count sequentially with
the three-clause form; count in parallel with `parallel for i in 0..<N:`
([§22](13-concurrency.md#22-parallel-for)), which is the only context where
`0..<N` is legal — a sequential `for i in 0..<N:` is refused.

**The zero-step guarantee is gone, and that is a deliberate trade, not an
oversight.** `range()` rejected a literal `0` step at compile time and
**aborted** on a step that evaluated to `0` at run time — the same fail-closed
discipline as division by zero. A three-clause `post` is arbitrary code, so no
equivalent check exists: `for i := 0; i < n; i += 0:` is an infinite loop and
the implementation **does not diagnose it**, at compile time or at run time.
`0..<N` steps by `1` implicitly and so has no zero-step case at all. What was
bought is a single loop form that says its own direction and amount in the
source instead of inferring them from the sign of a step expression.

> Provenance: bare `for:` `src/tychoc.c:3739@TK_COLON`; the three-clause header
> scan and its five required-clause refusals `src/tychoc.c:3415-3464`; `init`
> parsed by `parse_stmt` itself `src/tychoc.c:3790@parse_stmt`; loop scoping and
> the post clause resolved outside the body block `src/tychoc.c:7518-7523`;
> `continue` emitted as `goto _post<id>` `src/tychoc.c:11070-11073` with the
> label at `src/tychoc.c:11937@_post%d`; the `range()` refusal
> `src/tychoc.c:3873@was removed: write`. There is no step in the implementation
> at all: `Stmt` carries `r_start` and `r_stop` only (`src/tychoc.c:1617-1623`)
> and every `S_FORRANGE` emits `h_i < _stopN; h_i += 1`
> (`src/tychoc.c:11243-11247`).
>
> **Amended 2026-07-30 (the loops-cleanup plan).** This note previously read "The step
> codegen and its zero-step guards still exist but are unreachable: every
> remaining `S_FORRANGE` producer writes a NULL step", citing the `Stmt` field
> `r_step`. That was true from 2026-07-29, when `range(a, b, step)` — the field's
> only producer — was removed, until 2026-07-30, when the field, the step
> codegen, the `tycho: range step is zero` abort, the `_stepN > 0 ? … : …`
> direction ternary, the literal-zero-step refusal and the `parallel for` step
> refusal were all deleted. The paragraph above is unaffected: the zero-step
> guarantee was already gone as a language guarantee, and this only removes the
> dead machinery that used to implement it.

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
