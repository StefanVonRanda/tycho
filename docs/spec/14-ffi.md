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
