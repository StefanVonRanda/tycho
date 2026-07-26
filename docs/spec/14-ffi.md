# 24. `extern` and the C boundary · 25. Typed handles · 26. FFI and concurrency

Tycho calls C through `extern` functions. The boundary is deliberately narrow and
**fails closed**: only a fixed set of types may cross, and everything that
crosses is copied so that Tycho never holds a pointer into C-owned memory nor
exposes its own storage to C.

> Provenance: `docs/reference/ffi.md`; `parse_extern_fn` `src/tychoc.c:3212-3282`;
> boundary copy routines `runtime/tycho_rt.c:740-761`,`:1026`,`:1127-1134`; typed handles
> `docs/internals/typed-handles-design.md`.

## 24. `extern` and the C boundary

An `extern` function is bodyless and binds to a C symbol
([§4.1.3](02-grammar.md#413-extern-functions-and-subscripts)); an optional
library name (`extern "m" fn cos(x: float) -> float`) adds a link flag. Its name
is emitted unmangled and it receives no arena argument.

### 24.1 Crossable types

Only these may appear in an `extern` signature; a **map, struct, or non-scalar
array** is rejected at the boundary:

- **Scalars** `int`, `char`, `float`, `bool`.
- **Sized integers.** The whole fixed-width integer family — `u8`/`u16`/`u32`/
  `u64` and `i8`/`i16`/`i32`/`i64` — and `f32` ([§5.2](03-types.md#52-scalar-types))
  are first-class types that cross as themselves: an `extern` parameter or return
  of one of these takes or produces a value of that exact type, emitted as the
  matching fixed-width C type so the call matches the C ABI. An argument of a
  different type is a type error — use the `to_uN` / `to_iN` / `to_f32` conversion
  to produce the sized value.
- **`string`** — passed as a C `char*`. A `string` **returned** from C is copied
  into the caller's storage at the call site, so Tycho never retains a pointer
  into C memory. A nullable C return is declared `-> Option(string)`.
- **`bytes`** — crosses as a `(pointer, length)` pair, preserving interior `NUL`s;
  a `bytes`/array returned from C is copied into an arena and the C buffer freed.
- **`[int]` and `[float]`** — a **scalar** array crosses as a `(const T*, long)`
  pair (like `bytes`); an array of any other element type, and any map or struct,
  is rejected (no flat, self-describing C ABI).
- **`ptr`** — an opaque `void*` Tycho never dereferences; the `null` literal and
  `is_null(p)` apply.
- **typed `handle`s** — §25.
- **`inout` out-parameters** — a numeric scalar or `ptr` only (`int`/`char`/
  `float`/`bool`/`ptr`); a `string`, `bytes`, handle, or composite `inout`
  out-parameter is **rejected** (no trivial out-param ABI).

Every value returned from C that carries storage (`string`, `bytes`, an array) is
**deep-copied into the caller's storage at the call site**; a program never holds
a live pointer into C-owned memory.

The sized-integer conversions truncate or extend by the C cast (probed on both
compilers). `to_uN` / `to_iN` narrows a value to the type's low `N` bits; widening
back with `to_int` **sign-extends** the signed types (`i8`/`i16`/`i32`/`i64`) and
**zero-extends** the unsigned (`u8`/`u16`/`u32`/`u64`): e.g. `to_i32(-1)` → `-1`,
`to_i32(2^32)` → `0`, `to_u8(-1)` → `255`, `to_u8(256)` → `0`, `to_i8(200)` →
`-56`. An `extern` parameter of a sized type requires an argument of that exact
type (a bare `int` is a type error).

### 24.1.1 Returning a payload **and** a classification

An `extern` return is one value, and `-> Result(T, E)` is **not** a crossable
return shape: `Result` is a Tycho aggregate with no flat C ABI, and it is absent
from §24.1 deliberately, not by omission. So a C function whose failure has more
than one *cause* — `bytes` that came back empty because the peer closed, because
a timeout expired, or because the read failed — has one documented spelling:

**a numeric `inout` out-parameter carrying the classification, alongside the
payload return.** The classification is a plain `int` (a small enumerated code the
package maps onto its own error enum); the payload keeps whatever crossable return
type it already had.

```tycho
# corelib/net/net.ty: 0 EOF, 1 data, 2 timeout, 3 error
extern fn netx_read(fd: int, max: int, status: inout int) -> bytes
```

**The C ABI, and it is the one rule worth knowing.** Written parameters lower in
written order, an `inout` becoming a `T*` (§24.1); a `bytes` or array *return*
lowers to a `void` function with **two trailing** out-params
(`unsigned char **out, tycho_int *outlen`) appended after all of them. So the
classification pointer sits **ahead** of the payload out-params even though it is
written last, and the C definition must match that order exactly:

```c
void netx_read(tycho_int fd, tycho_int max, tycho_int *status,
               unsigned char **out, tycho_int *outlen);
```

Emitted prototype, for a `bytes` return and for a `string` parameter respectively:

```c
extern void netx_read(tycho_int , tycho_int , tycho_int *, unsigned char **, tycho_int *);
extern void iox_read_file(char *, tycho_int *, unsigned char **, tycho_int *);
```

Rules that follow from §24.1 and are not optional:

- The classification parameter must be a **numeric scalar or `ptr`** — that is the
  whole `inout` crossable set. A `string`, `bytes`, handle or composite `inout` is
  rejected, so the classification cannot be a message; it is a code.
- **Set it on every path, first.** The shim writes a failure code before anything
  can fail (`*status = TY_RD_ERR;` in `corelib/net/net_shim.c`) so an early
  `return` fails closed rather than leaving the caller's variable untouched.
- **The payload must still be valid on the failure paths.** Leaving `*out = NULL`
  is correct: `tycho_bytes_from_c` builds an empty buffer and frees nothing.
- The Tycho-side wrapper is what turns the pair into a `Result` — the FFI never
  produces one. `net.read` reads `status` and returns `Ok(b)` / `Err(Eof)` /
  `Err(Timeout)` / `Err(Failed)`; `io.read_bytes` does the same over
  `iox_read_file`'s codes.

**When NOT to use it.** If the classification and the failure share one integer
and there is no separate payload, return the code directly and skip the out-param:
`extern fn iox_stat_kind(path: string) -> int` is the same four codes as
`iox_read_file`'s with no `inout`, because the *kind* is the whole answer.

> Provenance: the shape is used twice — `netx_read` (`corelib/net/net_shim.c:236-259`,
> declared `corelib/net/net.ty:99-102`) and `iox_read_file`
> (`corelib/io/io_shim.c:61-96`, declared `corelib/io/io.ty:81-85`). The ordering
> rule is `gen_extern_proto` (`src/tychoc.c:10385-10397`): written params first,
> the return's out-params appended. Written down here because it was reproduced
> verbatim from one shim into the other with this section listing neither it nor
> `-> Result(T, E)` (FRICTION.md).

### 24.2 Linking

An `extern "Lib"` adds `-lLib` to the link line; a package's `deps` file adds
`pkg-config` flags, and a co-located `<pkg>_shim.c` is compiled and linked
automatically ([§28](15-program.md)). `-lm` (a link flag) and `-fwrapv` (a
compile flag) are always on this single `cc` command line.

## 25. Typed handles

A `handle Name: free: c_free` declares a nominal, affine, opaque C resource — a
`void*` whose destructor `c_free` runs automatically at scope exit (RAII). A
handle is typically produced by an `extern` "opener" (the only kind of function
permitted to *return* a handle) and released without explicit calls.

- **A handle name is a type name, and the collision rule is symmetric.** A
  handle shares the one type namespace with structs, enums and newtypes — it is
  written in type position and mangled to a C typedef like them — so a duplicate
  would otherwise reach `cc` as two declarations of one name, with no Tycho-level
  diagnostic. The rule therefore runs in **both** directions: a `handle Name`
  MUST NOT repeat a struct, enum, newtype or handle name declared earlier in the
  file, and a `struct`, `enum` or `type` declaration MUST NOT repeat a **handle**
  name declared earlier in the file. Either order is a compile error, reported at
  the second declaration. Locked by `tests/reject/handle_dup_name.ty` (the
  handle-second order) and `tests/reject/handle_then_struct.ty`,
  `handle_then_enum.ty`, `handle_then_newtype.ty` (the handle-first order).
- **Scope-exit free.** The owning variable's destructor runs at every scope exit
  — block end, early `return`, `break`, `continue`, `or_return`.
- **Borrow on pass.** Passing a handle passes the `void*`; the callee does **not**
  free it — only the owning scope does.
- **Affine, exactly one owner.** A handle MUST NOT be copied; reassigning a handle
  variable frees the previous handle first. It cannot be stored in an array, map,
  struct, tuple, `Option`, or `Result`, captured by a closure or `parallel for`,
  or returned from a Tycho function.
- **Early `close(h)`.** `close(h)` runs the destructor immediately and sets the
  handle to null; the scope-exit finalizer is null-guarded, so the destructor
  runs **exactly once**. `close` requires a handle **variable** (a call result has
  no owning scope); otherwise it is a compile error.
- **Use after `close`.** Using a handle after `close(h)` passes null to C — a
  logic bug, **not** memory corruption, and (in this version) **not**
  compile-rejected (it mirrors the run-time-not-compile-time stance on a second
  `wait`).

## 26. FFI and concurrency

The race-freedom guarantee of [§20](13-concurrency.md#20-the-concurrency-model)
covers **Tycho values only and does not cross the FFI boundary**. A C function
that reads or writes process-global or `static` state is invisible to the
compiler; two tasks calling it race exactly as the same C would. Code that calls
such a C function from multiple tasks MUST make the C side thread-safe itself —
the reference corelib shims do this with per-thread state (`static __thread …`).
A conforming implementation makes no thread-safety guarantee about foreign code.
