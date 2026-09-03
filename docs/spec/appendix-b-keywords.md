# Appendix B — Keywords and contextual identifiers

See [§3.6](01-lexical.md#36-keywords) and [§3.7](01-lexical.md#37-contextual-identifiers)
for the normative definitions.

## B.1 Reserved words

These 42 words are reserved; none may be used as an identifier.

```text
and     bool    break   bytes   continue elif    else    enum
f32     false   float   fn      for      handle  i8      i16
i32     i64     if      in      inout    int     is      match
not     null    or      or_return parallel ptr    return  select
soa     spawn   string  struct  true     type    u8      u16
u32     u64
```

41 of the 42 are **lexer** keywords: the set is exactly `keyword()`
(`src/tychoc.c@keyword`), which never hands back an identifier token for one.
`soa` is the exception — it lexes as an ordinary identifier and is reserved by
the **parser**, which requires a `[` after it in every expression position
(`src/tychoc.c:2931-2934`). The difference is invisible to a program and shows
only in the diagnostic: `soa := 5` is accepted at its declaration, and the next
mention of the name dies with `expected '[' after soa`, so the binding is
declarable and permanently unusable. It is listed here, with the reserved words,
because that is what it costs a program — not under B.2, where it sat until
2026-09-02 under a heading promising the opposite.

`is` is the variant test, for an enum, an `Option` or a `Result`
([§19.8](12-aggregates.md#198-is--the-variant-test)).
It is **reserved**, not contextual: no `.ty` file in this tree used `is` as an
identifier when it was added, so reserving it broke nothing, and an operator
keyword shares its position with an ordinary name (`x is y`) in a way that
`pass`, a whole statement, does not.

Of these, the **type keywords** are `int`, `float`, `bool`, `string`, `ptr`,
`bytes`, the fixed-width integers `u8`, `u16`, `u32`, `u64`, `i8`, `i16`, `i32`,
`i64`, and `f32`. There is no `while`, `char`, or `void` keyword.

## B.2 Contextual identifiers

Significant only in a specific position; ordinary identifiers elsewhere (a
variable of the same name is unaffected).

| Word | Significant position |
|---|---|
| `package`, `import`, `extern`, `const`, `subscript` | top-level declaration leader |
| `const`, `delete` | statement leader (when followed by an identifier) |
| `where`, `channel` | type / expression position. Both are genuinely unaffected as names: `where := 5` and `channel := 5` compile and print (measured 2026-09-02) |
| `Option`, `Result`, `Channel` | type / expression position. These are not reserved either, but §3.5.1 bars **every** uppercase name from a run-time binding position, so `Option := 5` is refused for the same reason `Foo := 5` is; they remain legal as a user's own type name, which shadows the builtin |
| `void` | a `Result`'s ok payload, and nowhere else ([§5.3.6](03-types.md#536-enums-option-result)). Not a primitive type keyword: `int`/`float`/`bool`/`string`/… are lexed as keywords, `void` is an ordinary identifier the type parser recognises in that one slot, so a variable named `void` is still legal |
| `pass` | a whole statement, and only then — the no-op ([§14.1](10-statements.md#141-blocks)). `pass := 0` and `pass = pass + 1` stay ordinary code, which is why it is not reserved: two files in this tree already used the name |
| `sink` | parameter modifier |
| `yield` | `subscript` body |
| `free` | `handle` body |
| `range` | `for … in` head — recognised **only to refuse it** since 2026-07-29 (§14.4) |
| `None`, `Some`, `Ok`, `Err` | value constructors |
| `_` | `match` wildcard pattern |
| all builtin names (`len`, `push`, `str`, `wait`, …) | resolved as calls; never reserved ([§29](16-builtins.md)) |
