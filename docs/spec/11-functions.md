# 15. Functions

The grammar of function declarations, parameters, and the `where` clause is in
[§4.1.1](02-grammar.md#411-functions). This chapter defines parameter passing,
variadics, first-class function values, method-call syntax, and subscripts.

> Provenance: `parse_fn` `src/tychoc.c:3933-4057`; parameter modes and the
> `sink`/`inout` semantics `docs/reference/basics.md:24-70`,
> `docs/reference/functions.md`.

## 15.1 Declarations

A function has a name, a parameter list, an optional `-> R` return type (absent
means `void`), an optional `where` clause (for a generic function,
[§7.2](05-generics.md#72-constraints-where)), and a block body. A function that
mentions a `$T` or `$N` in its signature is generic (§7). A function MUST NOT
return a `handle` ([§25](14-ffi.md)), and MUST NOT declare more than **16**
parameters (the same cap for an `extern fn`; a *function type*, which is a
distinct construct, allows up to 8 — [§5.3.8](03-types.md#538-function-types)).
The 16-parameter cap applies to a **generic template** exactly as it does to a
concrete function: being generic defers what the parameters *are*, never how
many there may be (`src/tychoc.c:9118@parameters`, ahead of the template stash; locked by
`tests/reject/params_17.ty`, `extern_params_17.ty` and
`generic_params_17.ty`).
Exactly one function named `main`, with no parameters and `void` return, is the
program entry point ([§27](15-program.md)).

## 15.2 Parameter passing modes

A parameter is passed in one of three modes:

- **Default (by value).** For a scalar, the callee receives a copy. For a
  heap-bearing value (a `string`, an array, a map, a heap-bearing struct), the
  callee receives a **read-only borrow** — it may read but not mutate the value,
  and mutating a borrowed parameter is a compile error. Because the caller's
  value is unchanged, this is value semantics with a copy elided where the callee
  cannot observe the difference. A value produced or returned from the callee is
  copied into the appropriate storage as usual.
- **`inout` (copy-in/copy-out borrow).** Specified in [§11](07-memory-model.md#11-inout):
  an exclusive, call-scoped mutable borrow written `&x` at the call site,
  equivalent to `x = f(x)`, with the exclusivity rule of §11.2.
- **`sink` (owned, consuming).** The callee **owns and consumes** the argument
  and may mutate it in place. The caller's argument is consumed: after passing a
  variable to a `sink` parameter, the caller MUST NOT use that variable again
  (doing so is a compile error, not a silent copy). A fresh literal or a local on
  its last use is adopted with **no copy**; a copy is made only where value
  semantics require independence (the variable is read again after the call, or
  is captured by a closure). **Inside a loop a named variable is neither adopted
  nor silently copied — the call is REFUSED**, because a copy hidden in a loop is
  the one place the move-vs-copy cost is invisible and unbounded; pass a fresh
  value (a literal, or a call's result) or lift the call out. Until 2026-08-14
  this paragraph listed "used inside a loop" among the silent-copy cases, which
  no implementation ever did (`src/tychoc.c@sink_arg_into`). A value that then escapes
  (is returned or stored past the call) is still copied to the longer-lived
  storage.

`sink` and `inout` are mutually exclusive on one parameter, and neither may
combine with a variadic parameter.

## 15.3 Variadic parameters

A final parameter written `...T` has type `[T]`; a call packs its trailing
positional arguments into that array. `f()` supplies an empty `[]T`; an existing
`[T]` is forwarded with the spread form `f(xs...)`. Fixed parameters may precede
the variadic one, which MUST be last. The generic form `...$T` infers `T` from
the arguments; an EMPTY call to one has nothing to infer from, so it is an error
unless the type is named explicitly — `f$(int)()` supplies the empty `[]int` and
is the cure the diagnostic points at (§7.5). A variadic parameter cannot be
`inout` or `sink`. Variadics are pure sugar: the call folds the trailing
arguments into one array argument, after which it is an ordinary call.

The packing is a property of the CALL, not of how the callee was named: a
package-qualified call packs identically (`vp.sum(1, 2, 3)`), and so does one
carrying explicit type arguments. Until 2026-08-14 neither did — the fold was
skipped for a qualified callee and the call was measured against the single
declared parameter (`'vp__sum' takes 1 argument(s), got 3`), and the qualified
`$(…)` spelling did not parse at all. Both are fixed and pinned by
`tests/pkg/variadic_qual/`.

## 15.4 First-class function values

A function name and a lambda both denote a **function value** of a function type
([§5.3.8](03-types.md#538-function-types)), which may be bound, passed, stored,
and called. A lambda's body is a single expression. Closures capture their free
variables by **deep copy at creation**, and an escaping closure's environment is
re-homed into the caller's storage on return
([§13.6](09-expressions.md#136-closures-and-function-values)). Two things cannot
be taken as function values: a **builtin** name (`len`, `push`, `str`, …), and a
function with an **`inout`** parameter (an `inout` borrow is call-scoped and
cannot be deferred through an indirect call). Function values are **not
comparable** (§5.5).

## 15.5 Methods (UFCS)

Any free function may be called in **method position**: `x.f(a, b)` means
`f(x, a, b)` — **unless** the receiver's struct has a function-typed field named
`f`, in which case that stored function value is called instead (the field wins).
Method-call syntax is uniform function-call syntax — Tycho has
no separately-declared methods. For a generic function, the receiver is matched
against the template's first-parameter pattern and the function is then
instantiated ([§7.6](05-generics.md#76-ufcs-and-generic-method-style-calls)).
Method-call chaining (`x.f().g()`) follows from the left-associative postfix
grammar.

## 15.6 Subscripts

A `subscript` ([§4.1.3](02-grammar.md#413-extern-functions-and-subscripts),
[§18](12-aggregates.md)) declares a user-defined projection — a compile-time
place-macro that yields a place rooted in one of its parameters, generalizing the
built-in `&m[k]`. It is called in method position, usable as a place or an
rvalue, and introduces no run-time object and no new aliasing. Its rules (the
yielded place rooted in a parameter, each parameter used at most once) are
specified in §18.
