# FFI — calling C from Hier

Hier's model is value semantics over an implicit arena: Hier code never does
manual memory management. The FFI is the one deliberate **escape hatch** — a
narrow, opt-in boundary for calling existing C libraries (libm, libc, SQLite,
SDL, …). The boundary is kept narrow so the value-semantic model stays intact on
the Hier side: no foreign pointer ever leaks into Hier's owned world.

The FFI is an *unsafe* boundary — you can still pass a bad handle to C and crash
inside C. The design's job is to keep the **Hier side** sound and to make the
unsafe surface explicit, which is what the `extern` keyword marks. The README's
[FFI section](../README.md#ffi-calling-c) is the short version; this page is the
full rule set. A complete binding lives in
[`examples/sqlite/`](../examples/sqlite/) — in-memory SQLite driven through both
compilers.

## Quick start

To call a C library you (1) declare each C function you need as an `extern fn`,
(2) call it like any Hier function, and (3) tell the compiler which library to
link. Calling `cbrt` from libm needs no link flags, because libm is always
linked:

```
extern fn cbrt(x: float) -> float

fn main():
    println(str(cbrt(27.0)))    # 3.0
```

For a library that isn't linked by default, name it in the declaration. This
declares `crc32` from zlib and links `-lz` automatically:

```
extern "z" fn crc32(crc: int, buf: string, n: int) -> int
```

The rest of this page covers the declaration syntax, which types may cross the
boundary, the lifetime and safety rules, how linking works, and the by-design
limits.

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
memory across the boundary. To pass aggregate data, marshal it through scalars,
strings, and `ptr` handles, or write a small C shim (see [Linking](#linking)).

## Lifetime & safety

The whole point is that foreign memory never enters Hier's owned world:

- **`string` argument (Hier → C):** the `char *` is passed through unchanged.
  Zero-cost.
- **`string` return (C → hier):** the C-owned bytes are **copied into the
  caller's arena** at the call site, so Hier never holds a pointer into memory C
  controls. A `NULL` return becomes `""` rather than a crash. This is the same
  copy the `getenv` builtin does — an `extern fn … -> string` is the general
  case.
  - *Read-once borrow (an optimization, not a semantic change).* When the
    returned string is the direct argument of a read-once consumer —
    `len(f())` or `print(f())` — the copy is skipped and the C pointer is read
    in place (NULL-guarded), because the borrow cannot escape the consuming
    call. Anything that could retain it (binding to a variable, concatenating,
    storing, returning, or comparing two extern strings) keeps the copy. Output
    is byte-identical either way.
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

A **shim** is the standard way to adapt a C API that the FFI can't express
directly — for example an out-parameter constructor like `sqlite3_open(path,
&db)`. Write a small C wrapper that returns the handle instead, declare the
wrapper as an `extern fn`, and pass the C file with `--shim`.

## Capabilities

What the FFI supports today, in rough order of how much of C it unlocks:

1. **Scalars + `string`** — `int`/`char`/`float`/`bool`/`string`/void. Covers
   libm, most of libc, and any C library with a scalar/string ABI.
2. **Opaque `ptr`** — `void *` plus the `null` literal and `is_null`. Covers
   handle-based libraries (SQLite, SDL, curl).
3. **Linking ergonomics (`hierc`)** — `-L`/`-I`/`--link`/`--pkg`/`--shim` on the
   `hierc` command line.

Every feature exists in both compilers. The `tests/ffi/` fixtures (a scalar
round-trip, a string-returning extern, a NULL-return extern, a `ptr` handle
round-trip, and a `--shim` build) run through both compilers under `make ffi`,
ASan-clean and output-identical.

A worked binding lives in [`examples/sqlite/`](../examples/sqlite/): opaque
`db`/`stmt` handles, SQL string arguments, arena-copied column text, `--shim`
for the out-parameter API, and `--pkg` for linking — against a library whose
returned text pointer is genuinely transient.

## Limitations (by design)

These are deliberate boundaries, not missing work:

- **No composite types across the boundary.** Arrays, maps, structs,
  `Option`/`Result`, and tuples are rejected; they have no stable C ABI. Marshal
  through scalars, strings, and `ptr`, or use a shim.
- **No struct-by-value.** Pass an opaque `ptr` handle instead.
- **No variadics.** Wrap variadic C functions (like `printf`) in a fixed-arity C
  shim.
- **No callbacks into Hier.** A Hier function value is a fat pointer that is not
  C-ABI, so C cannot call back into Hier.
- **No automatic binding generation.** Each C function you use is declared by
  hand as an `extern fn`.
- **`ptr` is opaque.** No dereference and no pointer arithmetic on the Hier side
  — Hier only shuttles handles back to C.

These keep the boundary narrow on purpose: anything richer would either break
value semantics on the Hier side or depend on an unstable C ABI.

## Design notes

Two choices keep the boundary cheap. First, a Hier `string` is already a
NUL-terminated `char *`, so there is no wrapper type and no `c_str()` conversion
at the call site — a fat-pointer string representation would have forced one.
Second, a returned `string` is always copied into the caller's arena, and a
`NULL` return is coerced to `""`, so a C function that hands back a transient or
null pointer can neither dangle nor crash on the Hier side.

The keyword is `extern` because it reads as "defined elsewhere" and matches C; a
bare `extern fn` assumes libc/libm rather than requiring an explicit `--link`;
and a returned `string` is always arena-copied, with the read-once borrow as the
single, provably-safe exception.
