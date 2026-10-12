# FFI design notes (maintainers)

Why the behavior in [reference/ffi.md](../reference/ffi.md) is shaped this
way. Users don't need this page.

## Ownership asymmetry: `string` vs `bytes`

A returned `string` is copied into the caller's arena and never freed by the
call site; a returned `bytes` is copied into the caller's arena and its C
buffer *is* freed (`runtime/tycho_rt.c@tycho_bytes_from_c`). The two directions
cannot be reasoned about together: a `malloc`ed string return leaks silently,
while a `bytes` return must be `malloc`ed. Shim guidance: return a `static` or
`__thread` buffer for strings (`corelib/net/net_shim.c@netx_peer_addr` is that
shape), or hold the pointer in the shim and free it on the next call.

## Integers and prototypes

The emitted prototype for an `extern fn` lives in a different translation unit
from the shim, so C never compares the two: a shim written with `long` where
the generated declaration says 64-bit returns the wrong number on LLP64/ILP32
instead of an error. Corelib shims spell signatures with a guarded
`typedef int64_t tycho_int` as house style; a new shim only needs
`#include <stdint.h>` and `int64_t`.

## Cheap by representation

A Tycho `string` is already a NUL-terminated `char *`, so there is no wrapper
type and no `c_str()` conversion at the call site — a fat-pointer string would
have forced one. A returned `string` is always arena-copied with `NULL`
coerced to `""`, so transient or null C pointers can neither dangle nor crash
on the Tycho side. The single exception is the read-once borrow: when the
returned string is the direct argument of `len`/`print`/`println`, the copy is
skipped and the C pointer is read in place (NULL-guarded); anything that could
retain it keeps the copy. Output is byte-identical either way.

The keyword is `extern` because it reads as "defined elsewhere" and matches C;
a bare `extern fn` assumes libc/libm rather than requiring an explicit `--link`.

## Coverage

The `tests/ffi/` fixtures (scalar round-trip, string-returning extern,
NULL-return extern, `ptr` round-trip, `--shim` build) run under `make ffi`,
ASan-clean and output-identical. Worked bindings: `examples/sqlite/` (raw
`ptr`, out-parameter machinery) and `tools/tycho-du/` (the worked `handle` +
`--shim` pairing).
