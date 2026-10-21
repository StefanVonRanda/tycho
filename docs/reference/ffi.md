# FFI (calling C)

> **Memory:** A C string returned across the boundary is copied into the caller's
> arena at the call site; opaque handles (`ptr`) are passed through but never
> dereferenced. The boundary stays narrow so Tycho values stay independent of C
> memory.

`extern fn` declares a C function Tycho can call. It is bodyless, bound to a direct C symbol,
and can optionally name the library to link.

```tycho
extern fn rand() -> i32                              # libc: C `int rand(void)`
extern "m" fn cos(x: float) -> float                 # links -lm
extern fn sx_col_text(stmt: ptr, i: int) -> string   # C string in, Tycho string out
extern fn crc32(data: bytes, len: u32) -> u32        # sized ints: real uint32_t at the C ABI
```

**The declared signature must match the C header.** Tycho `int` is 64 bits, so
`extern fn getpid() -> int` does not match `unistd.h`'s 32-bit `pid_t` return
and the emitted C will not compile. Declare the width the header actually uses
(`i32`, `u32`, …) when it is not 64-bit. A C `long` has no Tycho spelling: its
width varies by platform. Prefer a C API whose header type is fixed everywhere.

## The boundary

Rule: only plain data crosses. Anything with a Tycho-internal layout is
rejected at compile time rather than corrupted at run time.

- **Scalars** — `int`, `char`, `float`, `bool`.
- **Sized integers** — `u8`, `u16`, `i8`, `i16`, `i32`, `i64`, `u32`, and `u64`
  are distinct numeric types. In an `extern` signature they emit the matching
  fixed-width C type, so `extern fn f(x: u8) -> i16` matches
  `int16_t f(uint8_t)`. Values of these types are produced explicitly with
  `to_u8`, `to_i16`, and the other `to_` conversions; there is no implicit
  conversion between a sized integer and `int` (use `to_int` or the
  corresponding `to_` conversion). Signed returns keep their signed type, and
  unsigned returns keep theirs; the compiler does not widen them to `int`.
- **`string`** — passed as a C `char*`; a C-returned string is **copied into the
  caller's arena** at the call site, so Tycho never holds a pointer into C-owned
  memory. A nullable C return is declared `-> Option(string)`. **The pointer
  must still be valid when the C function returns** — the copy happens after it
  does, so returning a pointer into something freed first is a use-after-free
  that a normal build will not catch. **The copy is all the call site does — it
  never frees the pointer**, so a `malloc`ed return leaks silently. Return a
  buffer that outlives the call instead, or hold the pointer in a shim and free
  it on the next call.
- **`bytes`** — a binary buffer (interior NULs intact), crossing as a `(pointer,
  length)` pair. A `bytes` return uses an out-param shim: the C function
  allocates the buffer and Tycho copies it into the caller's arena, then frees
  the C buffer. A `NULL` return becomes empty `bytes`. Unlike `string`, a
  `bytes` return never leaks: the two directions have opposite ownership, so do
  not reason about them together.
- **`[int]` and `[float]`** — a scalar array crosses as a `(pointer, length)`
  pair (like `bytes`); an array of any other element type does not.
- **`ptr`** — an opaque foreign handle, a `void*` Tycho never dereferences. The
  `null` literal and `is_null(p)` work on it. `is_null` accepts a `handle` too,
  which is how to check whether an opener succeeded; the scope-exit free is
  null-guarded, so a failed open is safe to let fall out of scope.
- **typed `handle`s** — a `handle Name:` header over an indented `free: c_fn`
  line declares a `void*` whose C destructor runs automatically at scope exit,
  so a foreign resource cannot leak or be used after close. A handle has exactly
  one owner: it cannot be copied, reassigned, stored in an array, map, struct,
  tuple, `Option` or `Result`, captured by a closure or `parallel for`, or
  returned from a Tycho function. An `extern` opener is the only thing that may
  return one. Passing a handle **borrows** it — the callee does not free it.
  `close(h)` runs the destructor early on a handle **variable** and nulls it
  (never on a handle parameter: the callee's copy is a borrow, so closing it is
  a compile error); the scope-exit free is null-guarded, so the destructor runs
  exactly once either way. After a `close(h)` statement, any later mention of
  `h` in the same block (a second `close`, `is_null(h)` included) is a
  **compile error**. A `close` inside an `if` or loop reaches only the end of
  that block, so a use after it compiles: it passes null to C if the branch ran,
  a logic bug, not memory corruption.
- **POD structs** — a non-packed, non-aligned struct of numeric scalar fields
  may cross **by value**, as a **return**, and as an **`inout`** pointer. A
  dynamic array of one (`[T]`) crosses as a `(const T*, length)` parameter,
  borrowed for the call. Use fixed-width fields (`i32`, `u8`, `f32`) to match
  the C header; `int` is 64-bit and `float` is C `double`.
- **`inout` out-parameters** — a numeric scalar, `ptr`, or a **POD struct**. The
  C function gets a pointer to the struct. A `string`, `bytes`, handle, map,
  array or non-POD struct `inout` is **rejected**.

Maps, non-POD structs, and non-scalar arrays are **rejected** at the boundary —
fail closed. (The scalar arrays `[int]`/`[float]` and arrays of POD structs do
cross, above.)

## Threads

The "race-free by construction" guarantee is over Tycho values and does **not**
cross the FFI. A C function touching shared state is invisible to the compiler,
so isolate such state per thread or serialize the calls. See
[Concurrency](concurrency.md#the-safety-envelope).

## Linking

Ergonomics live on the `tychoc` command line: `--link` (a raw linker flag),
`--pkg` (pkg-config), and `--shim` (compile a companion `.c` alongside). For a
complete worked binding over a real library, in-memory SQLite is at
[`examples/sqlite/`](../../examples/sqlite) — note that it drives the C API
through raw `ptr`, not through a `handle`. For a worked **handle** paired with
`--shim`, see [`tools/tycho-du/`](../../tools/tycho-du);
[`tools/tycho-fh/`](../../tools/tycho-fh) declares one over a hand-built static
library.

---

## Working with it

Tycho code never manages memory by hand. The FFI is the deliberate escape hatch
for calling existing C libraries (libm, libc, SQLite, …), kept narrow so the
value-semantic model stays intact on the Tycho side: no foreign pointer ever
leaks into Tycho's owned world.

The FFI is an *unsafe* boundary: the `extern` keyword marks exactly where
safety becomes the caller's job. A bad handle passed to C crashes inside C; C
code with shared state races across tasks; a freed pointer handed back is a
use-after-free; a `malloc`ed string return leaks. The rules below keep the
Tycho side sound; what C does with what it is given is still on the caller.

### Quick start

To call a C library you (1) declare each C function you need as an `extern fn`,
(2) call it like any Tycho function, and (3) tell the transpiler which library to
link. Calling `cbrt` from libm needs no link flags, because libm is always
linked:

```tycho
extern fn cbrt(x: float) -> float

fn main():
    println(str(cbrt(27.0)))    # 3.0
```

For a library that isn't linked by default, name it in the declaration. This
declares `crc32` from zlib and links `-lz` automatically:

```tycho
extern "z" fn crc32(crc: int, buf: string, n: int) -> int
```

The rest of this page covers the declaration syntax, which types can cross the
boundary, the lifetime and safety rules, how linking works, and the by-design
limits.

### Surface syntax

An `extern fn` is a bodyless declaration whose name *is* the C symbol name,
optionally prefixed with the library to link:

<!-- fence-skip: a signature showcase with no main, naming SDL2 and a SQLite shim this host has no symbol for -->
```tycho
extern fn rand() -> i32                                  # libc (already linked)
extern fn hypot(x: float, y: float) -> float             # libm (already linked)
extern "z" fn crc32(crc: int, buf: string, n: int) -> int    # links -lz
extern "SDL2" fn SDL_Init(flags: int) -> int                 # links -lSDL2
extern fn sx_col_text(stmt: ptr, i: int) -> string       # C string in, Tycho string out
```

The symbol name is never mangled, even inside a package — a C symbol is global —
so an `extern` can be declared and called from any package. A bare `extern fn`
(no `"Lib"`) assumes the symbol is already linked; libc and libm always are.

A symbol the language already provides cannot be re-declared. `extern fn
sqrt(x: float) -> float` does not compile: `sqrt` is a builtin, so it fails with
`'sqrt' is already defined`.

### Type mapping

Scalars, `string`, `bytes`, and the opaque handle `ptr` cross the boundary:

| Tycho type | C type      | direction | notes |
|-----------|-------------|-----------|-------|
| `int`     | `int64_t`   | in/out    | zero-cost |
| `char`    | `int64_t`   | in/out    | zero-cost |
| `float`   | `double`    | in/out    | zero-cost |
| `bool`    | `int`       | in/out    | zero-cost |
| `string`  | `char *`    | in: zero-cost; out: arena-copied | see below |
| `bytes`   | `(const unsigned char *, int64_t)` in; out-param shim out | binary buffer, length-carried | see below |
| `ptr`     | `void *`    | in/out    | opaque handle, never dereferenced |
| (none)    | `void`      | out only  | return-less extern |

**The integer column is `int64_t`, not `long`.** Writing `long` in a C prototype
is right only where `long` is 64 bits and silently truncates where it is 32.
Nothing catches the mismatch: the emitted prototype lives in a different
translation unit from the shim, so C never compares the two.

**Shims spell the integer `int64_t`.** The generated program declares each
`extern fn` for the caller; the shim only defines a function of the same shape.
`#include <stdint.h>` and write `int64_t` — no Tycho-specific typedef, guard, or
header is required.

**String passing.** A Tycho `string` is a NUL-terminated `char *`, so a C
function taking `const char *` takes a Tycho `string` directly — no wrapper
type, no conversion at the call site.

**Bytes passing.** `bytes` is an immutable binary buffer that, unlike `string`,
crosses the boundary length-carried — so it can hold interior `0x00` and needs no
hex marshaling. Build one with `to_bytes(s)` and read it back with `to_str(b)`.
`len(b)`, `==`, `b[i]` (the byte as an `int`), `b[i:j]` (a sub-buffer) and `+`
(with `bytes` or a `char`) all work exactly as for strings
([spec §5.2.6](../spec/03-types.md#526-bytes)). What it does **not** do is mix
implicitly with `string`: `b + "s"` is a type error, by design.

- *A `bytes` **parameter** crosses as a pointer and a length.* So `extern fn
  f(b: bytes)` binds to C `void f(const unsigned char *, int64_t)`, and
  `f(mybytes)` passes the pointer and length.
- *A `bytes` **return** uses an out-param shim.* `extern fn f(...) -> bytes`
  binds to C `void f(<args>, unsigned char **out, int64_t *outlen)`.
  **Convention: the C function `malloc`s `*out` and sets `*outlen`; Tycho copies
  the buffer into the caller's arena and then `free`s `*out`.** A `NULL` `*out`
  becomes empty `bytes`.

**Nullable string return (`-> Option(string)`).** An `extern fn … -> string`
maps a C `NULL` return to `""` (see below). When the difference between "absent"
and "empty" matters, declare the return as `Option(string)` instead: the C symbol
still returns `char*`, but a `NULL` surfaces as `None` and any other pointer as
`Some(<arena-copied>)`. (Only `Option(string)` is supported at the boundary, not
arbitrary `Option(T)`.)

**Out-parameter constructors (`inout` params).** Many C APIs return a status and
write their real result through a pointer out-parameter — `int sqlite3_open(const
char *path, sqlite3 **db)`. Declare the C `int` status as `i32` and the
out-parameter `inout`; the transpiler passes
the address of the local automatically; no hand-written shim:

```tycho
extern "sqlite3" fn sqlite3_open(path: string, db: inout ptr) -> i32

db := null
rc := sqlite3_open("app.db", &db)      # the compiler emits sqlite3_open(path, &db)
println(str(rc))
```

An `inout` extern parameter is declared to C as a pointer to its type (`ptr →
void**`, `int → int64_t*`, a struct → `S_Name *`), so the C function fills it in
place and Tycho reads it back by value. A **plain struct of scalar fields** is
allowed when its layout is the C layout. Use fixed-width fields for C widths —
`i32` for C `int`, `u8` for `uint8_t`, `f32` for C `float`; Tycho `int` is 64-bit
and `float` is C `double`. `packed`/`align` structs, and structs containing
strings, arrays or maps, are rejected: use `--shim` for those. A `inout string`
would hand Tycho a raw pointer with no length header; `bytes`/handles/maps/arrays
have no trivial pointer-to-self form, so all are rejected.

POSIX/LP64 example (`timeval`'s fields are 64-bit there):

```tycho
struct Timeval:
    sec: i64
    usec: i64

extern fn gettimeofday(tv: inout Timeval, tz: ptr) -> i32

fn main():
    tv := Timeval(0, 0)
    rc := gettimeofday(&tv, null)      # C gets S_Timeval * and fills the fields
    println("rc=" + str(rc) + " sec_positive=" + str(tv.sec > 0))
```

The same POD struct may also be passed and returned **by value**; a
`[Timeval]` parameter crosses as `(const S_Timeval *, length)`, borrowed for the
call.

**Boundary helpers: `to_ptr` and `to_i32`.** Two builtins cover the rough edges of
crossing C's scalar conventions, both in-language (no shim):

- **`to_ptr(n: int) -> ptr`** turns an integer into an opaque pointer, for C APIs
  that use a sentinel pointer constant — e.g. SQLite's `SQLITE_TRANSIENT`, which
  is `(void*)-1`: `sqlite3_bind_text(st, i, s, -1, to_ptr(-1))`. Tycho never
  dereferences the pointer, so this is sound.
- **`to_i32(n: int) -> i32`** explicitly narrows a Tycho `int` to signed 32 bits.
  Declare a C `int` return as `-> i32` so the emitted prototype matches the C
  ABI; convert it to Tycho's 64-bit `int` with `to_int` if needed. Likewise, use
  `-> int` only for a C 64-bit integer return. A C `long` has
  platform-dependent width, so prefer an explicitly sized C API.

**Other composites are rejected.** Arrays of non-POD elements, maps, non-POD
structs, other `Option`/`Result`, and tuples have Tycho-internal C
representations, not a stable C ABI. The exceptions are POD structs by value, by
return, and as `inout`, plus a dynamic `[POD struct]` parameter. The type checker
rejects any other `extern fn` parameter or return outside the table above (plus
the `Option(string)` return form) — it fails closed rather than emitting
something that would corrupt memory across the boundary. To pass other aggregate
data, marshal it through scalars, strings, and `ptr` handles, use a POD struct,
or write a small C shim (see [Linking](#linking)).

**C unions are not crossable, by design.** Keep the union in C and return a
tagged POD struct: `examples/gfx` decodes `SDL_Event` into
`Event { kind, key, x, y, button }`, so no layout-dependent union reaches Tycho.
A language-level union would need initialization, active-member and ownership
rules; it is not an FFI shortcut.

### Typed handles (safe-by-default resources)

A raw `ptr` is never freed for the caller. For a C resource with a clear owner
and a destructor (a `sqlite3*`, a `FILE*`, a zlib stream), declare a **handle**
type instead — the transpiler frees it automatically at the end of the scope
that owns it, so it cannot leak or be used after close:

<!-- fence-skip: extern block against a real libsqlite3, and no main; compiling it needs --link sqlite3 -->
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
  every way out of *that* scope (block end, early `return`, `break`/`continue`,
  `or_return`). A `continue` frees a handle declared inside the loop body, not
  one owned by a scope around the loop. Freed exactly once.
- **Borrow on pass.** Passing a handle to a fn/extern passes the `void*`; the
  callee does not free it. Only the owning scope does.
- **Affine, fail-closed.** A handle is opaque (no deref) and cannot be copied,
  stored in a container/struct, captured by a closure or `parallel for`,
  reassigned, or returned out of its scope — the type checker rejects each so a
  handle can never be aliased into a double-free or escape its destructor. Only
  an `extern fn` opener may produce one.

### Lifetime & safety

The rule behind every item here: foreign memory never enters Tycho's owned
world.

- **`string` argument (Tycho → C):** the `char *` is passed through unchanged.
- **`string` return (C → Tycho):** the C-owned bytes are **copied into the
  caller's arena** at the call site, so Tycho never holds a pointer into memory
  C controls. A `NULL` return becomes `""` rather than a crash. When the
  returned string is consumed immediately (`len(f())`, `print(f())`), the copy
  may be skipped with byte-identical output; anything that could retain it keeps
  the copy.
- **`ptr`:** an opaque `void *`. Tycho supports only passing it back to C,
  comparing it (`==`/`!=` against another `ptr` or the `null` literal), and
  `is_null(p)`. No dereference, no arithmetic — Tycho shuttles the handle and
  never touches what it points at. A `ptr` rides the scalar paths, so it can
  also be a local, a struct field, or an array element (copied by value).
- **No variadics.** A `printf`-style function needs a fixed-arity C wrapper.
- **No struct-by-value and no callbacks into Tycho** — a Tycho function value
  cannot cross out.

What can still go wrong, plainly: passing a bad handle crashes inside C, not in
Tycho; C code with shared state races across tasks (see [Threads](#threads));
a pointer freed before the C function returns is a use-after-free no normal
build catches; a `malloc`ed string return leaks, while a `bytes` return is
freed. The fix in each case is on the C side of the boundary: valid handles,
per-thread or serialized state, a buffer that outlives the call, and the
`bytes` convention for allocated returns.

### Threads

The "race-free by construction" guarantee is over Tycho values and **stops at
the FFI**: a C function touching shared state is invisible to the transpiler,
so two tasks racing on it race exactly as they would in C. Isolate such state
per thread or serialize the calls. The full analysis is in
[`rfc/ffi-threading-design-review.md`](../rfc/ffi-threading-design-review.md), and
the concurrency guarantee's scope is in
[the concurrency reference](../reference/concurrency.md#the-safety-envelope).

Handles are affine (see [Typed handles](#typed-handles-safe-by-default-resources)):
they cannot be captured by a closure or `parallel for`, or otherwise cross a
task boundary, so a foreign resource cannot become shared across threads by
accident. Scalar arguments pass into spawned tasks by value as usual.

### Linking

The C reference transpiler (`tychoc`) links the program for the caller:

A **shim** is the standard way to adapt a C API that the FFI cannot express
directly — for example an out-parameter constructor like `sqlite3_open(path,
&db)`. Write a small C wrapper that returns the handle instead, declare the
wrapper as an `extern fn`, and pass the C file with `--shim`.

The flags, all on the `tychoc` command line (`tychoc --help` groups them the same way):

| Flag | Does |
| --- | --- |
| `--link <lib>` | a bare `-l<lib>` for a library the source does not name |
| `--pkg <name>` | add `pkg-config --cflags --libs <name>` |
| `--shim <file.c>` | compile and link a companion C file |
| `-L<dir>`, `-I<dir>` | library and include search paths |
| `--cc <compiler>` | use another C compiler |
| `--print-shims` | print the `<pkg>_shim.c` files the program needs, transitively, and exit: what a hand-written `cc` line must pass |
| `--print-deps` | print the pkg-config names it needs, transitively, and exit |

A C function that can return `NULL` for a string is declared `-> Option(string)`; see
[nullable string return](#type-mapping).

### Capabilities

What the FFI handles today, in rough order of how much of C it unlocks:

1. **Scalars + `string`** — `int`/`char`/`float`/`bool`/`string`/void. Covers
   libm, most of libc, and any C library with a scalar/string ABI.
2. **Opaque `ptr`** — `void *` plus the `null` literal and `is_null`. Covers
   handle-based libraries (SQLite, SDL, curl).
3. **Linking ergonomics (`tychoc`)** — `-L`/`-I`/`--link`/`--pkg`/`--shim` on the
   `tychoc` command line.

A worked scalar/string binding is in [`examples/sqlite/`](../../examples/sqlite/):
opaque pointers, SQL string arguments, and `--shim` for the out-parameter API.
The worked `handle` is [`tools/tycho-du/`](../../tools/tycho-du), built with
`--shim`; [`tools/tycho-fh/`](../../tools/tycho-fh) builds one over a static
library instead.

### Limitations (by design)

These limits are deliberate:

- **No composite types across the boundary.** Arrays, maps, structs,
  `Option`/`Result`, and tuples are rejected; they have no stable C ABI. Marshal
  through scalars, strings, and `ptr`, or use a shim.
- **No struct-by-value.** Pass an opaque `ptr` handle instead.
- **No variadics.** Wrap variadic C functions (like `printf`) in a fixed-arity C
  shim.
- **No callbacks into Tycho.** A Tycho function value cannot cross out, so C
  cannot call back into Tycho.
- **No automatic binding generation.** Each C function used is declared by hand
  as an `extern fn`.
- **`ptr` is opaque.** No dereference and no pointer arithmetic on the Tycho side
  — Tycho only shuttles handles back to C.

These keep the boundary narrow on purpose: anything richer would either break
value semantics on the Tycho side or lean on an unstable C ABI.

---

Why the boundary is shaped this way — the string representation, the arena
copy, and the `extern` keyword — is in [design-ffi](../internals/design-ffi.md),
for maintainers.
