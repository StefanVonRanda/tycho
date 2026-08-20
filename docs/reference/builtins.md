# Builtins

> **Memory:** Builtins follow the same per-scope arena discipline as any Tycho code.
> Every builtin that allocates — string operations, channel operations — does so in
> the caller's arena.

The built-in functions, by category. These are part of the language — always there, no
import needed; the importable [standard library](../guides/corelib.md) layers more on top. Anything written
as an operator or keyword (`m[k]`, `k in m`, `delete`, `for x in xs`) lives on its own topic page.

## Output and input

| Builtin | Type | Notes |
| --- | --- | --- |
| `print(s)` | `string -> void` | No implicit newline; add `"\n"`. |
| `println(s)` | `string -> void` | `print(s)` plus a trailing newline. String-only, like `print` — use `println(str(x))` for non-strings. |
| `input()` | `-> string` | One line from stdin (newline stripped). |
| `read_all()` | `-> string` | All of stdin as one string. |
| `args()` | `-> [string]` | Command-line arguments; `args()[0]` is the program name. |
| `getenv(name)` | `string -> string` | The variable's value, or `""` if unset. |
| `die(msg)` | `string -> void` | Print `msg` to stderr and exit with status 1. |
| `exit(code)` | `int -> void` | Terminate with status `code`, printing nothing; never returns. `exit(0)` is an ordinary answered exit, not a failure. |

## Conversions

| Builtin | Type | Notes |
| --- | --- | --- |
| `str(x)` | `int`/`float`/`bool` `-> string` | A **finite** float prints as the shortest decimal (15, 16 or 17 significant digits) that reads back as the same `float`, always with a `.`; a bool prints `true`/`false`. NaN and the infinities are the exception — see below. |
| `to_float(n)` | `int -> float` | Widen. |
| `to_int(x)` | `float -> int` | Truncate toward zero. |
| `to_bytes(s)` / `to_str(b)` | `string <-> bytes` | Same byte buffer; `bytes` may carry interior NULs. |
| `to_bytes(xs)` | `[int] -> bytes` | Each element `& 0xFF` into a fresh buffer — builds a binary `bytes` (interior NULs and all) that a `string` can't hold. |
| `chr(n)` | `int -> string` | The one-byte string for byte value `n` (`0`–`255`). |
| `to_char(n)` | `int -> char` | The byte value `n` as a `char`. Outside `0..255` it **aborts** — where `chr(n)` returns a one-byte *string*, this returns a `char`. |

**NaN and the infinities do not round-trip.** `str` hands those three to the host C
library, which on glibc spells them `-nan`, `inf` and `-inf` — no decimal point, and not
the same text on every platform. `strings.parse_float` refuses all three by design, so a
float written with `str` and read back with `parse_float` survives only if it was finite.
`x == x` is false for exactly NaN, and `x - x == 0.0` is false for exactly NaN and the
infinities, which is how to check before serialising.

(A newtype's `to_int` / `to_float` / `to_str` / `to_bool` / `to_under` unwrappers are on the
[Types](types.md#distinct-newtypes-type) page.)

## Strings

Strings are byte buffers; `len`, `s[i]`, `substr`, and `find` are all byte-oriented (not
Unicode-aware). String escapes: `\n \t \r \\ \"` — those five and no others, with the
`char` set and the rejected spellings on the [Types](types.md#string-escapes) page.

| Builtin | Type | Notes |
| --- | --- | --- |
| `len(s)` | `string -> int` | Byte length. |
| `substr(s, a, b)` | `(string, int, int) -> string` | Substring `[a, b)`, a fresh copy. Out-of-range bounds are **clamped**, not an error. |
| `find(s, sub)` | `(string, string) -> int` | Byte index of the first `sub`, or `-1`. |
| `char_at(s, i)` | `(string, int) -> char` | The byte at `i` as a `char` — the same read and the same bounds abort as `s[i]`. |
| `split(s, sep)` | `(string, string) -> [string]` | Split on a non-empty separator; `n` separators yield `n+1` fields. An empty separator aborts. |

## Arrays

| Builtin | Type | Notes |
| --- | --- | --- |
| `len(a)` | `[T] -> int` | Element count. |
| `push(a, v)` | `([T], T) -> void` | Append in place (needs a mutable array). |
| `pop(a)` | `[T] -> T` | Remove and return the last element; aborts if empty. |
| `reserve(a, n)` | `([T], int) -> void` | Capacity hint (also works on a map place `m[k]`): preallocate room for `n`. A hint, not a length — `len` is unchanged and pushing past `n` still grows; an unallocatable capacity aborts. |

## Maps

`len(m)`, `m[k] = v`, `k in m`, `delete m[k]`, and `keys(m)` are documented on the
[Maps](maps.md) page. The default-valued read is a method on the map:

| Builtin | Type | Notes |
| --- | --- | --- |
| `m.get(k, d)` | `(map, K, V) -> V` | Value for `k`, or default `d` if absent. `m.get(k)` is `m[k]` (value-type zero on a miss). |
| `hash(x)` | `K -> int` for any map-key type | The hash the maps themselves use, exposed: values equal by `==` hash equal. Returns the full 64-bit value. |

## Concurrency

See [Concurrency](concurrency.md) for semantics.

| Builtin | Type | Notes |
| --- | --- | --- |
| `wait(t)` | `Task(T) -> T` | Join a spawned task (result deep-copied out). Exactly once per task. |
| `ncpu()` | `-> int` | Online CPUs, or `TYCHO_THREADS` when set to an integer >= 1. The *requested* worker count, **not** the fan-out width: `parallel for` clamps to 64 chunk tasks. |
| `channel(T, cap)` | `-> Channel(T)` | A bounded lock-free queue (`cap` rounds up to a power of two). Legal only as a declaration's direct RHS. |
| `send(ch, v)` | `(Channel(T), T) -> void` | Deep-copy `v` in; blocks when full; aborts if closed. |
| `recv(ch)` | `Channel(T) -> Option(T)` | Blocking receive (deep-copied out); `None` means closed **and** drained. |
| `close(ch)` | `Channel(T) -> void` | Receivers drain then see `None`; a further send or second close aborts. |

## Filesystem and time

| Builtin | Type | Notes |
| --- | --- | --- |
| `read_file(path)` | `string -> string` | Whole file as a string, or `""` if it can't be opened. |
| `write_file(path, s)` | `(string, string) -> bool` | Write `s`'s exact bytes (truncating); `false` if it can't be opened. |
| `list_dir(path)` | `string -> [string]` | Entries excluding `.`/`..` (filesystem order); empty if it can't be opened. |
| `clock()` | `-> int` | Monotonic nanoseconds (differences are meaningful; the absolute value is not). |
| `now()` | `-> int` | Wall-clock seconds since the UNIX epoch. |

## Float math (libm)

| Builtin | Type |
| --- | --- |
| `sqrt(x)` / `pow(x, y)` / `floor(x)` / `fabs(x)` | `float… -> float` |

## FFI and sized-integer helpers

Supporting the [FFI boundary](ffi.md) and the sized numeric types; always available, no import.

| Builtin | Type | Meaning |
| --- | --- | --- |
| `eprint(s)` | `string -> void` | Write `s` to standard error (no newline, no exit). |
| `is_null(p)` | `ptr -> bool` | Test an opaque FFI `ptr` for null. |
| `to_ptr(n)` | `int -> ptr` | Make a sentinel `ptr` from an int (e.g. `(void*)-1`); Tycho never dereferences it. |
| `to_i32(n)` | `int -> int` | Sign-extend the low 32 bits of `n` — for an `extern` that returns a 32-bit C `int`. |
| `to_u32(x)` / `to_u64(x)` / `to_f32(x)` | numeric `-> u32`/`u64`/`f32` | Convert any numeric scalar to the sized type. |
