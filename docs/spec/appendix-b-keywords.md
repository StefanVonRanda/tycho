# Appendix B — Keywords and contextual identifiers

See [§3.6](01-lexical.md#36-keywords) and [§3.7](01-lexical.md#37-contextual-identifiers)
for the normative definitions.

## B.1 Reserved words

These 34 words are reserved; none may be used as an identifier.

```
and     bool    break   bytes   continue elif    else    enum
f32     false   float   fn      for      handle  if      in
inout   int     match   not     null     or      or_return  parallel
ptr     return  select  spawn   string   struct  true    type
u32     u64
```

Of these, the **type keywords** are `int`, `float`, `bool`, `string`, `ptr`,
`bytes`, `u32`, `u64`, `f32`. There is no `while`, `char`, or `void` keyword.

## B.2 Contextual identifiers

Significant only in a specific position; ordinary identifiers elsewhere (a
variable of the same name is unaffected).

| Word | Significant position |
|---|---|
| `package`, `import`, `extern`, `const`, `subscript` | top-level declaration leader |
| `const`, `delete` | statement leader (when followed by an identifier) |
| `soa`, `where`, `channel`, `Option`, `Result`, `Channel` | type / expression position |
| `sink` | parameter modifier |
| `yield` | `subscript` body |
| `free` | `handle` body |
| `range` | `for … in` head |
| `None`, `Some`, `Ok`, `Err` | value constructors |
| `_` | `match` wildcard pattern |
| all builtin names (`len`, `push`, `str`, `wait`, …) | resolved as calls; never reserved ([§29](16-builtins.md)) |
