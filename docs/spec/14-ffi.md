# 24. `extern` and the C boundary · 25. Typed handles · 26. FFI and concurrency

Tycho calls C through `extern` functions. The boundary is deliberately narrow and
**fails closed**: only a fixed set of types may cross, and everything that
crosses is copied so that Tycho never holds a pointer into C-owned memory nor
exposes its own storage to C.

> Provenance: `docs/reference/ffi.md`; `parse_extern_fn` `src/tychoc.c:4197-4272`;
> boundary copy routines `runtime/tycho_rt.c:1122-1134`,`:1749@tycho_arr_int_from_c`,`:1518-1529`.

## 24. `extern` and the C boundary

An `extern` function is bodyless and binds to a C symbol
([§4.1.3](02-grammar.md#413-extern-functions-and-subscripts)); an optional
library name (`extern "m" fn cos(x: float) -> float`) adds a link flag. Its name
is emitted unmangled and it receives no arena argument.

### 24.1 Crossable types

Only these may appear in an `extern` signature; a **map, struct, or any array
outside the list below** is rejected at the boundary:

- **Scalars** `int`, `char`, `float`, `bool`.
- **Sized integers.** The whole fixed-width integer family — `u8`/`u16`/`u32`/
  `u64` and `i8`/`i16`/`i32`/`i64` — and `f32` ([§5.2](03-types.md#52-scalar-types))
  are first-class types that cross as themselves: an `extern` parameter or return
  of one of these takes or produces a value of that exact type, emitted as the
  matching fixed-width C type so the call matches the C ABI. An argument of a
  different type is a type error — use the `to_uN` / `to_iN` / `to_f32` conversion
  to produce the sized value.
- **`string`** — passed as a C `char*`, with no length alongside it. A `string`
  **returned** from C is copied into the caller's storage at the call site, so
  Tycho never retains a pointer into C memory. A nullable C return is declared
  `-> Option(string)`. A `string` holding an interior `NUL` does **not** survive
  the round trip in either direction — see "Interior `NUL`s" below.
- **`bytes`** — crosses as a `(pointer, length)` pair, preserving interior `NUL`s;
  a `bytes`/array returned from C is copied into an arena and the C buffer freed.
- **`[int]` and `[float]`** — a **scalar** array crosses as a `(const T*, int64_t)`
  pair (like `bytes`), in either direction; an array of any other element type,
  and any map or struct, is rejected (no flat, self-describing C ABI).
- **`[string]` — parameter direction only.** It crosses as a
  `(const char *const *, int64_t)` pair, the same `(ptr, len)` convention as
  `[int]`/`[float]` and **not** a `NULL`-terminated `argv`; a callee wanting
  `execv` shape appends its own `NULL`. No marshalling happens: a Tycho string is
  NUL-terminated at its end, so the pointer handed over is the array's own
  element storage. The length counts *pointers*, not bytes, and no per-element
  length crosses — an element holding an interior `NUL` is therefore read short
  by the callee (see "Interior `NUL`s" below).
  The array is **borrowed for the duration of the call** — the
  callee may read the strings while it runs, and **must not retain the pointer, any
  element, or write through either** after returning. Nothing is copied, and nobody
  frees. This contract is **not enforceable by the implementation**; violating it is
  undefined behaviour. When the array is empty the pointer may be null and the
  length is `0`, so a callee must check the length before dereferencing.
  A `[string]` **return** is rejected: a `char**` out of C carries no length, the
  same reason an `inout string` out-parameter is rejected below.
- **`ptr`** — an opaque `void*` Tycho never dereferences; the `null` literal and
  `is_null(p)` apply.
- **typed `handle`s** — §25.
- **`inout` out-parameters** — a numeric scalar or `ptr` only (`int`/`char`/
  `float`/`bool`/`ptr`); a `string`, `bytes`, handle, or composite `inout`
  out-parameter is **rejected** (no trivial out-param ABI).

Every value returned from C that carries storage (`string`, `bytes`, an array) is
**deep-copied into the caller's storage at the call site**; a program never holds
a live pointer into C-owned memory.

#### Interior `NUL`s: a `string` is truncated at the boundary, silently

A Tycho `string` carries an explicit length and **may hold an interior `0x00`** —
`to_bytes` on an `[int]` is the documented way to build one
([§16](16-builtins.md)), and `chr(0)` concatenates like any other byte. A C
`char*` carries no length: it ends at its first `NUL`. The two disagree, and the
ABI has nowhere to put the difference.

So wherever a `string` crosses the FFI **as a `char*`**, the bytes at and after
the first interior `NUL` are invisible to the other side. This is **not
diagnosed** — there is no error, no truncation warning, and no way to detect it
after the fact. It applies in every `char*` direction:

- a **scalar `string` parameter** — the callee's `strlen` reports the prefix
  length, not `len(s)`;
- **each element of a `[string]` parameter** — the length counts pointers, so no
  element length is available to the callee at all;
- a **`string` return from C** — the arena copy is `strlen`-bounded
  (`runtime/tycho_rt.c@tycho_str_from_c`), so a C buffer holding `"h\0i"` becomes
  a Tycho `string` of length 1.

Given `s := "a" + chr(0) + "c"`, `len(s)` is `3` while a callee declared
`int64_t f(const char *)` returns `1` for `strlen`. The trailing bytes are still in
memory and still reachable by index through the borrowed pointer; what is lost is
the *length*, which is the only thing telling the callee they are there.

**This is a deliberate consequence of the borrow, not a defect to be worked
around in the callee.** Passing `(ptr, len)` per element would mean marshalling a
parallel length vector, giving up the "nothing is copied" property that is the
whole point of the `[string]` contract above; scanning each string for a `NUL`
before every call would turn an O(1) borrow into an O(total bytes) one, and could
not cover the return direction at all, since a `char*` out of C offers nothing to
compare against.

**Use `bytes` for any payload that may hold a `NUL`.** `bytes` crosses as a
`(pointer, length)` pair in both directions and preserves interior `NUL`s exactly
— that is what the explicit length is for. Convert with `to_bytes(s)` at the call
and `to_str(b)` on the way back, both zero-cost reinterprets
([§16](16-builtins.md)). Reserve a `string` parameter for what C means by a
string: text that ends at its first `NUL`.

A string *literal* cannot contain a `NUL` — `\0` is not a supported escape and is
rejected at compile time — so a truncating value always arrives from `chr(0)`,
from `to_str` over a constructed `bytes`, or from C.

**A package built on this boundary does not inherit the exemption.** "Documented,
not enforced" is a statement about the ABI, not about a library whose answer is a
yes/no about untrusted bytes: such a caller must cross as `bytes`, or refuse the
input by name. `core:regex` does the first — the subject crosses as
`(pointer, length)` and `REG_STARTEND` bounds the match by it
(`corelib/regex/regex_shim.c@rx_exec`). `core:io`, `core:net` and `core:os` do the
second, since a path, a host and a command line have no length-bearing syscall to
cross by: each checks the argument before anything is attempted
(`corelib/io/io.ty@has_nul`) and refuses — `Err(BadPath)` / `Err(BadAddr)` where
there is an error channel, and each call's documented "this did not happen"
sentinel where there is not. Until 2026-08-13 all three acted on the PREFIX:
`io.exists("h" + chr(0) + "i")` was `true`, `io.read` of it returned the contents
of `h`, and `io.write` of it created `h` — a caller that validated one path
reached another. `core:path` needs no guard: it is lexical and reaches no `char*`.

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

**The mirror arrangement, and why there are exactly two.** The shape above puts
the payload in the return and the classification in the `inout`. That assignment
is **forced, not preferred** — and the constraint that forces it only exists for
one kind of payload:

- **A `bytes` or array payload cannot be an `inout` at all.** The crossable
  `inout` set is int/char/float/bool/ptr (the rule above), and the compiler
  rejects the rest by name — `src/tychoc.c@ffi_scalar_type` is the predicate and
  its diagnostic spells out "string/bytes/handle/composite have no trivial
  out-param ABI". So a `bytes` payload has only the return available, and the
  classification takes the `inout` because nothing else is left. That is
  §24.1.1's case.
- **A scalar payload can occupy either slot**, so nothing above decides it and a
  second constraint does: the two halves must not share one integer's code space.
  An epoch second, a file size or a byte count can take *any* value, including
  every value the status codes use, so returning both as one integer is the
  `-1`-means-error conflation this section exists to avoid. The classification
  therefore keeps the return — where its code space is its own — and the payload
  takes the `inout`:

```tycho
# corelib/io/io.ty: the same 0..3 codes, with `mtime` as the payload
extern fn iox_stat_mtime(path: string, mtime: inout int) -> int
```

The C ABI for the mirror is the ordinary one, because a scalar return needs no
out-params: written parameters in written order, the `inout` lowering to a `T*`,
the classification as the C function's own return value. No reordering surprise —
that only arises when a `bytes` return appends its two out-params.

```c
tycho_int iox_stat_mtime(const char *path, tycho_int *mtime);
```

So the two arrangements are one rule seen from both sides: **the classification
and the payload never share a slot, and which slot each takes is settled by what
can cross as an `inout` — never by taste.** Every other rule above (numeric
classification, set it first on every path, the payload valid on failure paths,
the Tycho wrapper being what builds the `Result`) applies unchanged to both.

**When NOT to use it.** If the classification and the failure share one integer
and there is no separate payload, return the code directly and skip the out-param:
`extern fn iox_stat_kind(path: string) -> int` is the same four codes as
`iox_read_file`'s with no `inout`, because the *kind* is the whole answer.

> Provenance: five uses in the tree — three of the first arrangement and two of the
> mirror. Payload-in-the-return: `netx_read` (`corelib/net/net_shim.c@netx_read`,
> declared `corelib/net/net.ty@netx_read`), `iox_read_file`
> (`corelib/io/io_shim.c@iox_read_file`) and `iox_read_at`
> (`corelib/io/io_shim.c@iox_read_at`), both declared in `corelib/io/io.ty`.
> Payload-in-the-`inout`: `corelib/io/io_shim.c@iox_stat_mtime` and
> `corelib/io/io_shim.c@iox_stat_size`. The ordering rule is
> `src/tychoc.c@gen_extern_proto`: written params first, the return's out-params
> appended. The `inout` restriction that forces the split is
> `src/tychoc.c@ffi_scalar_type`. Written down here because §24.1.1's own shape had
> been reproduced verbatim from one shim into the other with no spec to copy, and
> the mirror was doing it again — three cross-referencing comment blocks in
> `corelib/io/io.ty` deriving by hand what this section now states (docs/internals/FRICTION.md
> item 11).

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
