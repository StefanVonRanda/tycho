# FFI (calling C)

`extern fn` declares a C function Tycho can call. It's bodyless, bound to a direct C symbol,
and can optionally name the library to link.

```
extern fn getpid() -> int                            # libc
extern "m" fn cos(x: float) -> float                 # links -lm
extern fn sx_col_text(stmt: ptr, i: int) -> string   # C string in, Tycho string out
```

## The boundary

Here's what can cross:

- **Scalars** — `int`, `float`, `bool`.
- **`string`** — passed as a C `char*`; a C-returned string is **copied into the caller's
  arena** at the call site, so Tycho never holds a pointer into C-owned memory. A nullable C
  return is declared `-> Option(string)`.
- **`bytes`** — a binary buffer (interior NULs intact), crossing as a `(pointer, length)` pair.
- **`ptr`** — an opaque foreign handle, a `void*` Tycho never dereferences. The `null` literal
  and `is_null(p)` work on it.
- **typed `handle`s** — `handle Name: free: c_fn` declares a `void*` whose C destructor runs
  automatically at scope exit (RAII), so a foreign resource won't leak or get used after close.
- **`mut` scalar / string out-parameters** — cross too.

Composite aggregates (arrays, maps, structs) are **rejected** at the boundary — fail closed.

## Threads

The "race-free by construction" guarantee is over Tycho values and does **not** cross the FFI.
A C function touching process-global or `static` state is invisible to the compiler, so you'll
need to isolate such state per thread or serialize the calls. See [Concurrency](concurrency.md#scope-of-the-guarantee).

## Linking

Ergonomics live on the `tychoc` command line: `--link` (a raw linker flag), `--pkg`
(pkg-config), and `--shim` (compile a companion `.c` alongside). For a complete worked binding,
in-memory SQLite is at [`examples/sqlite/`](../../examples/sqlite), and the full rules are in
[the FFI design note](../ffi.md).
