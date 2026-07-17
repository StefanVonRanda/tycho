# FFI — calling C from Tycho

Tycho's model is value semantics over an implicit arena: Tycho code never does
manual memory management. The FFI is the one deliberate **escape hatch** — a
narrow, opt-in boundary for calling existing C libraries (libm, libc, SQLite,
SDL, …). I keep the boundary narrow so the value-semantic model stays intact on
the Tycho side: no foreign pointer ever leaks into Tycho's owned world.

The FFI is an *unsafe* boundary — you can still pass a bad handle to C and crash
inside C. All I'm trying to do is keep the **Tycho side** sound and make the
unsafe surface explicit, which is what the `extern` keyword marks. The
[FFI reference](../reference/ffi.md) is the short version; this page is the
full rule set. A complete binding lives in
[`examples/sqlite/`](../../examples/sqlite/) — in-memory SQLite driven through both
transpilers.

## Quick start

To call a C library you (1) declare each C function you need as an `extern fn`,
(2) call it like any Tycho function, and (3) tell the transpiler which library to
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

The rest of this page covers the declaration syntax, which types can cross the
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
extern fn sx_col_text(stmt: ptr, i: int) -> string       # C string in, Tycho string out
```

The symbol name is never mangled, even inside a package — a C symbol is global —
so an `extern` can be declared and called from any package. A bare `extern fn`
(no `"Lib"`) assumes the symbol is already linked; libc and libm always are.

## Type mapping

Scalars, `string`, `bytes`, and the opaque handle `ptr` cross the boundary:

| Tycho type | C type      | direction | notes |
|-----------|-------------|-----------|-------|
| `int`     | `long`      | in/out    | zero-cost |
| `char`    | `long`      | in/out    | zero-cost |
| `float`   | `double`    | in/out    | zero-cost |
| `bool`    | `int`       | in/out    | zero-cost |
| `string`  | `char *`    | in: zero-cost; out: arena-copied | see below |
| `bytes`   | `(const unsigned char *, long)` in; out-param shim out | binary buffer, length-carried | see below |
| `ptr`     | `void *`    | in/out    | opaque handle, never dereferenced |
| (none)    | `void`      | out only  | return-less extern |

**String passing.** A Tycho `string` is a NUL-terminated `char *`, so a C
function taking `const char *` takes a Tycho `string` directly — no wrapper type,
no conversion at the call site. (Languages whose string is a fat pointer
`{ptr, len}` are forced into an explicit `c_str()` conversion; Tycho is not.)

**Bytes passing.** `bytes` is an immutable binary buffer that, unlike `string`,
crosses the boundary length-carried — so it can hold interior `0x00` and needs no
hex marshaling. Build one with `to_bytes(s)` and read it back with `to_str(b)`;
`len(b)` and `==` work as for strings.

- *A `bytes` **parameter** lowers to two C arguments* `(const unsigned char *ptr,
  long len)`. So `extern fn f(b: bytes)` binds to C `void f(const unsigned char *,
  long)`, and `f(mybytes)` passes the pointer and length. Zero-copy in.
- *A `bytes` **return** uses an out-param shim.* `extern fn f(...) -> bytes` binds
  to C `void f(<args>, unsigned char **out, long *outlen)`. **Convention: the C
  function `malloc`s `*out` and sets `*outlen`; Tycho copies the buffer into the
  caller's arena and then `free`s `*out`.** A `NULL` `*out` becomes empty `bytes`.

**Nullable string return (`-> Option(string)`).** An `extern fn … -> string`
maps a C `NULL` return to `""` (see below). When the difference between "absent"
and "empty" matters, declare the return as `Option(string)` instead: the C symbol
still returns `char*`, but a `NULL` surfaces as `None` and any other pointer as
`Some(<arena-copied>)`. This removes the need for sentinel strings on nullable C
getters. (Only `Option(string)` is supported at the boundary, not arbitrary
`Option(T)`.)

**Out-parameter constructors (`inout` params).** Many C APIs return a status and
write their real result through a pointer out-parameter — `int sqlite3_open(const
char *path, sqlite3 **db)`. Declare the out-parameter `inout` and the transpiler passes
the address of your local automatically; no hand-written shim:

```
extern "sqlite3" fn sqlite3_open(path: string, db: inout ptr) -> int

db := null
rc := sqlite3_open("app.db", &db)     # the compiler emits sqlite3_open(path, &db)
```

A `inout` extern parameter is declared to C as a pointer to its type (`ptr → void**`,
`int → long*`, etc.), so the C function fills it in place and Tycho reads it back by
value. Only `int`/`char`/`float`/`bool`/`ptr` may be `inout` — a `inout string` would be
a `char**` handing Tycho a raw pointer with no length header, and `bytes`/handles/
composites have no trivial pointer-to-self ABI, so all are rejected (use `--shim`).

**Boundary helpers: `to_ptr` and `to_i32`.** Two builtins cover the rough edges of
crossing C's scalar conventions, both in-language (no shim):

- **`to_ptr(n: int) -> ptr`** turns an integer into an opaque pointer, for C APIs
  that use a sentinel pointer constant — e.g. SQLite's `SQLITE_TRANSIENT`, which is
  `(void*)-1`: `sqlite3_bind_text(st, i, s, -1, to_ptr(-1))`. Tycho never
  dereferences the pointer, so this is sound.
- **`to_i32(n: int) -> int`** sign-extends a 32-bit C `int` return. Tycho's `int` is
  64-bit (C `long`), so an `extern fn … -> int` bound to a C function that really
  returns 32-bit `int` reads the correct low 32 bits but the wrong upper bits — a
  returned `-1` shows up as `4294967295`. Wrap the call: `rc := to_i32(c_func())`
  recovers the sign. (Only needed when the C function can return a *negative* `int`;
  non-negative codes and any function returning `long`/`int64` are already correct.
  For a 64-bit row value, prefer the `…_int64` variant of the C call.)

**Other composites are rejected.** Arrays, maps, structs, other `Option`/`Result`,
and tuples have Tycho-internal C representations, not a stable C ABI. The type
checker rejects any `extern fn` whose parameter or return type is outside the
table above (plus the `Option(string)` return form) — it fails closed rather than
emitting something that would corrupt memory across the boundary. To pass aggregate data, marshal it through scalars,
strings, and `ptr` handles, or write a small C shim (see [Linking](#linking)).

## Typed handles (safe-by-default resources)

A raw `ptr` is never freed for you. For a C resource with a clear owner and a
destructor (a `sqlite3*`, a `FILE*`, a zlib stream), declare a **handle** type
instead — the transpiler frees it automatically at the end of the scope that owns
it, so it can't leak or be used after close:

```tycho
handle Db:                                   # Db's C type is void*; freed by its destructor
    free: sqlite3_close                      # a C symbol (declared as an extern fn below)

extern "sqlite3" fn sqlite3_open(path: string) -> Db       # opener -> an owned Db
extern "sqlite3" fn sqlite3_exec(db: Db, sql: string) -> int  # borrow: uses it, does not free
extern "sqlite3" fn sqlite3_close(db: Db) -> int           # the destructor

fn run():
    db := sqlite3_open("x.db")               # owned here
    sqlite3_exec(db, "create table t(x)")    # borrow — db stays usable
    # db is freed here, exactly once: the compiler emits sqlite3_close(db)
```

- **RAII.** The destructor `free:` runs at the owning variable's scope exit —
  every exit (block end, early `return`, `break`/`continue`). Freed exactly once.
- **Borrow on pass.** Passing a handle to a fn/extern passes the `void*`; the
  callee does not free it. Only the owning scope does.
- **Affine, fail-closed.** A handle is opaque (no deref) and can't be copied,
  stored in a container/struct, captured by a closure or `parallel for`,
  reassigned, or returned out of its scope — the type checker rejects each so a
  handle can never be aliased into a double-free or escape its destructor. Only
  an `extern fn` opener may produce one. (These bans are enforced by the
  reference transpiler.)

## Lifetime & safety

The whole point is that foreign memory never enters Tycho's owned world:

- **`string` argument (Tycho → C):** the `char *` is passed through unchanged.
  Zero-cost.
- **`string` return (C → tycho):** the C-owned bytes are **copied into the
  caller's arena** at the call site, so Tycho never holds a pointer into memory C
  controls. A `NULL` return becomes `""` rather than a crash. This is the same
  copy the `getenv` builtin does — an `extern fn … -> string` is the general
  case.
  - *Read-once borrow (an optimization, not a semantic change).* When the
    returned string is the direct argument of a read-once consumer —
    `len(f())`, `print(f())`, or `println(f())` — the copy is skipped and the C pointer is read
    in place (NULL-guarded), because the borrow cannot escape the consuming
    call. Anything that could retain it (binding to a variable, concatenating,
    storing, returning, or comparing two extern strings) keeps the copy. Output
    is byte-identical either way.
- **`ptr`:** an opaque `void *`. Tycho supports only passing it back to C,
  comparing it (`==`/`!=` against another `ptr` or the `null` literal), and
  `is_null(p)`. No dereference, no arithmetic — Tycho shuttles the handle and
  never touches what it points at. A `ptr` rides the scalar paths, so it can
  also be a local, a struct field, or an array element (copied by value).
- **No variadics.** A `printf`-style function needs a fixed-arity C wrapper.
- **No struct-by-value and no callbacks into Tycho** — a Tycho function value is a
  fat pointer that is not C-ABI, so it cannot cross out.

## Threads

The "race-free by construction" guarantee is over Tycho values and **stops at
the FFI**: a C function touching process-global or `static` state is invisible
to the transpiler, so two tasks racing on it race exactly as they would in C.
Isolate such state per thread (thread-local storage, as the `core:crypto` shim
does) or serialize the calls. The full analysis is in
[`rfc/ffi-threading-design-review.md`](../rfc/ffi-threading-design-review.md), and
the concurrency guarantee's scope is in
[the concurrency reference](../reference/concurrency.md#the-safety-envelope).

Handles are affine (see [Typed handles](#typed-handles-safe-by-default-resources)):
they cannot be captured by a closure or `parallel for`, or otherwise cross a
task boundary, so a foreign resource cannot become shared-mutable across threads
by accident. Scalar arguments pass into spawned tasks by value as usual.

## Linking

The C reference transpiler (`tychoc`) links the program for you:

- Each distinct `"Lib"` from an `extern "Lib"` declaration becomes a `-l<Lib>`.
  `-lm` and libc are always passed, so libm/libc externs need no library name.
- CLI passthrough on the `tychoc` line: `-L<dir>` / `-I<dir>` (library and
  include paths), `--link <lib>` (a bare `-l` for a library not named in
  source), `--pkg <name>` (`pkg-config --cflags --libs`), and `--shim <file.c>`
  (a companion C file compiled and linked alongside the generated `.c` — the
  `*_shim.c` pattern, as an explicit flag rather than auto-discovery). All
  accumulate onto one `cc` invocation, with libraries trailing the objects that
  need them. `--cc <compiler>` overrides the compiler.
- The self-hosted transpiler (`tychoc0`) emits C to stdout and doesn't link, so
  these flags are `tychoc`-only; link `tychoc0`'s output with your own `cc`,
  passing the same `-l`/`-L`/shim flags.

A **shim** is the standard way to adapt a C API that the FFI can't express
directly — for example an out-parameter constructor like `sqlite3_open(path,
&db)`. Write a small C wrapper that returns the handle instead, declare the
wrapper as an `extern fn`, and pass the C file with `--shim`.

## Capabilities

What the FFI handles today, in rough order of how much of C it unlocks:

1. **Scalars + `string`** — `int`/`char`/`float`/`bool`/`string`/void. Covers
   libm, most of libc, and any C library with a scalar/string ABI.
2. **Opaque `ptr`** — `void *` plus the `null` literal and `is_null`. Covers
   handle-based libraries (SQLite, SDL, curl).
3. **Linking ergonomics (`tychoc`)** — `-L`/`-I`/`--link`/`--pkg`/`--shim` on the
   `tychoc` command line.

Both transpilers handle every FFI feature (the static handle-misuse bans noted above
are enforced by `tychoc`, the reference). The `tests/ffi/` fixtures (a scalar
round-trip, a string-returning extern, a NULL-return extern, a `ptr` handle
round-trip, and a `--shim` build) run through both transpilers under `make ffi`,
ASan-clean and output-identical.

A worked binding lives in [`examples/sqlite/`](../../examples/sqlite/): opaque
`db`/`stmt` handles, SQL string arguments, arena-copied column text, `--shim`
for the out-parameter API, and `--pkg` for linking — against a library whose
returned text pointer is genuinely transient.

## Limitations (by design)

These are deliberate boundaries, not stuff I haven't gotten to yet:

- **No composite types across the boundary.** Arrays, maps, structs,
  `Option`/`Result`, and tuples are rejected; they have no stable C ABI. Marshal
  through scalars, strings, and `ptr`, or use a shim.
- **No struct-by-value.** Pass an opaque `ptr` handle instead.
- **No variadics.** Wrap variadic C functions (like `printf`) in a fixed-arity C
  shim.
- **No callbacks into Tycho.** A Tycho function value is a fat pointer that is not
  C-ABI, so C cannot call back into Tycho.
- **No automatic binding generation.** Each C function you use is declared by
  hand as an `extern fn`.
- **`ptr` is opaque.** No dereference and no pointer arithmetic on the Tycho side
  — Tycho only shuttles handles back to C.

These keep the boundary narrow on purpose: anything richer would either break
value semantics on the Tycho side or lean on an unstable C ABI.

## Design notes

Two choices keep the boundary cheap. First, a Tycho `string` is already a
NUL-terminated `char *`, so there is no wrapper type and no `c_str()` conversion
at the call site — a fat-pointer string representation would have forced one.
Second, a returned `string` is always copied into the caller's arena, and a
`NULL` return is coerced to `""`, so a C function that hands back a transient or
null pointer can neither dangle nor crash on the Tycho side.

The keyword is `extern` because it reads as "defined elsewhere" and matches C; a
bare `extern fn` assumes libc/libm rather than requiring an explicit `--link`;
and a returned `string` is always arena-copied, with the read-once borrow as the
single, provably-safe exception.
