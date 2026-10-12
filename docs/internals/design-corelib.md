# corelib design notes (maintainers)

Why the behavior in [reference/corelib.md](../reference/corelib.md) is shaped
this way. Users don't need this page.

## Paths: why the check is layered

`safe_join` is a lexical prefilter; it cannot see symlinks. `resolve_under`
resolves the longest existing ancestor and checks containment, so a missing
path reports "not found" rather than "forbidden", while a dangling symlink is
refused. Residual TOCTOU: between the check and the caller's own open, an
attacker with write access inside the root could swap a resolved component for
a symlink. Closing it needs the open done inside the check with `O_NOFOLLOW`
per component. That narrows an unconditional escape to a race an outside
attacker cannot reach; it does not remove it.

## Numbers: why crypto binds OpenSSL

Pure-Tycho 32-bit arithmetic (in `rand`, `hash`, `md5`, `sha256`) is kept
overflow-free with masks and `*`/`/`/`%` over non-negative values. But `/` and
`%` lower to a runtime helper that branches on its operands
(`runtime/tycho_rt.c@tycho_imod`), so a routine written over secret values
branches on those values however the source is written. Constant-time code
needs guarantees no high-level language makes about its own arithmetic — the
same reason Go ships `crypto/subtle`. Hence `core:crypto` binds OpenSSL rather
than reimplementing. Pointer-shaped storage costs ~1.55× C in this model
(fundamental; see [value-semantics-limits](value-semantics-limits.md));
`core:pool` removes the index-plus-generation bookkeeping, not the cost.

## `io`: why the spellings differ

`exists` is one `stat(2)`, not a parent listing (O(entries) and blind under an
unlistable parent). `make_dir`/`remove` split `EEXIST` into `Ok(false)` for
"already a directory" vs `Err(Exists)` for "something else is in the way".
`make_dir_all` is a separate name on purpose — no corelib name is secretly
recursive — and there is deliberately no `remove_all`: a recursive delete is
the one where a wrong path costs unrecoverable data. `read_at` allocates
`min(n, size - off)`, never `n`, so a length off the wire cannot over-allocate.
Writers that can only succeed or fail return `Result(void, …)` (an `Ok(false)`
would be unreachable); `make_dir`/`remove` return a bool because `Ok(false)`
is a real second answer there.

## `os` / `net`: FFI details callers don't need

An argv vector crosses as one borrowed `[string]`; the shim appends the `NULL`
`execv` wants to a copy it owns. On Windows the vector is joined by the
`CommandLineToArgvW` quoting rules and round-tripped through the real splitter
(`corelib/os/os_argv_quotecheck.c`, a `make shim-check` leg). Callee-side
remains: a program parsing its own command line by other rules (a `.bat` file)
can still read it differently. `net`'s readiness calls are `poll(2)`, not
`select(2)`, which cannot represent a descriptor ≥ 1024 (`make net-poll-check`
scores that leg).

## `httpd`: refused framing, no pipelining

Framing is refused, never guessed: chunked bodies are not decoded, conflicting
or `Transfer-Encoding`-mixed framings are `Err(Malformed)`, and the connection
must not be reused — the request-smuggling shape. Pipelining is unsupported
because handing the leftover back would need a per-connection buffer the
signatures do not carry; the leftover is discarded, never glued onto the body.

## `signal`: why shutdown, not close

No Tycho code runs in handler context: the handler is only async-signal-safe C
that shuts down the listening socket. `shutdown` on a listening socket was
chosen by measurement — it released 4/4 blocked accept loops where `close(fd)`
released 1/4 and recycled the fd number. It is a Linux behavior, not a POSIX
guarantee, and backlogged connections are dropped, not drained. Normative text:
`docs/spec/18-library.md` §32.27.

## Handle guards: cost and the rejected alternative

`crypto`/`tls`/`http`/`image` keep each freed handle's header with a DEAD
sentinel, so a double free or use-after-free aborts by name instead of reading
freed memory. Headers are never reused (a stale handle can never alias a new
one) and never freed: a few dozen bytes per handle ever opened (32 per `crypto`
key, 48 per `image` handle, 55 per `tls` connection, 61 per `http` response on
Linux x86-64/glibc; below ~40k handles it hides under the libraries'
high-water mark). The rejected alternative — a bounded ring that really frees
the oldest header — would cap the cost but drop the diagnosis for old handles;
nothing shipped opens handles in a loop long enough to need it. Lane: `make
handle-guard`.

## `compress`: determinism callers may rely on

`compress` is byte-deterministic per zlib build (the MTIME header field is held
at zero); `tools/tycho-ar`'s archive-twice-`cmp`-identical gate holds it. The
compressed *length* is a zlib tuning detail and is not stable across versions.
