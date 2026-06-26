# FFI & Threading — design review and improvement plan

Status: original read-only review (citations to `path:line`); **most
recommendations have since shipped** — re-verify against the compilers before
treating any item below as open.

- **FFI — shipped:** R1 `bytes` (`tests/bytes*`), R2 typed `handle`s with
  destructors, R3 nullable `-> Option(string)`, R4 `mut` out-parameter
  constructors (`src/tychoc.c:2742`, `examples/sqlite/demo.ty`).
- **FFI — deliberate non-goals:** R5 variadics / callbacks-into-Tycho; the
  auto-shim for *non-scalar* out-params is rejected by ABI design, not pending.
- **Threading — shipped:** R1 docs-honesty pass; the unbounded-spawn vector is
  closed by a spawn **cap** (ceiling, default 1024 / `TYCHO_MAX_TASKS`) plus
  `parallel for x in ch:` bounded fan-out (`ncpu()` workers) — covering R2's and
  R3's safety goal without the full work-stealing pool R2 literally described.
- **Threading — open / deliberate:** R4 per-task failure isolation (panic still
  `exit(1)`s the process) remains a documented fail-stop stance, not built.

The review below is preserved as the original analysis.

This document responds to two pieces of feedback:

- **FFI** is "too narrow, hard to use, and unsafe."
- **Threading** is "very heavy and very very unsafe."

Both critiques are largely fair against the current implementation. This
review enumerates the concrete pain points with evidence, then proposes
prioritized improvements that fit Tycho's value-semantics + implicit-arena
model. Each recommendation states whether it is an incremental change or a
fundamental one, and the risk it poses to the value-semantic invariant.

---

## Summary

**FFI.** The boundary is deliberately tiny: only scalars, NUL-terminated
`string`, and an opaque `ptr` cross it (`docs/ffi.md:62-83`,
`src/tychoc.c:2515-2554`). That keeps the *language* sound — no foreign
pointer enters Tycho's owned world — but it pushes real cost onto users:

1. No composite types cross (rejected at parse, `src/tychoc.c:2542,2554`).
2. No byte-buffer / `(ptr,len)` story. A `string` cannot hold `0x00`
   (`corelib/hex/hex.ty:7-11`), so all binary is marshaled as hex — see the
   crypto package marshaling *every* key/nonce/ciphertext/digest as a hex
   string (`corelib/crypto/crypto.ty:11-18`, `corelib/crypto/crypto_shim.c:1-25`).
3. `ptr` is fully opaque and unsafe: no type tag, no compiler-known
   destructor, so handles leak and nothing prevents use-after-free or
   passing the wrong handle type (`docs/ffi.md:104-106`; no destructor exists
   — verified by searching `src/tychoc.c` for destructor/drop/dealloc/free).
4. String lifetime rules have a subtle read-once-borrow footgun
   (`src/tychoc.c:5748-5752,5790,5900`).
5. Every out-param / callback API needs a hand-written C shim
   (`docs/ffi.md:127-138`, `examples/sqlite/`, `corelib/crypto/crypto_shim.c`).
6. No variadics, no callbacks into Tycho (`docs/ffi.md:108-110,163-166`).

**Threading.** "Race-free by construction" (`README.md:35-37`,
`docs/concurrency.md:5-9`) is *true for pure Tycho values* and **false the
moment FFI, process-global C state, or a panic is involved**. It is also
heavy: every `spawn` is one OS thread via `pthread_create`
(`runtime/tycho_rt.c:286-288`), `parallel for` fans out `ncpu` threads with a
full deep-copy of captures per chunk (`src/tychoc.c:4119-4132`), there is no
thread pool or work-stealing (`docs/concurrency.md:135-137`), spawning is
unbounded, and a panic/abort in any task `exit(1)`s the whole process
(`docs/concurrency.md:139`, `runtime/tycho_rt.c:921-936`).

**Top recommendations (ranked, both areas):**

1. **First-class immutable `bytes` type crossing as `(ptr,len)`** — removes
   the hex-marshaling tax, the single biggest FFI usability win. Fundamental
   (new type) but fits value semantics cleanly.
2. **Tighten the safety claim in docs** — cheap, honest, high trust value.
   State exactly what "race-free by construction" covers and what it does not
   (FFI, global C state, panic). Add an FFI-from-threads guidance section.
3. **Typed handles with a compiler-known destructor** (`handle "Lib" Db`) —
   fixes leaks + wrong-handle bugs. Incremental on top of `ptr`.
4. **Bounded worker pool for `spawn` / `parallel for`** — caps the
   thread-explosion and thread-creation cost. Runtime-only, no language change.
5. **Auto-shim / out-param sugar** — reduces hand-written C. Incremental,
   medium effort.

---

## FFI — pain points (with citations)

### The boundary today

`extern fn` is bodyless and calls a C symbol directly; the type checker
rejects anything outside the scalar/string/`ptr` table, failing closed:

- Parse + type gate: `src/tychoc.c:2515` (`ffi_scalar_type`), `:2521`
  (`parse_extern_fn`), `:2540` (rejects `inout`/`mut` params), `:2542`
  (rejects composite params), `:2554` (rejects composite return).
- Type table: `docs/ffi.md:62-71`. `int/char/float/bool` → scalar long/double;
  `string` → `char *`; `ptr` → `void *`; void return allowed.
- Link line assembled in one `cc` call: `src/tychoc.c:8547-8616`. Each
  `extern "Lib"` adds `-lLib` (`:5001` `add_link`). `--link/--shim/--pkg`
  passthrough at `:8563-8567`. Auto-discovered `<pkg>_shim.c` + `deps`
  pkg-config at `:8355-8358`, `:2902-2927`, `:8614-8616`.
- String return is arena-copied so Tycho never holds a foreign pointer
  (`src/tychoc.c:6003-6010`, `tycho_str_from_c`, NULL→`""`).

### Pain point 1 — no composite types cross

Arrays, maps, structs, `Option`/`Result`, tuples are all rejected
(`src/tychoc.c:2542,2554`; `docs/ffi.md:78-83`). Rationale is sound: those
have Tycho-internal C layouts, not a stable ABI. But it means any aggregate
must be flattened into scalars/strings/`ptr` or carried through a hand-written
shim. For a struct-heavy C API this is a lot of boilerplate.

### Pain point 2 — no byte buffer; binary marshaled as hex (the biggest one)

A Tycho `string` is a NUL-terminated `char *` (`docs/ffi.md:73`), so it cannot
carry an interior `0x00`. `corelib/hex/hex.ty:7-11` documents this directly:
`decode("00")` is `""` — a NUL byte is dropped. Consequence: the entire crypto
package marshals **all** binary data as lowercase hex
(`corelib/crypto/crypto.ty:11-18`, shim header `corelib/crypto/crypto_shim.c:1-15`):

- Every key, nonce, ciphertext, signature, digest crosses as hex
  (`crypto.ty` externs `cx_sha256_hex`, `cx_aead_encrypt`, etc.).
- The shim hex-decodes on the way in and hex-encodes on the way out
  (`crypto_shim.c:38-50` `out_hex`, `:60-73` `hexdec`).
- Cost: 2× memory for every buffer, plus encode/decode CPU on every call, plus
  a layer of conversion code the user must write and audit. This is the single
  clearest "FFI is hard to use" datapoint in the repo.

### Pain point 3 — `ptr` is opaque and unsafe

`ptr` is `void *` with only three operations: pass back to C, compare
(`==`/`!=` vs another `ptr` or `null`), and `is_null` (`docs/ffi.md:104-106`;
`E_NULL` → `T_PTR` at `src/tychoc.c:3291`; literal at `:1590`). Three distinct
hazards, none mitigated:

- **No type tag.** A `sqlite3 *` and a `FILE *` are both `ptr`; the compiler
  cannot stop you passing one where the other is expected.
- **No destructor / leak.** Nothing in the compiler frees a `ptr` at scope
  exit (verified: no destructor/drop/dealloc/free-for-ptr in `src/tychoc.c`;
  only task/channel finalizers exist, `:6380-6390`). A handle from
  `sqlite3_open` leaks unless the user remembers to call `sqlite3_close`.
- **Dangling / double-free.** Tycho copies a `ptr` by value freely (it rides
  scalar paths, `docs/ffi.md:106`), so the same handle can be live in several
  places; use-after-free and double-free are entirely on the user.

### Pain point 4 — string-lifetime footguns

The rule (`docs/ffi.md:85-102`): a returned `string` is copied into the
caller's arena; `NULL` becomes `""`. An optimization — the **read-once
borrow** — skips the copy when the result is the *direct* argument of
`len()`/`print()`/`println()` (`src/tychoc.c:5748-5752` `is_extern_str_call`,
applied at `:5790` for `len`, `:5900` for print/println). Footguns:

- `NULL → ""` silently erases the C/Tycho distinction between "no value" and
  "empty string". A caller that needs to detect absence cannot (the crypto
  shim works around this with an explicit `"!err"` sentinel,
  `crypto_shim.c:17-20`, `crypto.ty:14-17`).
- The read-once optimization is invisible and shape-sensitive: `len(f())` reads
  in place, but `x := f(); len(x)` copies. Equivalent-looking code takes
  different paths. It is sound today (the borrow cannot escape the consuming
  call), but it is a sharp edge for anyone extending the FFI, and a shim that
  returns a recycled buffer (see crypto `__thread g_out`, below) is only safe
  *because* of the arena copy — a future "borrow more aggressively" change
  could break that contract.

### Pain point 5 — manual shims for every out-param / callback

Out-parameter constructors (the dominant C idiom, e.g.
`sqlite3_open(path, &db)`) cannot be expressed; the user writes a C wrapper
that returns the handle and passes it with `--shim` (`docs/ffi.md:127-138`,
`examples/sqlite/`). Same for any function whose result comes back through a
pointer argument. The crypto package is essentially one large shim
(`corelib/crypto/crypto_shim.c`).

### Pain point 6 — no variadics, no callbacks into Tycho

`printf`-style variadics need a fixed-arity C wrapper (`docs/ffi.md:108,163`).
A Tycho function value is a non-C-ABI fat pointer, so C cannot call back into
Tycho (`docs/ffi.md:109-110,165-166`) — qsort-comparator / event-callback APIs
are unreachable without a C trampoline. These are documented as by-design
non-goals; listed here for completeness, not necessarily to fix.

### FFI — ranked recommendations

Ranked by value / effort.

**R1 (highest value). First-class immutable `bytes` type, crosses as `(ptr,len)`.**
- *Design.* Add an immutable byte buffer type (`bytes`, or `[u8]`). It owns a
  length-carrying buffer in the arena like any value, deep-copies on the value
  seam exactly as a `string` does, but allows interior `0x00`. Across the FFI
  it lowers to two C arguments `(const unsigned char *ptr, long len)` for a
  parameter, and an extern returning `bytes` uses an out-param-len shim
  convention (or a small compiler-known `{ptr,len}` return struct emitted by
  Tycho, copied into the arena like the current string return at
  `src/tychoc.c:6003-6010`).
- *Why.* Eliminates the hex-marshaling tax that dominates the crypto package
  and would hit any binary-data library (compression, image, network, hashing).
  Halves memory and removes the encode/decode CPU and code.
- *Incremental or fundamental.* Fundamental — a new core type with its own
  arena/copy paths in both compilers — but it is *parallel* to `string`, so
  much of the string machinery is a template.
- *Risk to value semantics.* Low. `bytes` is just another value type; deep-copy
  on the seam preserves the no-aliasing invariant. The only new surface is the
  `(ptr,len)` lowering, which is as safe as the existing `string` `char *`
  lowering (arena-copy on return keeps foreign memory out).

**R2. Typed handle wrapper over `ptr`, with an optional compiler-known free.**
- *Design.* Allow a newtype-style declaration that tags a handle and names its
  destructor, e.g. `extern handle "sqlite3" Db free sqlite3_close`. The
  compiler treats `Db` as distinct from `ptr` and from other handles (fixes the
  wrong-handle hazard, pain point 3a), and emits the named free at scope exit
  for an *owned* handle (fixes the leak, pain point 3b) — reusing the existing
  task/channel finalizer mechanism (`src/tychoc.c:6380-6390`) that already runs
  destructor calls at scope end.
- *Why.* Turns the most dangerous FFI primitive into something the compiler can
  reason about. Most handle-based libs (SQLite, SDL, curl) become safe-by-default.
- *Incremental or fundamental.* Incremental — `ptr` stays as the escape hatch;
  the handle type is a tagged newtype over it plus a finalizer registration.
- *Risk to value semantics.* Medium. Ownership of a foreign resource is *not*
  value-semantic (you cannot deep-copy a live `sqlite3 *`). Must define handles
  as **affine like tasks** (`docs/concurrency.md:35-46`): no copying, single
  owner, freed once at scope exit. This is the same affine machinery tasks
  already use, so it is consistent with the model rather than a violation of it.
  Decide explicitly whether handles are movable (`inout`) or strictly local.

**R3. `len`-carrying / nullable string variants, or an explicit "no read-once"
opt-out.**
- *Design.* Two small pieces: (a) an `extern fn ... -> string?` form that
  surfaces a real `None` for a C `NULL` return instead of silently mapping to
  `""` (fixes pain point 4a); (b) document the read-once borrow as a stable
  guarantee or gate it behind an attribute, so shim authors can rely on it.
- *Why.* Removes the silent `NULL`/`""` conflation that forces sentinel hacks
  like crypto's `"!err"`.
- *Incremental or fundamental.* Incremental.
- *Risk.* Low. `Option(string)` already exists in the language.

**R4. Auto-shim generation for out-parameter constructors.**
- *Design.* A declaration form that lets the compiler synthesize the trivial
  out-param→return shim it currently makes users write by hand, e.g.
  `extern fn sqlite3_open(path: string, out db: ptr) -> int` emitting the
  `&db` wrapper automatically. Keeps `--shim` for everything the synthesizer
  cannot express.
- *Why.* Removes the most common reason a binding needs hand-written C.
- *Incremental or fundamental.* Incremental, medium effort (codegen of a small
  C wrapper, alongside the existing shim plumbing at `src/tychoc.c:8355-8358`).
- *Risk.* Low — generated C is mechanical; fail closed to `--shim` if the shape
  is anything non-trivial.

**R5 (lowest priority). Variadics / callbacks-into-Tycho.**
- These are documented non-goals (`docs/ffi.md:108-110,163-166`). A callback
  story would require emitting a C-ABI trampoline that re-enters Tycho with a
  fresh arena — large, and it punctures the "no foreign control flow into
  Tycho" stance. Recommend leaving as-is unless a concrete library forces it.

---

## Threading — cost analysis + safety envelope (with citations)

### Why it is heavy

- **One OS thread per `spawn`.** `tycho_task_start` calls `pthread_create`
  directly with no pool (`runtime/tycho_rt.c:286-289`). Each task allocates a
  fresh root arena (`tycho_task_new`, `:268-274`). Thread creation +
  teardown + a fresh arena per task is the per-spawn cost.
- **`parallel for` forks `ncpu` threads with full capture copy per chunk.** K =
  `tycho_ncpu()` chunk tasks (`src/tychoc.c:4119-4132`; runtime `:551-558`,
  `TYCHO_THREADS` overrides). Every captured variable is deep-copied into each
  chunk's root arena — the honest per-chunk cost, documented at
  `docs/concurrency.md:57`. For large captures this is real memory and time.
- **No thread pool, no work-stealing.** Explicitly: "no work-stealing runtime:
  a blocked waiter is a parked OS thread" (`docs/concurrency.md:135-137`).
  Fine for the benchmark shape (a fixed fan-out of long tasks), painful for
  many short tasks (each pays full thread create/destroy).
- **Blocking is a parked OS thread.** A `recv`/`wait`/select waits on a
  spin → `sched_yield` → 1ms timed-park ladder (`runtime/tycho_rt.c:398-418`,
  `docs/concurrency.md:112-115`). Mostly cheap on the fast path, but a blocked
  task holds a whole OS thread.

The measured numbers are good for the *intended* shape (parreduce at C parity,
pipeline beating Go — `docs/concurrency.md:120-130`), which is why this is a
cost-model issue, not a correctness one. The heaviness bites a workload of many
small or short-lived tasks.

### Where it is actually unsafe (despite copy-in/copy-out)

The claim "race-free by construction" (`README.md:35-37`,
`docs/concurrency.md:5-9`) holds for **pure Tycho values** — after copy-in a
task shares zero bytes (`runtime/tycho_rt.c:257-266`). It does **not** hold in
these cases, and the docs only partially flag them:

1. **FFI into non-thread-safe or stateful C.** Tycho's value isolation says
   nothing about C's global state. Two tasks each calling into the same C
   library hit whatever sharing that library has. The repo's own crypto shim is
   a live example to scrutinize:
   - The recycled return buffer is `static __thread char *g_out`
     (`corelib/crypto/crypto_shim.c:36`), so it *is* per-thread-safe — good.
     But this is a property of *this* shim, not of the FFI, and it relies on
     Tycho arena-copying the returned string before the next call
     (`crypto_shim.c:10-14`). A shim author who uses a plain `static` buffer
     (not `__thread`) would have a cross-thread data race that the language's
     "race-free by construction" claim does nothing to prevent.
   - OpenSSL EVP one-shot calls as used here (`EVP_Digest`, `HMAC`,
     `EVP_PKEY_*` with a fresh `EVP_PKEY_CTX` per call, `crypto_shim.c:88-173,
     219-317`) are generally thread-safe in OpenSSL 1.1+/3.x because each call
     builds its own context. That is an OpenSSL property the Tycho model cannot
     guarantee; a different library, or OpenSSL with an `ENGINE`/global config,
     would not be covered.
2. **Channels are shared mutable state — by design.** The channel is "the ONE
   intentionally shared object" (`runtime/tycho_rt.c:316-330`). It is
   internally synchronized, so it is safe, but it *is* a shared-state mechanism,
   so "no shared state" is an overstatement; "the only shared state is the
   internally-synchronized channel" is the accurate phrasing.
3. **Unbounded spawning / resource exhaustion.** Nothing caps the number of
   live threads. A `spawn` in a loop, or recursive spawning, creates threads
   until `pthread_create` fails, at which point the runtime prints and
   `exit(1)`s (`runtime/tycho_rt.c:287-289`). That is a fail-stop, not memory
   corruption, but it is a trivial fork-bomb / resource-exhaustion vector and is
   not mentioned as a limit.
4. **Panic/abort in a task kills the whole process.** Any runtime error in a
   task — bounds check, `pop` from empty, OOM, divide, `exit(1)` paths
   throughout `runtime/tycho_rt.c` (e.g. `:921-936`, `:87`, `:927-928`) — takes
   down every other task with it. Documented (`docs/concurrency.md:139`) but
   worth elevating: there is no task-level isolation of failure, unlike Erlang
   processes, which the intro compares Tycho to (`docs/concurrency.md:8-9`).
5. **Affine/implicit-join edge cases.** The affine rules are enforced and look
   sound (double-wait dies loudly, `runtime/tycho_rt.c:297-300`; implicit join
   at every scope exit, `:310-315`; `parallel for` rejects captured tasks /
   mut captures / cross-chunk mutation, `src/tychoc.c:4176-4263`). No unsafety
   found here — call out as *verified sound*, not a gap.

### Threading — ranked recommendations

**R1 (highest value, lowest risk). Make the safety claim honest in the docs.**
- *Action.* Reword `README.md:35-37` and `docs/concurrency.md:5-9` from
  "race-free by construction" (unqualified) to scope it explicitly:
  > Pure Tycho values crossing a thread boundary are race-free by construction
  > (deep copy = zero sharing). This does **not** extend to: FFI calls into C
  > (the C library's own thread-safety applies — see below), the channel itself
  > (shared but internally synchronized), or failure isolation (a panic in any
  > task aborts the whole process).
- Add an **"FFI from threads"** subsection: shims that return a buffer must use
  `__thread` (point to `corelib/crypto/crypto_shim.c:36` as the reference
  pattern), and the bound C library must be documented thread-safe for the
  calls made.
- *Effort.* Trivial. *Value.* High — the current wording is the specific thing
  the feedback flagged as overclaiming.

**R2. Bounded worker pool / thread cap for `spawn` and `parallel for`.**
- *Design.* A runtime worker pool sized to `tycho_ncpu()` (reuse the existing
  `tycho_ncpu` / `TYCHO_THREADS` knob, `runtime/tycho_rt.c:551-558`). `spawn`
  submits a closure to the pool instead of `pthread_create` per call; `wait`
  blocks on that task's completion. `parallel for` already fans out exactly K
  chunks so it maps onto the pool directly. Keep the per-task root arena
  (the isolation guarantee is unchanged); only the *thread* is pooled.
- *Why.* Removes per-spawn thread create/destroy cost and caps live threads —
  directly answers "very heavy" and the unbounded-spawn exhaustion vector.
- *Incremental or fundamental.* Runtime-only; no language or codegen change if
  the pool presents the same `tycho_task_start` / `tycho_task_join` interface
  (`runtime/tycho_rt.c:286-300`). Genuinely incremental.
- *Risk to value semantics.* None — pooling threads does not change which bytes
  are shared; the copy-in/copy-out seam is untouched. One subtlety: a pooled
  worker must flush its thread-local block pool (`tycho_pool_flush`,
  `:278-284`) between tasks or hand arenas back correctly, since the block pool
  is `__thread` (`:113`) and a pooled thread outlives a single task.
  *Caveat:* a blocking `recv`/`wait` inside a pooled task can starve the pool
  (classic pool-deadlock); needs either a "block creates a temporary extra
  worker" rule or documentation that blocking tasks should not be pooled. This
  is the one real design risk in R2 — call it out before building.

**R3. Optional bounded-concurrency primitive for fan-out loops.**
- *Design.* A way to spawn N tasks with a live-cap (semaphore over the pool), so
  `spawn` in a loop cannot exhaust resources even without R2's full pool.
- *Why.* Cheap safety net for the fork-bomb vector if R2 is deferred.
- *Incremental.* Yes. *Risk.* Low.

**R4 (document, do not necessarily build). Failure isolation.**
- True per-task failure isolation (a panic in one task not killing the process)
  would require catching aborts at the thread boundary and propagating a result
  rather than `exit(1)` — a large change touching every `exit(1)` path in
  `runtime/tycho_rt.c`. Recommend documenting the current fail-stop behavior
  prominently (R1) rather than building isolation now; Tycho's "abort = stop the
  program" stance is internally consistent and changing it is a big project.

---

## Suggested next steps (in order)

1. **Docs honesty pass (Threading R1 + FFI pain-point notes).** Lowest effort,
   highest trust return; directly addresses the "unsafe / overclaiming"
   feedback. Scope the race-free claim, add FFI-from-threads guidance, document
   the unbounded-spawn and panic-kills-process limits as first-class.
2. **`bytes` type (FFI R1).** The biggest usability win; removes the
   hex-marshaling tax that the crypto package demonstrates. Prototype the
   `(ptr,len)` lowering against a binary-data library (e.g. re-bind a hash
   function to take `bytes` directly and compare to the hex path).
3. **Bounded worker pool (Threading R2).** Runtime-only; resolves "very heavy"
   and the exhaustion vector. Watch the pooled-blocking-task starvation caveat.
4. **Typed handles with destructors (FFI R2).** Makes `ptr` safe-by-default for
   the common handle libraries; reuses the affine + finalizer machinery that
   already exists for tasks.
5. **Auto-shim out-params + nullable string return (FFI R3/R4).** Ergonomics
   polish once the foundational types/handles land.

Everything above preserves the two pillars (value semantics + implicit arenas).
The `bytes` type and the worker pool are pure additions to those pillars; typed
handles are the one place foreign-resource ownership is *not* value-semantic, so
they must be affine (single-owner, no copy) exactly as tasks already are.
