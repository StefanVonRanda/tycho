# Typed handles with destructors (FFI R2) — settled design

Status: **design settled, not yet implemented.** Surface = option **A** (a
dedicated `handle` declaration). v1 reuses the affine + finalizer machinery that
`spawn`/`wait` tasks already have — which is the proof that the ownership model is
sound. Implement in BOTH compilers, then fixpoint + tests + docs, as for `bytes`
([[../../...]] commit ab49393).

## Problem

A C handle (`sqlite3*`, `FILE*`, a zlib stream, a socket) crosses FFI today as the
opaque `ptr` (`docs/ffi.md`). Nothing frees it automatically — you call a close
`extern` by hand — and nothing stops a use-after-close, double-close, or leak. R2
makes a handle **safe-by-default**: a named handle type with a compiler-known
destructor that runs at scope exit.

## Surface (option A)

A struct/enum-style declaration naming the C destructor symbol:

```tycho
extern "sqlite3" fn sqlite3_open(path: string) -> Db        # opener -> owned handle
extern "sqlite3" fn sqlite3_exec(db: Db, sql: string) -> int  # borrow: takes Db, does not free
extern "sqlite3" fn sqlite3_close(db: Db) -> int            # the destructor C symbol

handle Db:
    free: sqlite3_close
```

- `handle Name:` then an indented `free: free_fn`. `free_fn` is a C symbol declared
  as `extern fn free_fn(h: Name) -> int|void` (its lib linkage comes from that
  extern). `Name`'s C representation is `void*` (same as `ptr`), but it is a
  DISTINCT, affine type.
- An `extern fn open(...) -> Name` yields an **owned** handle.

## Semantics (v1)

- **RAII / scope-exit free.** The owning variable frees its handle at scope exit:
  the compiler emits `free_fn(h)` exactly where `tycho_task_finish(h)` would be
  emitted for a task. Covers block end, early `return`, `break`/`continue`, and
  `or_return` — the same exit set the task finalizer already covers.
- **Borrow on pass.** Passing a handle to a fn/extern passes the `void*`; the
  callee does NOT free it (only the owning scope does). So `sqlite3_exec(db, ...)`
  is a borrow; `db` stays usable and frees once, at its scope end.
- **Affine, exactly-one-owner.** Like a task: reassigning a handle var frees the
  old one first; a handle is never copied (no aliasing → no double-free).
- **v1 fail-closed restrictions (identical to tasks — reuse `task_container_err`):**
  a handle cannot be stored in an array/map/struct/tuple/Option/Result, captured
  by a closure/parallel-for, or escape via `return`. These are the same bans
  tasks already enforce, so this is the affine-type model, not a deferred subset.
- **Deferred past v1 (each a clean follow-up):** explicit early `close(h)`
  (consume + suppress the scope-exit free, mirroring `wait(t)`); returning/moving a
  handle out of its scope (ownership transfer + re-home, mirroring an escaping
  task); storing handles in containers.

## Implementation map (mirrors `bytes` and the task machinery)

Both compilers. tychoc symbols cited; tychoc0 has the parallel string-tag forms.

1. **Lexer/parse.** Add `handle` keyword; `parse_handle` next to `parse_struct`
   (src/tychoc.c:2652) / dispatch at the top-level loop (2939-2940). Record
   `{name, free_fn_symbol}` in a `g_handles` table. tychoc0: a `parse_handle`
   beside `parse_struct`, a handle registry, and a `"handle:Name"` type tag.
2. **Type family.** A `T_HANDLE_BASE` range mirroring `T_TASK_BASE`
   (src/tychoc.c:499) with `IS_HANDLE`/`HANDLE_ID`. `c_type(handle) = "void *"`;
   `type_name` = the handle name. tychoc0: `"handle:Name"` tag, `cty -> "void* "`.
3. **Affine + container bans.** Add `IS_HANDLE` beside every `IS_TASK` guard
   (container insert 541/573/621/643/706/835, closure capture 3304, parfor 4303,
   reassign 4729, discard 4947). Reuse `task_container_err`.
4. **Finalizer emission.** When a handle var is declared, `taskvar_push` a
   `"free_fn(h_<var>)"` call (src/tychoc.c:6590) so `task_finishes_from` /
   `return_frees` (6594/6607) emit it at every scope exit — the existing path,
   just a different finalizer string per handle (look up `free_fn` from
   `g_handles`). tychoc0: the same in its preamble/finalizer emit.
5. **extern open/free.** `parse_extern_fn` (2600) already allows a return type;
   permit a handle type as param + return (like `ptr`). The open returns the
   `void*`; `free_fn(h: Name)` lowers `Name` → `void*` arg.
6. **No runtime helper needed** — `free_fn` is the user's C symbol; the compiler
   only emits the call. (Contrast `bytes`, which needed `tycho_bytes_from_c`.)

## Verification plan

- tests/ffi: a `handle` over a fixture C "resource" (an open that bumps a global
  refcount, a close that decrements). Assert: the handle frees exactly once at
  scope exit (refcount returns to 0), borrow-on-pass works, and ASan/Leak is
  clean. Both compilers + golden, in `make ci`.
- A reject fixture: storing a handle in an array / returning it must fail closed.
- fixpoint B==C (additive; tychoc0.ty itself uses no handles).

## Why this is sound

It is the task model with a user-supplied finalizer instead of `tycho_task_finish`.
Tasks already prove: affine ownership + scope-exit finalizer + container/escape
bans = no use-after-free, no double-free, no leak. A handle is the same shape with
`free_fn` as the finalizer.
