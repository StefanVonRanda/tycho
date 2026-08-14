# FFI (calling C)

> **Thesis context:** The FFI tests that the arena model coexists with C's heap without
> escape hatches that would weaken it. Foreign strings are copied into the caller's arena
> at the call site; opaque handles (`ptr`) are shuttled but never dereferenced. The
> boundary is narrow on purpose — anything richer would either break value semantics or
> depend on an unstable C ABI.

`extern fn` declares a C function Tycho can call. It's bodyless, bound to a direct C symbol,
and can optionally name the library to link.

```tycho
extern fn getpid() -> int                            # libc
extern "m" fn cos(x: float) -> float                 # links -lm
extern fn sx_col_text(stmt: ptr, i: int) -> string   # C string in, Tycho string out
extern fn crc32(data: bytes, len: u32) -> u32        # sized ints: real uint32_t at the C ABI
```

## The boundary

Here's what can cross:

- **Scalars** — `int`, `char`, `float`, `bool`.
- **Sized integers** — the first-class `u32`, `u64`, `f32` cross as **themselves** (an
  `extern` parameter or return of one of those types takes/produces a value of that exact
  type). The narrower spellings `u8 u16 i8 i16 i32 i64` are valid **only** in an `extern`
  signature and are `int` to Tycho — you pass and receive ordinary `int` values, but the
  emitted C prototype uses the real fixed-width type so a call matches e.g.
  `int16_t f(uint8_t)` at the ABI. On the way in the `int` narrows to the C width; on the
  way back the C result widens to `int`, **sign-extended** for `i8`/`i16`/`i32`/`i64` and
  **zero-extended** for `u8`/`u16` (so `u8(-1)` reads back as `255`, `i8(200)` as `-56`).
- **`string`** — passed as a C `char*`; a C-returned string is **copied into the caller's
  arena** at the call site, so Tycho never holds a pointer into C-owned memory. A nullable C
  return is declared `-> Option(string)`.
- **`bytes`** — a binary buffer (interior NULs intact), crossing as a `(pointer, length)` pair.
- **`[int]` and `[float]`** — a scalar array crosses as a `(const T*, long)` pair (like
  `bytes`); an array of any other element type does not.
- **`ptr`** — an opaque foreign handle, a `void*` Tycho never dereferences. The `null` literal
  and `is_null(p)` work on it.
- **typed `handle`s** — `handle Name: free: c_fn` declares a `void*` whose C destructor runs
  automatically at scope exit (RAII), so a foreign resource won't leak or get used after close.
  What makes that hold is that a handle is **affine — exactly one owner**, and the rules are
  worth knowing before you reach for one: it cannot be copied (`g := f`), reassigned, stored in
  an array, map, struct, tuple, `Option` or `Result`, captured by a closure or `parallel for`,
  or returned from a Tycho function. An `extern` opener is the only thing that may return one.
  Passing a handle **borrows** it — the callee does not free it — so passing is how you use it
  somewhere else. `close(h)` runs the destructor early on a handle **variable** and nulls it;
  the scope-exit free is null-guarded, so the destructor runs exactly once either way. Using a
  handle after `close` passes null to C: a logic bug, not memory corruption, and not rejected.
  (The copy and struct-field cases were accepted until 2026-08-14, and the copy double-freed.)
- **`inout` scalar out-parameters** (a numeric scalar or `ptr`) — cross too; a `string`,
  `bytes`, handle, or composite `inout` out-parameter is **rejected** (no trivial out-param ABI).

Maps, structs, and non-scalar arrays are **rejected** at the boundary — fail closed. (The
scalar arrays `[int]`/`[float]` do cross, above.)

## Threads

The "race-free by construction" guarantee is over Tycho values and does **not** cross the FFI.
A C function touching process-global or `static` state is invisible to the compiler, so you'll
need to isolate such state per thread or serialize the calls. See [Concurrency](concurrency.md#the-safety-envelope).

## Linking

Ergonomics live on the `tychoc` command line: `--link` (a raw linker flag), `--pkg`
(pkg-config), and `--shim` (compile a companion `.c` alongside). For a complete worked binding,
in-memory SQLite is at [`examples/sqlite/`](../../examples/sqlite), and the full rules are in
[the FFI design note](../guides/ffi.md).
