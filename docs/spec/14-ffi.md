# 24. `extern` and the C boundary · 25. Typed handles · 26. FFI and concurrency

Tycho calls C through `extern` functions. The boundary is deliberately narrow and
**fails closed**: only a fixed set of types may cross, and everything that
crosses is copied so that Tycho never holds a pointer into C-owned memory nor
exposes its own storage to C.

> Provenance: `docs/reference/ffi.md`; `parse_extern_fn` `src/tychoc.c:3212-3282`;
> boundary copy routines `runtime/tycho_rt.c:740-761`,`:1127-1134`; typed handles
> `docs/internals/typed-handles-design.md`.

## 24. `extern` and the C boundary

An `extern` function is bodyless and binds to a C symbol
([§4.1.3](02-grammar.md#413-extern-functions-and-subscripts)); an optional
library name (`extern "m" fn cos(x: float) -> float`) adds a link flag. Its name
is emitted unmangled and it receives no arena argument.

### 24.1 Crossable types

Only these may appear in an `extern` signature; a composite (array, map, struct)
is **rejected** at the boundary:

- **Scalars** `int`, `float`, `bool`.
- **Sized integers** `u8`, `u16`, `u32`, `u64`, `i8`, `i16`, `i32`, `i64` — valid
  **only** in an `extern` signature (a by-value parameter or the return). They
  are `int` on the Tycho side; the emitted C prototype uses the real fixed-width
  C type, so the call matches the C ABI. A value narrows to the C width on the way
  in and widens back to `int` on return.
- **`string`** — passed as a C `char*`. A `string` **returned** from C is copied
  into the caller's storage at the call site, so Tycho never retains a pointer
  into C memory. A nullable C return is declared `-> Option(string)`.
- **`bytes`** — crosses as a `(pointer, length)` pair, preserving interior `NUL`s;
  a `bytes`/array returned from C is copied into an arena and the C buffer freed.
- **`ptr`** — an opaque `void*` Tycho never dereferences; the `null` literal and
  `is_null(p)` apply.
- **typed `handle`s** — §25.
- **`inout` scalar/string out-parameters**.

Every value returned from C that carries storage (`string`, `bytes`, an array) is
**deep-copied into the caller's storage at the call site**; a program never holds
a live pointer into C-owned memory.

> Editor's note (punch-list #20, unresolved): the Tycho-observable result of a
> sized-integer round-trip — e.g. passing a negative `int` to a `u32` parameter
> and reading the returned value — must be pinned (sign-extension vs.
> reinterpretation) rather than left at "C's defined conversion."

### 24.2 Linking

An `extern "Lib"` adds `-lLib` to the link line; a package's `deps` file adds
`pkg-config` flags, and a co-located `<pkg>_shim.c` is compiled and linked
automatically ([§28](15-program.md), forthcoming). `-lm` and `-fwrapv` are always
on the link line.

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
