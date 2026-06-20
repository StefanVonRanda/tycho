# FFI — calling C from Hier

Hier's thesis is value semantics over an implicit arena: Hier code never does
manual memory management. The FFI is the one deliberate **escape hatch** — a
narrow, opt-in boundary for calling existing C libraries (libm, libc, SQLite,
SDL, …). The design keeps that boundary narrow so the thesis stays intact on the
Hier side: no foreign pointer ever leaks into Hier's value-semantic world.

The FFI is honestly an *unsafe* boundary — you can still pass a bad handle to C
and crash inside C. The design's job is to make the **Hier side** sound and to
make the unsafe surface explicit, which is what the `extern` keyword marks. The
README's [FFI section](../README.md#ffi-calling-c) is the short version; this is
the full rule set. A real binding lives in
[`examples/sqlite/`](../examples/sqlite/) (in-memory SQLite through both
compilers, ASan-clean).

## Surface syntax

An `extern fn` is a bodyless declaration whose name *is* the C symbol name,
optionally prefixed with the library to link:

```
extern fn getpid() -> int                                # libc (already linked)
extern fn sqrt(x: float) -> float                        # libm (already linked)
extern "z" fn crc32(crc: int, buf: string, n: int) -> int    # links -lz
extern "SDL2" fn SDL_Init(flags: int) -> int                 # links -lSDL2
extern fn sx_col_text(stmt: ptr, i: int) -> string       # C string in, Hier string out
```

The symbol name is never mangled, even inside a package — a C symbol is global —
so an `extern` can be declared and called from any package. A bare `extern fn`
(no `"Lib"`) assumes the symbol is already linked; libc and libm always are.

## Type mapping

Only scalars, `string`, and the opaque handle `ptr` cross the boundary:

| Hier type | C type      | direction | notes |
|-----------|-------------|-----------|-------|
| `int`     | `long`      | in/out    | zero-cost |
| `char`    | `long`      | in/out    | zero-cost |
| `float`   | `double`    | in/out    | zero-cost |
| `bool`    | `int`       | in/out    | zero-cost |
| `string`  | `char *`    | in: zero-cost; out: arena-copied | see below |
| `ptr`     | `void *`    | in/out    | opaque handle, never dereferenced |
| (none)    | `void`      | out only  | return-less extern |

**The string win.** A Hier `string` is a NUL-terminated `char *`, so a C
function taking `const char *` takes a Hier `string` directly — no wrapper type,
no conversion at the call site. (Languages whose string is a fat pointer
`{ptr, len}` are forced into an explicit `c_str()` conversion; Hier is not.)

**Composites are rejected.** Arrays, maps, structs, `Option`/`Result`, and
tuples have Hier-internal C representations, not a stable C ABI. The type
checker rejects any `extern fn` whose parameter or return type is outside the
table above — it fails closed rather than emit something that would corrupt
memory across the boundary.

## Lifetime & safety

The whole point is that foreign memory never enters Hier's owned world:

- **`string` argument (Hier → C):** the `char *` is passed through unchanged.
  Zero-cost.
- **`string` return (C → hier):** the C-owned bytes are **copied into the
  caller's arena** at the call site, so Hier never holds a pointer into memory C
  controls. A `NULL` return becomes `""` rather than a crash. This is the same
  copy the `getenv` builtin already does — an `extern fn … -> string` is just
  the general case.
  - *Read-once borrow (an optimization, not a semantic change).* When the
    returned string is the direct argument of a read-once consumer —
    `len(f())` or `print(f())` — the copy is skipped and the C pointer is read
    in place (NULL-guarded), because the borrow cannot escape the consuming
    call. Anything that could retain it (binding to a variable, concatenating,
    storing, returning, or comparing two extern strings) keeps the copy. Output
    is byte-identical either way, and the fuzzer proves it.
- **`ptr`:** an opaque `void *`. Hier supports only passing it back to C,
  comparing it (`==`/`!=` against another `ptr` or the `null` literal), and
  `is_null(p)`. No dereference, no arithmetic — Hier shuttles the handle and
  never touches what it points at. A `ptr` rides the scalar paths, so it can
  also be a local, a struct field, or an array element (copied by value).
- **No variadics.** A `printf`-style function needs a fixed-arity C wrapper.
- **No struct-by-value and no callbacks into Hier** — a Hier function value is a
  fat pointer that is not C-ABI, so it cannot cross out.

## Linking

The C reference compiler (`hierc`) links the program for you:

- Each distinct `"Lib"` from an `extern "Lib"` declaration becomes a `-l<Lib>`.
  `-lm` and libc are always passed, so libm/libc externs need no library name.
- CLI passthrough on the `hierc` line: `-L<dir>` / `-I<dir>` (library and
  include paths), `--link <lib>` (a bare `-l` for a library not named in
  source), `--pkg <name>` (`pkg-config --cflags --libs`), and `--shim <file.c>`
  (a companion C file compiled and linked alongside the generated `.c` — the
  `*_shim.c` pattern, as an explicit flag rather than auto-discovery). All
  accumulate onto one `cc` invocation, with libraries trailing the objects that
  need them. `--cc <compiler>` overrides the compiler.
- The self-hosted compiler (`hierc0`) emits C to stdout and does not link, so
  these flags are `hierc`-only; link `hierc0`'s output with your own `cc`,
  passing the same `-l`/`-L`/shim flags.

## Verification

Every piece lands in both compilers, and the staged work is held to the
project's standard bar: `make ffi` (the `tests/ffi/` fixtures — a scalar
round-trip, a string-returning extern, a NULL-return extern, a `ptr` handle
round-trip, a `--shim` build) runs through both compilers, ASan-clean and
output-identical, and `make fixpoint` proves the self-build stays
byte-identical. The SQLite binding in `examples/sqlite/` is the real-world
dogfood: opaque `db`/`stmt` handles, SQL string arguments, arena-copied column
text, `--shim` for the out-parameter API, `--pkg` for linking — against a
library whose returned text pointer is genuinely transient.

---

## Appendix: design history

For contributors and the curious; users need only the rules above.

**Design rationale.** Two choices keep the boundary cheap. First, a Hier
`string` is already a NUL-terminated `char *`, so there is no wrapper type and no
`c_str()` conversion at the call site — a fat-pointer string representation would
have forced one. Second, a returned `string` is always copied into the caller's
arena, and a `NULL` return is coerced to `""`, so a C function that hands back a
transient or null pointer can neither dangle nor crash on the Hier side.

**Staged rollout (all shipped).** The FFI landed in three independently-useful
stages, each in both compilers with the fixpoint staying byte-identical:

1. **Scalars + `string`** — `int`/`char`/`float`/`bool`/`string`/void. Unlocks
   libm, most of libc, and any C library with a scalar/string ABI.
2. **Opaque `ptr`** — `void *` plus the `null` literal and `is_null`. Unlocks
   handle-based libraries (SQLite, SDL, curl).
3. **Linking ergonomics (`hierc`)** — `-L`/`-I`/`--link`/`--pkg`/`--shim`.

**Self-hosting cost.** Each piece exists twice — parse, signature recording,
prototype emission, call lowering, the C type map, and link flags — once in
`hierc` and once in `hierc0`. Externs are emitted only when actually declared,
and the package mangler skips them, so the fixpoint differential stays
byte-identical. Resolved design questions: the keyword is `extern` (reads as
"defined elsewhere", matches C); a bare `extern fn` assumes libc/libm rather
than requiring an explicit `--link`; and a returned `string` is always
arena-copied (the read-once borrow is the only, provably-safe, exception).
