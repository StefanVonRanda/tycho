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
extern "z" fn zlibVersion() -> string                # links -lz; C string out, arena-copied
```

**The declared signature must match the C header.** Tycho `int` is 64 bits, so
`extern fn getpid() -> int` does not match `unistd.h`'s 32-bit `pid_t` return
and the emitted C will not compile. Declare the width the header actually uses
(`i32`, `u32`, …) when it is not 64-bit. A C `long` has no Tycho spelling: its
width varies by platform. Prefer a C API whose header type is fixed everywhere.

## The boundary

Rule: only plain data crosses. Anything with a Tycho-internal layout is
rejected at compile time rather than corrupted at run time.

- **Scalars** — `int`, `float`, `bool`.
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
  pair (like `bytes`). As a parameter, so does an array of a sized scalar —
  `[f32]` arrives as `const float *` and `[u32]` as `const uint32_t *`, no
  conversion — which is how to hand a GPU or audio API its data.
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
  a logic bug, not memory corruption. `exit()` and `die()` do not run scope-exit
  destructors; close the handle explicitly before either. A failed opener returns
  a null handle (`is_null` checks it); an `inout int status` out-param or a
  separate error function reports why.
- **POD structs** — a non-packed, non-aligned struct of numeric scalar fields
  may cross **by value**, as a **return**, and as an **`inout`** pointer. A
  dynamic array of one (`[T]`) crosses as a `(const T*, length)` parameter,
  borrowed for the call. Use fixed-width fields (`i32`, `u8`, `f32`) to match
  the C header; `int` is 64-bit and `float` is C `double`.
- **`[string]` parameter** — crosses as a `(const char *const *, length)` pair,
  borrowed for the call. It is not `NULL`-terminated and carries no per-element
  length; a return is rejected.
- **`inout` out-parameters** — a numeric scalar, `ptr`, or a **POD struct**. The
  C function gets a pointer to the struct. A `string`, `bytes`, handle, map,
  array or non-POD struct `inout` is **rejected**.

Maps, non-POD structs, and other non-scalar arrays are **rejected** at the
boundary — fail closed. (The scalar arrays `[int]`/`[float]`, parameter arrays of
sized scalars and of POD structs, and `[string]` parameters do cross, above.)

## Threads

The "race-free by construction" guarantee is over Tycho values and does **not**
cross the FFI. A C function touching shared state is invisible to the compiler,
so two tasks calling it race exactly as two C threads would: isolate such state
per thread or serialize the calls. Handles cannot be captured by a closure or
`parallel for`, so a foreign resource cannot become shared by accident. See
[Concurrency](concurrency.md#the-safety-envelope) and
[`rfc/ffi-threading-design-review.md`](../rfc/ffi-threading-design-review.md).

## Linking

The flags, all on the `tychoc` command line (`tychoc --help` groups them the same way):

| Flag | Does |
| --- | --- |
| `--link <lib>` | a bare `-l<lib>` for a library the source does not name |
| `--pkg <name>` | add `pkg-config --cflags --libs <name>` |
| `--shim <file.c>` | compile and link a companion C file |
| `-L<dir>`, `-I<dir>` | library and include search paths |
| `--cc <compiler>` | use another C compiler; may name a wrapper that adds `-fsanitize=address,undefined` |
| `--print-shims` | print the `<pkg>_shim.c` files the program needs, transitively, and exit: what a hand-written `cc` line must pass |
| `--print-deps` | print the pkg-config names it needs, transitively, and exit |

A **shim** adapts a C API the FFI cannot express directly (a variadic, a union, a
non-POD struct): write a small C wrapper, declare it as an `extern fn`, and pass
the file with `--shim`.

Worked bindings: [`examples/sqlite/`](../../examples/sqlite) drives libsqlite3
through raw `ptr` and `inout` out-parameters, with no shim and `--pkg` for the
link. [`tools/tycho-du/`](../../tools/tycho-du) pairs a **handle** with `--shim`;
[`tools/tycho-fh/`](../../tools/tycho-fh) declares one over a hand-built static
library.

---

## Working with it

### Quick start

Calling `cbrt` from libm needs no link flags, because libm is always linked:

```tycho
extern fn cbrt(x: float) -> float

fn main():
    println(str(cbrt(27.0)))    # 3.0
```

For a library that isn't linked by default, name it in the declaration —
`extern "z" fn …` links `-lz` automatically.

### Surface syntax

An `extern fn` is a bodyless declaration whose name *is* the C symbol name,
optionally prefixed with the library to link:

<!-- fence-skip: a signature showcase with no main, naming SDL2 and a SQLite shim this host has no symbol for -->
```tycho
extern fn rand() -> i32                                  # libc (already linked)
extern fn hypot(x: float, y: float) -> float             # libm (already linked)
extern "z" fn zlibVersion() -> string                    # links -lz
extern "SDL2" fn SDL_Init(flags: u32) -> i32                 # links -lSDL2
extern fn sx_col_text(stmt: ptr, i: int) -> string       # C string in, Tycho string out
```

The symbol name is never mangled, even inside a package — a C symbol is global —
so an `extern` can be declared and called from any package. A bare `extern fn`
(no `"Lib"`) assumes the symbol is already linked; libc and libm always are.

A symbol the language already provides cannot be re-declared. `extern fn
sqrt(x: float) -> float` does not compile: `sqrt` is a builtin, so it fails with
`'sqrt' is already defined`.

### Type mapping

Scalars, `string`, `bytes`, and the opaque handle `ptr` cross the boundary (sized
integers map to their `<stdint.h>` type):

| Tycho type | C type      | direction | notes |
|-----------|-------------|-----------|-------|
| `int`     | `int64_t`   | in/out    | zero-cost |
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

**String passing.** A C function taking `const char *` takes a Tycho `string`
directly; it is passed NUL-terminated, so C sees it only up to any interior NUL.

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
tagged POD struct: `core:gfx`'s shim decodes `SDL_Event` into
`Event { kind, key, x, y, button }`, so no layout-dependent union reaches Tycho.
A language-level union would need initialization, active-member and ownership
rules; it is not an FFI shortcut.

### Typed handles (safe-by-default resources)

A raw `ptr` is never freed for the caller. For a C resource with a clear owner
and a destructor (a `sqlite3*`, a `FILE*`, a zlib stream), declare a **handle**
type instead — the transpiler frees it automatically at the end of the scope
that owns it, so it cannot leak or be used after close:

<!-- fence-skip: an excerpt with no main; its extern fns are a hypothetical shim (db_shim.c) this page does not ship -->
```tycho
handle Db:                                   # Db's C type is void*; freed by its destructor
    free: db_close                           # a C symbol (declared as an extern fn below)

extern fn db_open(path: string) -> Db        # opener -> an owned Db (shim over sqlite3_open)
extern fn db_exec(db: Db, sql: string) -> i32  # borrow: uses it, does not free
extern fn db_close(db: Db) -> int            # the destructor

fn run():
    db := db_open("x.db")                    # owned here
    db_exec(db, "create table t(x)")         # borrow — db stays usable
    # db is freed here, exactly once: the compiler emits db_close(db)
```

- **RAII.** The destructor `free:` runs at the owning variable's scope exit —
  every way out of *that* scope (block end, early `return`, `break`/`continue`,
  `or_return`). A `continue` frees a handle declared inside the loop body, not
  one owned by a scope around the loop. Freed exactly once.
- **Borrow on pass.** Passing a handle to a fn/extern passes the `void*`; the
  callee does not free it. Only the owning scope does.
- **Affine, fail-closed.** The ownership rules in [The boundary](#the-boundary)
  are each a compile error, so a handle can never be aliased into a double-free
  or escape its destructor.

### Lifetime & safety

The rule behind every item here: foreign memory never enters Tycho's owned
world.

- **`string` return (C → Tycho):** copied into the caller's arena (see
  [The boundary](#the-boundary)); a `NULL` return becomes `""`. When the result
  is consumed immediately (`len(f())`, `print(f())`) the copy may be skipped with
  byte-identical output.
- **`ptr`:** an opaque `void *`. Tycho supports only passing it back to C,
  comparing it (`==`/`!=` against another `ptr` or the `null` literal), and
  `is_null(p)`. No dereference, no arithmetic — Tycho shuttles the handle and
  never touches what it points at. A `ptr` rides the scalar paths, so it can
  also be a local, a struct field, or an array element (copied by value).
- **Callbacks into Tycho are limited to named functions with scalar/`ptr`/`void`
  shapes.** The compiler emits a C-ABI trampoline with its own per-call arena,
  so C may retain the pointer. A closure or a function variable is rejected:
  its environment has no C-side slot.

What can still go wrong, plainly: passing a bad handle crashes inside C, not in
Tycho; C code with shared state races across tasks (see [Threads](#threads));
a pointer freed before the C function returns is a use-after-free no normal
build catches; a `malloc`ed string return leaks, while a `bytes` return is
freed. The fix in each case is on the C side of the boundary: valid handles,
per-thread or serialized state, a buffer that outlives the call, and the
`bytes` convention for allocated returns.

### Limitations (by design)

- **No variadics.** Wrap a `printf`-style function in a fixed-arity C shim.
- **No automatic binding generation.** Each C function is declared by hand.
- Composites beyond the exceptions above, closure callbacks, and `ptr`
  dereference are refused, as described above: anything richer would break
  value semantics on the Tycho side or lean on an unstable C ABI.

---

Why the boundary is shaped this way — the string representation, the arena
copy, and the `extern` keyword — is in [design-ffi](../internals/design-ffi.md),
for maintainers.
