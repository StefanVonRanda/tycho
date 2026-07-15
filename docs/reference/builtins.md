# Builtins

> **Thesis context:** Builtins are the lowest-level test of the arena model — I/O,
> conversion, and concurrency primitives that must work with the same per-scope arena
> discipline as any Tycho code. Every builtin that allocates (string ops, channel
> operations) does so in the caller's arena.

The built-in functions, by category. These are part of the language — always there, no
import needed; the importable [standard library](../corelib.md) layers more on top. Anything written
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

## Conversions

| Builtin | Type | Notes |
| --- | --- | --- |
| `str(x)` | `int`/`float`/`bool` `-> string` | A float prints with up to 15 significant digits, always with a `.`; a bool prints `true`/`false`. |
| `to_float(n)` | `int -> float` | Widen. |
| `to_int(x)` | `float -> int` | Truncate toward zero. |
| `to_bytes(s)` / `to_str(b)` | `string <-> bytes` | Same byte buffer; `bytes` may carry interior NULs. |
| `to_bytes(xs)` | `[int] -> bytes` | Each element `& 0xFF` into a fresh buffer — builds a binary `bytes` (interior NULs and all) that a `string` can't hold. |
| `chr(n)` | `int -> string` | The one-byte string for byte value `n` (`0`–`255`). |

(A newtype's `to_int` / `to_float` / `to_str` / `to_bool` / `to_under` unwrappers are on the
[Types](types.md#distinct-newtypes-type) page.)

## Strings

Strings are byte buffers; `len`, `s[i]`, `substr`, and `find` are all byte-oriented (not
Unicode-aware). String escapes: `\n \t \\ \"`.

| Builtin | Type | Notes |
| --- | --- | --- |
| `len(s)` | `string -> int` | Byte length. |
| `substr(s, a, b)` | `(string, int, int) -> string` | Substring `[a, b)`, a fresh copy. Out-of-range bounds are **clamped**, not an error. |
| `find(s, sub)` | `(string, string) -> int` | Byte index of the first `sub`, or `-1`. |
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

## Concurrency

See [Concurrency](concurrency.md) for semantics.

| Builtin | Type | Notes |
| --- | --- | --- |
| `wait(t)` | `Task(T) -> T` | Join a spawned task (result deep-copied out). Exactly once per task. |
| `ncpu()` | `-> int` | `parallel for` fan-out width (online CPUs; `TYCHO_THREADS` overrides). |
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
