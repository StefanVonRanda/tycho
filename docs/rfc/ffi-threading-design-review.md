# FFI and threading — safety envelope and open item

A review of FFI and threading, written against the feedback that FFI was "too
narrow, hard to use, and unsafe" and threading "very heavy and very very
unsafe". Most of its recommendations have shipped; what remains is the
reasoning behind the FFI-from-threads rule and one open item.

## Outcome

Shipped (the spec is the reference for each):

- `bytes`, an immutable byte buffer that crosses the boundary as `(ptr, len)`,
  in place of hex-marshalling binary through `string`.
- Typed `handle`s with a compiler-known destructor, affine like tasks.
- Nullable string returns (`-> Option(string)`) and `inout` out-parameter
  constructors.
- A cap on live tasks (default 1024, `TYCHO_MAX_TASKS` overrides; exceeding it
  aborts) and `parallel for x in ch:` bounded fan-out over `ncpu()` workers.
  Together these close the unbounded-spawn exhaustion vector without the
  work-stealing pool first proposed.

Deliberate non-goals: C variadics, callbacks into Tycho, and an automatic shim
for non-scalar out-parameters (refused by ABI design).

## Why race-freedom stops at the FFI

Copy-in/copy-out makes **pure Tycho values** race-free: after the copy a task
shares no bytes with its spawner. Three things sit outside that guarantee.

1. **C state.** Value isolation says nothing about a C library's globals. Two
   tasks calling the same library share whatever it shares. The crypto shim is
   safe only because its recycled return buffer is `static __thread`
   (`corelib/crypto/crypto_shim.c`, `g_out`) and Tycho copies the returned
   string into its arena before the next call. A shim using a plain `static`
   buffer would race, and the language could not see it. OpenSSL's one-shot EVP
   calls are thread-safe because each builds its own context — a property of
   OpenSSL, not of Tycho. The rule that follows: shims that return a buffer use
   `__thread`, and the bound library must be thread-safe for the calls made, or
   the calls must be serialised.
2. **Channels.** A channel is shared state by design, internally synchronised.
   "The only shared state is the channel" is the accurate claim, not "no shared
   state".
3. **Failure.** A runtime error in any task — a bounds check, `pop` on an empty
   array, OOM — exits the whole process.

The affine rules themselves (double `wait` dies, implicit join at scope exit,
`parallel for` refusing captured tasks, `inout` captures and cross-chunk
mutation) were reviewed and found sound.

## Open: per-task failure isolation

Not built, by choice. Isolating a task's failure means catching the abort at the
thread boundary and propagating a result instead of exiting — a change to every
abort path in `runtime/tycho_rt.c`. Tycho's stance that an abort stops the
program is internally consistent, so the fail-stop behaviour is documented
rather than replaced. Revisit only if a workload needs a long-running process
to survive one task's bug.
